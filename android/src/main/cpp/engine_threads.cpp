// ---------------------------------------------------------------------------
// File: engine_threads.cpp
// Purpose: Implementation of the four decoder thread types: WAV, FLAC, local
//          media (AMediaCodec), and streaming media (URL-based).
// Importance: Each thread decodes audio and pushes float PCM into RingBuffer.
// Missing: None
// Known issues: None
// ---------------------------------------------------------------------------

#include "engine_threads.h"
#include "engine_state.h"
#include "ring_buffer.h"
#include "aaudio_utils.h"
#include "flac_handler.h"
#include "wav_handler.h"
#include "dispatcher.h"
#include "effect.h"
#include "common.h"
#include "dsp_processor.h"
#include "limiter.h"

#include <cstdio>
#include <vector>
#include <cstring>
#include <thread>
#include <chrono>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <aaudio/AAudio.h>
#include <FLAC/stream_decoder.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>

// ─── Constants ───────────────────────────────────────────────────────────────
static constexpr float HALF_PI = 1.57079632679f;

// ─── Gapless Helper Functions ───────────────────────────────────────────────

static inline void abortGapless(TrackState &trk, bool abort = false) {
    trk.gapLessVersion++;
    if (abort) trk.gapLessAbort = 1;
    trk.hasNext = 0;
}

static inline void resetAfterGapless(TrackState &trk, int32_t sr, int32_t ch,
                                     int64_t totalFrames) {
    strncpy(trk.path, trk.nextPath, sizeof(trk.path) - 1);
    trk.path[sizeof(trk.path) - 1] = '\0';
    trk.gapLessVersion++;
    trk.hasNext = 0;
    trk.sampleRate = sr;
    trk.channels = ch;
    trk.totalFrames = totalFrames;
    trk.writtenFrames = 0;
    trk.fadeHistPos = 0;
    trk.fadeHistCount = 0;
    trk.skipPacing = 0;
}

// Pre-decode + format check + resample + crossfade push for FLAC gapless.
// Returns false if gapless should be aborted.
static bool flacGaplessPrep(TrackState &trk, int32_t ch, int32_t ti) {
    if (!trk.preBufReady && gCtl.outChannels >= 2) {
        // Probe next file — if it's not real FLAC, can't gapless
        ProbedFormat nextFmt = probeAudioFormat(trk.nextPath);
        if (nextFmt != ProbedFormat::FLAC && nextFmt != ProbedFormat::UNKNOWN) {
            LOGI("FLAC thread[%d]: gapless next is format %d (not FLAC), fading out ring buffer: %s",
                 ti, (int)nextFmt, trk.nextPath);
            if (trk.ringBuf && trk.writtenFrames > 0 && ch > 0) {
                int32_t avail = trk.ringBuf->available(ch);
                if (avail > 0) {
                    std::vector<float> fadeBuf(avail * ch);
                    for (int32_t i = 0; i < avail; i++) {
                        float g = sinf((1.0f - (float)i / avail) * HALF_PI);
                        for (int c = 0; c < ch; c++)
                            fadeBuf[i * ch + c] = trk.lastFrame[c < 2 ? c : 0] * g;
                    }
                    trk.ringBuf->push(fadeBuf.data(), avail, ch);
                    LOGI("FLAC thread[%d]: faded out %d ring buffer frames", ti, avail);
                }
            }
            abortGapless(trk, true);
            return false;
        }
        if (!predecodeFlac(trk, trk.nextPath)) {
            LOGW("FLAC thread[%d]: gapless predecode failed for %s — aborting gapless", ti, trk.nextPath);
            abortGapless(trk, true);
            return false;
        }
    }
    if (!checkFlacFormatMatch(trk.nextPath, gCtl.sampleRate, gCtl.outChannels)) {
        bool channelsDiffer = trk.preBufReady && trk.preBufChannels != gCtl.outChannels;
        if (!trk.preBufReady) {
            LOGI("FLAC thread[%d]: format mismatch — stream=%dHz/%dch, next=%s. Aborting gapless.",
                 ti, gCtl.sampleRate, gCtl.outChannels, trk.nextPath);
            abortGapless(trk, true);
            return false;
        }
        if (channelsDiffer) {
            LOGI("FLAC thread[%d]: channel mismatch — stream=%dch, next=%dch. Aborting gapless.",
                 ti, gCtl.outChannels, trk.preBufChannels);
            abortGapless(trk, true);
            return false;
        }
    }
    bool srMismatch = (trk.preBufReady && trk.preBufSampleRate != gCtl.sampleRate
                       && trk.preBufSampleRate > 0);
    if (srMismatch) {
        double ratio = (double)gCtl.sampleRate / trk.preBufSampleRate;
        int32_t outFrames = (int32_t)(trk.preBufFrames * ratio);
        float *resampled = new float[outFrames * 2];
        int32_t ringBefore = trk.ringBuf ? trk.ringBuf->available(ch) : -1;
        auto t0 = std::chrono::steady_clock::now();
        resampleSinc(resampled, outFrames, trk.preBuf, trk.preBufFrames, 2, ratio);
        auto t1 = std::chrono::steady_clock::now();
        int64_t resampleMs = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        delete[] trk.preBuf;
        trk.preBuf = resampled;
        trk.preBufFrames = outFrames;
        LOGI("FLAC thread[%d]: resampled preBuf %d→%d frames (%dHz→%dHz) [%lldms] ringBuf=%d→%d",
             ti, trk.preBufOrigFrames > 0 ? trk.preBufOrigFrames : trk.preBufFrames,
             outFrames, trk.preBufSampleRate, gCtl.sampleRate,
             (long long)resampleMs, ringBefore,
             trk.ringBuf ? trk.ringBuf->available(ch) : -1);
    }
    int32_t availBefore = trk.ringBuf ? trk.ringBuf->available(ch) : 0;
    LOGI("FLAC thread[%d]: gapless crossfade: ringBuf avail before=%d, preBufFrames=%d, fadeHistCount=%d",
         ti, availBefore, trk.preBufFrames, trk.fadeHistCount);
    writeGaplessCrossfade(trk, ch);
    int32_t availAfter = trk.ringBuf ? trk.ringBuf->available(ch) : 0;
    LOGI("FLAC thread[%d]: gapless crossfade: ringBuf avail after=%d", ti, availAfter);
    return true;
}

// Destroy old FLAC decoder, create new one, seek past preBuf.
// Returns false on failure.
static bool flacSwapDecoder(TrackState &trk, FLAC__StreamDecoder *&decoder,
                            PlayState &ps, int32_t ti,
                            int32_t origPreFrames, bool srMismatch,
                            int32_t &ch, int32_t &threshold) {
    trk.preBufFrames = 0;
    trk.preBufOrigFrames = 0;
    FLAC__stream_decoder_finish(decoder);
    FLAC__stream_decoder_delete(decoder);
    decoder = nullptr;
    if (srMismatch) {
        trk.resampleToStream = 1;
        trk.streamSampleRate = gCtl.sampleRate;
        LOGI("FLAC thread[%d]: SR mismatch — real-time resample %d→%d enabled",
             ti, trk.preBufSampleRate, gCtl.sampleRate);
    } else {
        trk.resampleToStream = 0;
        trk.streamSampleRate = 0;
    }
    decoder = FLAC__stream_decoder_new();
    if (!decoder) return false;
    FLAC__stream_decoder_set_metadata_respond_all(decoder);
    auto st = FLAC__stream_decoder_init_file(
        decoder, trk.nextPath, flacEngineWriteCallback, metadataCallback, errorCallback, &ps);
    if (st != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
        FLAC__stream_decoder_delete(decoder); decoder = nullptr;
        return false;
    }
    FLAC__stream_decoder_process_until_end_of_metadata(decoder);
    if (ps.info.sampleRate == 0 || ps.info.channels == 0) {
        FLAC__stream_decoder_delete(decoder); decoder = nullptr;
        return false;
    }
    resetAfterGapless(trk, ps.info.sampleRate, ps.info.channels, ps.info.totalSamples);
    ch = ps.info.channels;
    threshold = RingBuffer::pacingThreshold(ch);
    if (origPreFrames > 0) {
        FLAC__stream_decoder_seek_absolute(decoder, origPreFrames);
        trk.writtenFrames = origPreFrames;
    }
    return true;
}

// Validate media format via extractor (shared by local + stream gapless).
// Returns false if format doesn't match.
static bool checkMediaExtractorFormat(AMediaExtractor *tmpExt, int32_t targetSR,
                                      int32_t targetCh, int32_t &outSR, int32_t &outCh) {
    outSR = 0; outCh = 0;
    bool found = false;
    for (int32_t i = 0; i < AMediaExtractor_getTrackCount(tmpExt); i++) {
        AMediaFormat *fmt = AMediaExtractor_getTrackFormat(tmpExt, i);
        const char *m = NULL;
        AMediaFormat_getString(fmt, AMEDIAFORMAT_KEY_MIME, &m);
        if (m && strncmp(m, "audio/", 6) == 0) {
            AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_SAMPLE_RATE, &outSR);
            AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &outCh);
            AMediaFormat_delete(fmt);
            found = true;
            break;
        }
        AMediaFormat_delete(fmt);
    }
    return found && outSR == targetSR && outCh == targetCh;
}

static void fadeOutAndDrain(TrackState &trk, int32_t ch) {
    if (trk.ringBuf && trk.writtenFrames > 0) {
        std::vector<float> fadeBuf(FADE_FRAMES * ch);
        for (int i = 0; i < FADE_FRAMES; i++) {
            float g = sinf((1.0f - (float)i / FADE_FRAMES) * HALF_PI);
            for (int c = 0; c < ch; c++)
                fadeBuf[i * ch + c] = (c < 2 ? trk.lastFrame[c] : 0) * g;
        }
        trk.ringBuf->push(fadeBuf.data(), FADE_FRAMES, ch);
    }
    // Wait for AAudio to drain the ring buffer — up to 5s safety limit
    if (trk.ringBuf) {
        int32_t maxWaitMs = 5000;
        while (trk.ringBuf->available(ch) > 0 && maxWaitMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            maxWaitMs -= 10;
        }
        if (maxWaitMs <= 0) {
            LOGW("FLAC thread: drain timeout — %d frames remaining",
                 trk.ringBuf->available(ch));
        }
    }
}

static void initTrackEq(int ti, int32_t sampleRate, int32_t channels) {
    TrackState &trk = gCtl.tracks[ti];
    if (gCtl.trackEqHasConfig[ti] || gCtl.trackEqPending[ti].load()) {
        if (!trk.dsp) { trk.dsp = new DspProcessor(); trk.dsp->init(sampleRate, channels); }
        applyPendingTrackEq(ti);
    }
}

static int32_t findAudioTrack(AMediaExtractor *ext, int32_t &sr, int32_t &ch, int64_t &durationUs) {
    for (int32_t i = 0; i < AMediaExtractor_getTrackCount(ext); i++) {
        AMediaFormat *fmt = AMediaExtractor_getTrackFormat(ext, i);
        const char *m = NULL;
        AMediaFormat_getString(fmt, AMEDIAFORMAT_KEY_MIME, &m);
        if (m && strncmp(m, "audio/", 6) == 0) {
            AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_SAMPLE_RATE, &sr);
            AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &ch);
            AMediaFormat_getInt64(fmt, AMEDIAFORMAT_KEY_DURATION, &durationUs);
            AMediaFormat_delete(fmt);
            return i;
        }
        AMediaFormat_delete(fmt);
    }
    return -1;
}

static int32_t processCodecOutput(
    TrackState &trk, int ti, int32_t outCh,
    AMediaCodec *codec, ssize_t outIdx, const AMediaCodecBufferInfo &info,
    std::vector<float> &decodeBuf)
{
    if (info.size <= 0 || !gCtl.stream || trk.paused) return 0;
    size_t outSize;
    uint8_t *outBuf = AMediaCodec_getOutputBuffer(codec, outIdx, &outSize);
    if (!outBuf) return 0;
    outBuf += info.offset;
    int32_t totalS = info.size / 2;
    int32_t frames = totalS / outCh;
    if (totalS > (int32_t)decodeBuf.size()) decodeBuf.resize(totalS);
    float *fb = decodeBuf.data();
    for (int32_t i = 0; i < totalS; i++) {
        int16_t vs = outBuf[i*2] | (outBuf[i*2+1]<<8);
        fb[i] = vs / 32768.0f;
    }
    if (trk.ringBuf) {
        DspProcessor *eq = getTrackEq(ti);
        if (eq) eq->process(fb, frames, outCh);
        updateFadeHistory(trk, fb, frames, outCh);
        applyFadeIn(trk, fb, frames, outCh);
        trk.ringBuf->push(fb, frames, outCh);
    }
    if (trk.pcmRingBuf) trk.pcmRingBuf->push(fb, frames, outCh);
    if (frames > 0) {
        trk.lastFrame[0] = fb[(frames - 1) * outCh];
        if (outCh > 1) trk.lastFrame[1] = fb[(frames - 1) * outCh + 1];
    }
    return frames;
}

static AMediaCodec* createCodecFromExtractor(
    AMediaExtractor *extractor, int32_t audioTrack, const char *mime, AMediaFormat *trackFmt) {
    AMediaExtractor_selectTrack(extractor, audioTrack);
    AMediaCodec *codec = AMediaCodec_createDecoderByType(mime);
    if (!codec || AMediaCodec_configure(codec, trackFmt, NULL, NULL, 0) != AMEDIA_OK ||
        AMediaCodec_start(codec) != AMEDIA_OK) {
        if (codec) AMediaCodec_delete(codec);
        AMediaFormat_delete(trackFmt);
        return nullptr;
    }
    AMediaFormat_delete(trackFmt);
    return codec;
}

static bool initFirstTrackStream(int32_t sr, int32_t ch) {
    if (gCtl.stream || sr <= 0 || ch <= 0) return true;
    gCtl.outChannels = ch;
    if (!gCtl.dsp) gCtl.dsp = new DspProcessor();
    gCtl.dsp->init(sr, ch);
    applyPendingEq();
    if (!gCtl.limiter) gCtl.limiter = new Limiter();
    ensureFxChain((float)sr, ch);
    gCtl.stream = createAAudioStreamCallback(sr, ch, aaudioDataCallback, nullptr);
    if (!gCtl.stream) return false;
    gCtl.sampleRate = sr;
    return true;
}

// ─── WAV Playback Thread ─────────────────────────────────────────────────────

void wavPlaybackThread(int ti) {
    TrackState &trk = gCtl.tracks[ti];
    uint8_t *data = trk.wavData;
    uint32_t dataSize = trk.wavDataSize;
    int32_t fs = trk.wavFrameSize;
    int32_t sr = trk.sampleRate, ch = trk.channels, bps = trk.bitsPerSample;
    int64_t total = trk.totalFrames;

    LOGI("WAV thread[%d]: sr=%d ch=%d bps=%d totalFrames=%lld",
         ti, sr, ch, bps, (long long)total);

    // First track sets the shared output config
    bool wasNew = !gCtl.stream;
    if (!initFirstTrackStream(sr, ch)) {
        LOGE("WAV thread[%d]: createAAudioStreamCallback failed", ti);
        delete[] data; trk.wavData = nullptr; trk.running = 0; return;
    }
    if (wasNew) LOGI("WAV thread[%d]: shared AAudio stream created (sr=%d ch=%d)", ti, sr, ch);

    // Per-track EQ
    initTrackEq(ti, sr, ch);

    trk.running = 1;
    trk.fadeLen.store(crossfadeMsToFrames(gCtl.crossfadeMs.load()));
    int32_t blockSize = 4096;
    int32_t threshold = RingBuffer::pacingThreshold(ch);

    // Pre-alloc decode buffer — reused every iteration (avoids heap alloc per 4096 frames)
    std::vector<float> floatBuf(blockSize * ch);

    uint64_t _st = 0;
    int64_t seek = -1;
    read(trk.stopFd, &_st, sizeof(_st)); // Drain phantom data

    while (true) {
        _st = 0;
        if (read(trk.stopFd, &_st, sizeof(_st)) > 0) {
            LOGI("WAV thread[%d]: got stop signal", ti);
            break;
        }

        // Loop: reset to beginning when track completes
        if (trk.writtenFrames.load() >= total) {
            int rc = trk.repeatCount.load();
            if (rc != 0) {
                if (rc > 0) trk.repeatCount = rc - 1;
                trk.writtenFrames = 0;
                if (trk.ringBuf) trk.ringBuf->reset();
                if (trk.repeatCount.load() <= 0 && rc > 0) {
                    break;
                }
                continue;
            } else if (trk.hasNext) {
                LOGI("WAV thread[%d]: gapless: loading %s (wf=%lld total=%lld)",
                     ti, trk.nextPath, (long long)trk.writtenFrames.load(), (long long)total);
                goto wav_gapless;
            } else break;
        }

        if (trk.paused) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        seek = trk.seekToFrame.exchange(-1);
        if (seek >= 0) {
            trk.writtenFrames = seek < total ? seek : total;
            if (trk.ringBuf) trk.ringBuf->reset();
            LOGI("WAV thread[%d]: seek to frame %lld (total=%lld)", ti, (long long)trk.writtenFrames.load(), (long long)total);
            if (total - seek <= SEEKGAP_THRESHOLD && trk.hasNext && trk.repeatCount.load() == 0) {
                LOGI("WAV thread[%d]: seek-to-end -> forcing gapless transition", ti);
                goto wav_gapless;
            }
        }

        // Normal decode path
        { int32_t effThreshold = (trk.hasNext && trk.repeatCount.load() == 0) ? std::min(threshold, 4096) : threshold;
          if (trk.ringBuf && trk.ringBuf->available(ch) > effThreshold && !trk.skipPacing) {
              std::this_thread::sleep_for(std::chrono::milliseconds(10));
              continue;
          }
        }

        { int32_t rem = (int32_t)(total - trk.writtenFrames);
          int32_t chunk = rem < blockSize ? rem : blockSize;
          int32_t base = (int32_t)trk.writtenFrames;

          for (int32_t i = 0; i < chunk; i++) {
              for (int32_t c = 0; c < ch; c++) {
                  int32_t off = (base + i) * fs;
                  float s = 0;
                  switch (bps) {
                      case 8:  s = (data[off + c] - 128) / 128.0f; break;
                      case 16: { int16_t v = data[off+c*2] | (data[off+c*2+1]<<8); s = v/32768.0f; break; }
                      case 24: { int32_t v = data[off+c*3]|(data[off+c*3+1]<<8)|(data[off+c*3+2]<<16); if(v&0x800000)v|=~0xFFFFFF; s=v/8388608.0f; break; }
                      case 32: { int32_t v = data[off+c*4]|(data[off+c*4+1]<<8)|(data[off+c*4+2]<<16)|(data[off+c*4+3]<<24); s=v/2147483648.0f; break; }
                  }
                  floatBuf[i * ch + c] = s;
              }
          }

          if (trk.ringBuf) {
              DspProcessor *eq = getTrackEq(ti);
              if (eq) eq->process(floatBuf.data(), chunk, ch);
              updateFadeHistory(trk, floatBuf.data(), chunk, ch);
              applyFadeIn(trk, floatBuf.data(), chunk, ch);
              int32_t pushed = trk.ringBuf->push(floatBuf.data(), chunk, ch);
              trk.writtenFrames += pushed;
              if (pushed > 0) {
                  trk.lastFrame[0] = floatBuf[(pushed - 1) * ch];
                  if (ch > 1) trk.lastFrame[1] = floatBuf[(pushed - 1) * ch + 1];
                  pushPositionToDart(ti, trk.writtenFrames.load() * 1000 / sr, true, trk.lastCallbackMs, gCtl.dartPort);
              }
          }
        }

        continue;

      wav_gapless:
        { int32_t tmpSR = 0, tmpCh = 0;
          if (getWavFormat(trk.nextPath, tmpSR, tmpCh) != 0 ||
              tmpSR != gCtl.sampleRate || tmpCh != gCtl.outChannels) {
              LOGI("WAV thread[%d]: format mismatch — stream=%dHz/%dch, next=%s (%dHz/%dch). Aborting gapless.",
                   ti, gCtl.sampleRate, gCtl.outChannels, trk.nextPath, tmpSR, tmpCh);
              abortGapless(trk, true);
              break;
          }
        }
        writeGaplessCrossfade(trk, ch);
        { int32_t res = loadWavIntoState(trk, trk.nextPath);
          if (res != 0) {
              LOGI("WAV thread[%d]: gapless: load failed %d", ti, res);
              trk.crossfading = 0;
              trk.crossfadeRemaining = 0;
          }
        }
        resetAfterGapless(trk, trk.sampleRate, trk.channels, trk.totalFrames);
        data = trk.wavData;
        dataSize = trk.wavDataSize;
        fs = trk.wavFrameSize;
        sr = trk.sampleRate;
        ch = trk.channels;
        bps = trk.bitsPerSample;
        total = trk.totalFrames;
        floatBuf.resize(blockSize * ch);
        LOGI("WAV thread[%d]: gapless: new total=%lld ch=%d sr=%d data=%p", ti, (long long)total, ch, sr, (void*)data);
        continue;
    }

    LOGI("WAV thread[%d]: loop exit wf=%lld total=%lld", ti,
         (long long)trk.writtenFrames.load(), (long long)total);

    fadeOutAndDrain(trk, ch);

    delete[] data;
    trk.wavData = nullptr;
    trk.running = 0;
    LOGI("WAV thread[%d]: finished wf=%lld/%lld", ti,
         (long long)trk.writtenFrames.load(), (long long)total);
}

// ─── FLAC Playback Thread ────────────────────────────────────────────────────

void flacPlaybackThread(int ti) {
    TrackState &trk = gCtl.tracks[ti];

    PlayState ps;
    memset(&ps, 0, sizeof(ps));
    ps.trackIndex = ti;

    FLAC__StreamDecoder *decoder = FLAC__stream_decoder_new();
    if (!decoder) {
        LOGE("FLAC thread[%d]: decoder_new failed: %s", ti, trk.path);
        trk.running = false; return;
    }

    FLAC__stream_decoder_set_metadata_respond_all(decoder);
    FLAC__StreamDecoderInitStatus st = FLAC__stream_decoder_init_file(
        decoder, trk.path, flacEngineWriteCallback, metadataCallback, errorCallback, &ps);
    if (st != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
        ProbedFormat realFmt = probeAudioFormat(trk.path);
        LOGE("FLAC thread[%d]: init_file failed (status=%d, real format=%d): %s",
             ti, st, (int)realFmt, trk.path);
        FLAC__stream_decoder_delete(decoder);
        trk.running = false; return;
    }

    FLAC__stream_decoder_process_until_end_of_metadata(decoder);
    if (ps.info.sampleRate == 0 || ps.info.channels == 0) {
        ProbedFormat realFmt = probeAudioFormat(trk.path);
        FLAC__StreamDecoderState ds = FLAC__stream_decoder_get_state(decoder);
        LOGE("FLAC thread[%d]: metadata failed (sr=%d ch=%d state=%d real format=%d): %s",
             ti, ps.info.sampleRate, ps.info.channels, ds, (int)realFmt, trk.path);
        FLAC__stream_decoder_delete(decoder);
        trk.running = false; return;
    }

    trk.sampleRate = ps.info.sampleRate;
    trk.channels = ps.info.channels;
    trk.totalFrames = ps.info.totalSamples;

    // First track sets shared output config
    bool wasNew = !gCtl.stream;
    if (!initFirstTrackStream(ps.info.sampleRate, ps.info.channels)) {
        FLAC__stream_decoder_delete(decoder);
        trk.running = 0; return;
    }
    if (wasNew) LOGI("FLAC thread[%d]: shared AAudio stream created", ti);
    // Post-stream pre-decode: track_set_next was called before stream creation,
    // predecode was skipped (outChannels=0). Now that stream exists, do it.
    if (wasNew && trk.hasNext && !trk.preBufReady && gCtl.outChannels >= 2) {
        LOGI("FLAC thread[%d]: post-stream predecode for %s", ti, trk.nextPath);
        if (!predecodeFlac(trk, trk.nextPath)) {
            LOGW("FLAC thread[%d]: post-stream predecode failed — gapless will be skipped", ti);
        }
    }
    // Post-stream pre-resample: predecodeFlac couldn't pre-resample because
    // stream didn't exist yet. Do it now.
    if (wasNew && trk.preBufReady && trk.preBufSampleRate > 0
        && trk.preBufSampleRate != gCtl.sampleRate && trk.preBufOrigFrames == 0) {
        int32_t srcSr = trk.preBufSampleRate;
        double ratio = (double)gCtl.sampleRate / srcSr;
        int32_t outFrames = (int32_t)(trk.preBufFrames * ratio);
        float *resampled = new float[outFrames * 2];
        resampleSinc(resampled, outFrames, trk.preBuf, trk.preBufFrames, 2, ratio);
        trk.preBufOrigFrames = trk.preBufFrames;
        delete[] trk.preBuf;
        trk.preBuf = resampled;
        trk.preBufFrames = outFrames;
        trk.preBufSampleRate = gCtl.sampleRate;
        LOGI("FLAC thread[%d]: post-stream pre-resampled %d→%d frames (%dHz→%dHz)",
             ti, trk.preBufOrigFrames, outFrames, srcSr, gCtl.sampleRate);
    }

    // Per-track EQ
    initTrackEq(ti, ps.info.sampleRate, ps.info.channels);

    trk.running = 1;
    trk.fadeLen.store(crossfadeMsToFrames(gCtl.crossfadeMs.load()));
    ps.stream = gCtl.stream;

    int32_t ch = ps.info.channels;
    int32_t threshold = RingBuffer::pacingThreshold(ch);

    uint64_t _stopFlac = 0;
    while (read(trk.stopFd, &_stopFlac, sizeof(_stopFlac)) <= 0) {
        _stopFlac = 0;

        // EARLY CROSSFADE: activate real-time mixing — old decoder continues, write callback
        // mixes old frames with preBuf (old fading out, new fading in) and pushes to ring buffer.
        // Old decoder reaches EOS → goto flac_gapless for decoder swap.
        if (trk.repeatCount.load() == 0 && trk.hasNext && !trk.crossfading.load()
            && ps.info.totalSamples > 0) {
            int32_t fadeLen = crossfadeMsToFrames(gCtl.crossfadeMs.load());
            int64_t remaining = ps.info.totalSamples - trk.writtenFrames;
            if (remaining > 0 && remaining <= fadeLen) {
                // Probe next file — if it's not real FLAC, can't crossfade.
                // Skip crossfade entirely and let the song play to EOS.
                // At EOS, flacGaplessPrep will handle the non-FLAC abort cleanly.
                ProbedFormat nextFmt = probeAudioFormat(trk.nextPath);
                if (nextFmt != ProbedFormat::FLAC && nextFmt != ProbedFormat::UNKNOWN) {
                    LOGI("FLAC thread[%d]: EARLY CROSSFADE skipped — next is format %d (not FLAC), playing to EOS: %s",
                         ti, (int)nextFmt, trk.nextPath);
                } else {
                    LOGI("FLAC thread[%d]: EARLY CROSSFADE trigger: remaining=%lld <= fadeLen(%d) preBufPos=%d",
                         ti, (long long)remaining, fadeLen, trk.crossfadePreBufPos);
                    trk.fadeLen.store(fadeLen);
                    trk.crossfading = 1;
                    trk.crossfadeRemaining = (int32_t)remaining;
                }
                // Do NOT reset crossfadePreBufPos — if re-triggered after applyFadeIn
                // resets crossfading, we must preserve write callback's progress.
                // Do NOT goto flac_gapless — let old decoder keep running.
                // Write callback will mix old+new frames and push to ring buffer.
            }
        }

        // Loop: seek back to beginning when track completes
        if (ps.info.totalSamples > 0 && trk.writtenFrames >= ps.info.totalSamples) {
            LOGI("FLAC thread[%d]: natural-end detected wf=%lld total=%lld loop=%d hasNext=%d",
                 ti, (long long)trk.writtenFrames.load(), (long long)ps.info.totalSamples,
                 trk.repeatCount.load(), trk.hasNext.load());
            if (trk.repeatCount.load() != 0) {
                int rc = trk.repeatCount.load();
                if (rc > 0) trk.repeatCount = rc - 1;
                FLAC__stream_decoder_seek_absolute(decoder, 0);
                trk.writtenFrames = 0;
                if (trk.ringBuf) trk.ringBuf->reset();
                if (trk.repeatCount.load() <= 0 && rc > 0) {
                    break;
                }
                continue;
            } else if (trk.hasNext) {
              flac_gapless:
                if (trk.crossfading.load()) {
                    // Crossfade cleanup: push remaining preBuf with fade-in
                    int32_t preRemaining = trk.preBufFrames - trk.crossfadePreBufPos;
                    if (preRemaining > 0 && trk.preBuf && trk.ringBuf) {
                        int32_t fadeLen = trk.fadeLen.load();
                        int32_t consumed = trk.crossfadePreBufPos;
                        std::vector<float> faded(preRemaining * trk.preBufChannels);
                        for (int32_t i = 0; i < preRemaining; i++) {
                            float t = (float)(consumed + i) / fadeLen;
                            if (t > 1.0f) t = 1.0f;
                            float fadeIn = sinf(t * HALF_PI);
                            for (int32_t c = 0; c < trk.preBufChannels; c++)
                                faded[i * trk.preBufChannels + c] =
                                    trk.preBuf[(consumed + i) * trk.preBufChannels + c] * fadeIn;
                        }
                        // Push ALL remaining faded frames — retry until ring buffer has space
                        {
                            int32_t pushed = 0;
                            while (pushed < preRemaining) {
                                int32_t n = trk.ringBuf->push(faded.data() + pushed * trk.preBufChannels,
                                                              preRemaining - pushed, trk.preBufChannels);
                                pushed += n;
                                if (pushed < preRemaining) {
                                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                                }
                            }
                        }
                        LOGI("FLAC thread[%d]: crossfade done — pushed %d remaining preBuf frames (faded)", ti, preRemaining);
                    }
                    trk.crossfading = 0;
                    trk.crossfadeRemaining = 0;
                    trk.crossfadePreBufPos = 0;
                    delete[] trk.preBuf; trk.preBuf = nullptr;
                    trk.preBufReady = 0;
                    // Don't clear preBufFrames/preBufOrigFrames — decoder swap needs them
                } else {
                    // Normal gapless: pre-decode, resample, crossfade push
                    if (!flacGaplessPrep(trk, ch, ti)) break;
                }
                // Shared decoder swap (both crossfade and normal paths converge here)
                {
                    int32_t origPreFrames = trk.preBufOrigFrames > 0 ? trk.preBufOrigFrames : trk.preBufFrames;
                    bool srMismatch = (trk.preBufOrigFrames > 0);
                    LOGI("FLAC thread[%d]: shared section — origPreFrames=%d preBufOrigFrames=%d preBufFrames=%d preBufReady=%d srMismatch=%d",
                         ti, origPreFrames, trk.preBufOrigFrames, trk.preBufFrames, trk.preBufReady.load(), srMismatch);
                    if (!flacSwapDecoder(trk, decoder, ps, ti, origPreFrames, srMismatch, ch, threshold)) {
                        trk.hasNext = 0;
                        break;
                    }
                    LOGI("FLAC thread[%d]: gapless -> %s (seek to %lld)", ti, trk.path, (long long)origPreFrames);
                    // Reset rate limit so position push succeeds immediately post-swap
                    trk.lastCallbackMs = 0;
                    // Push position immediately so Dart shows correct position post-swap
                    if (trk.sampleRate > 0) {
                        int64_t posMs = trk.writtenFrames.load() * 1000 / trk.sampleRate;
                        pushPositionToDart(ti, posMs, true, trk.lastCallbackMs, gCtl.dartPort);
                    }
                    continue;
                }
            } else break;
        }

        int64_t seek = trk.seekToFrame.exchange(-1);
        if (seek >= 0) {
            LOGI("FLAC thread[%d]: SEEK seek=%lld totalFrames=%lld totalSamples=%lld remaining=%lld hasNext=%d loop=%d",
                 ti, (long long)seek, (long long)trk.totalFrames, (long long)ps.info.totalSamples,
                 (long long)(trk.totalFrames - seek), trk.hasNext.load(), trk.repeatCount.load());
            if (seek < ps.info.totalSamples || ps.info.totalSamples == 0) {
                FLAC__stream_decoder_seek_absolute(decoder, seek);
                trk.writtenFrames = seek;
                if (trk.ringBuf) trk.ringBuf->reset();
            }
            // If seek reaches/passes end and next track is queued, trigger gapless immediately
            if (trk.totalFrames - seek <= SEEKGAP_THRESHOLD && trk.hasNext && trk.repeatCount.load() == 0) {
                LOGI("FLAC thread[%d]: seek-to-end -> forcing gapless transition", ti);
                if (!flacGaplessPrep(trk, ch, ti)) break;
                int32_t origPreFrames = trk.preBufOrigFrames > 0 ? trk.preBufOrigFrames : trk.preBufFrames;
                bool srMismatch = (trk.preBufReady && trk.preBufSampleRate != gCtl.sampleRate
                                   && trk.preBufSampleRate > 0);
                if (!flacSwapDecoder(trk, decoder, ps, ti, origPreFrames, srMismatch, ch, threshold)) {
                    trk.hasNext = 0;
                    break;
                }
                LOGI("FLAC thread[%d]: seek-gapless -> %s (seek to %lld)", ti, trk.path, (long long)origPreFrames);
            }
        }

        if (trk.paused) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        int32_t effThreshold = (trk.hasNext && trk.repeatCount.load() == 0) ? std::min(threshold, 4096) : threshold;
        if (trk.ringBuf && trk.ringBuf->available(ch) > effThreshold && !trk.skipPacing) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        if (!FLAC__stream_decoder_process_single(decoder)) {
            FLAC__StreamDecoderState errSt = FLAC__stream_decoder_get_state(decoder);
            LOGE("FLAC thread[%d]: decode error state=%d path=%s", ti, errSt, trk.path);
            pushErrorToDart(ti, "FLAC decode error", gCtl.dartPort);
            break;
        }

        // Crossfade completed — transition immediately instead of waiting for EOS.
        // applyFadeIn set crossfading=0, but trigger could re-fire if we loop back.
        // Prevent re-trigger by transitioning now.
        if (!trk.crossfading.load() && trk.crossfadePreBufPos > 0
            && trk.hasNext && trk.repeatCount.load() == 0) {
            LOGI("FLAC thread[%d]: crossfade completed — transitioning immediately (preBufPos=%d/%d)",
                 ti, trk.crossfadePreBufPos, trk.preBufFrames);
            goto flac_gapless;
        }

        if (FLAC__stream_decoder_get_state(decoder) == FLAC__STREAM_DECODER_END_OF_STREAM) {
            LOGI("FLAC thread[%d]: EOS wf=%lld total=%lld hasNext=%d loop=%d crossfading=%d cfRemaining=%d",
                 ti, (long long)trk.writtenFrames.load(), (long long)ps.info.totalSamples,
                 trk.hasNext.load(), trk.repeatCount.load(),
                 trk.crossfading.load(), trk.crossfadeRemaining.load());
            if (trk.repeatCount.load() != 0) {
                int rc = trk.repeatCount.load();
                if (rc > 0) trk.repeatCount = rc - 1;
                FLAC__stream_decoder_seek_absolute(decoder, 0);
                trk.writtenFrames = 0;
                if (trk.ringBuf) trk.ringBuf->reset();
                if (trk.repeatCount.load() <= 0 && rc > 0) {
                    break;
                }
                continue;
            }
            if (trk.hasNext && trk.repeatCount.load() == 0) {
                LOGI("FLAC thread[%d]: EOS -> goto flac_gapless", ti);
                goto flac_gapless;
            }
            LOGI("FLAC thread[%d]: EOS -> break (no transition)", ti);
            break;
        }
    }

    fadeOutAndDrain(trk, ch);

    if (decoder) FLAC__stream_decoder_finish(decoder);
    FLAC__stream_decoder_delete(decoder);
    trk.running = false;
    LOGI("FLAC thread[%d]: finished", ti);
}

// ─── Media Playback Thread (AMediaCodec, local file) ─────────────────────────

void mediaPlaybackThread(int ti) {
    TrackState &trk = gCtl.tracks[ti];

    int fd = open(trk.path, O_RDONLY);
    if (fd < 0) { trk.running = false; return; }

    AMediaExtractor *extractor = AMediaExtractor_new();
    off64_t fileLen = lseek64(fd, 0, SEEK_END);
    lseek64(fd, 0, SEEK_SET);
    if (AMediaExtractor_setDataSourceFd(extractor, fd, 0, fileLen) != AMEDIA_OK) {
        AMediaExtractor_delete(extractor); close(fd); trk.running = false; return;
    }

    int32_t sr = 0, ch = 0;
    int64_t durationUs = 0;
    int32_t audioTrack = findAudioTrack(extractor, sr, ch, durationUs);
    if (audioTrack < 0) { AMediaExtractor_delete(extractor); close(fd); trk.running = false; return; }

    AMediaFormat *trackFmt = AMediaExtractor_getTrackFormat(extractor, audioTrack);
    const char *mime = NULL;
    AMediaFormat_getString(trackFmt, AMEDIAFORMAT_KEY_MIME, &mime);
    AMediaCodec *codec = createCodecFromExtractor(extractor, audioTrack, mime, trackFmt);
    if (!codec) { AMediaExtractor_delete(extractor); close(fd); trk.running = false; return; }

    trk.sampleRate = sr;
    trk.channels = ch;
    trk.totalFrames = (durationUs > 0 && sr > 0) ? (durationUs * sr / 1000000) : 0;
    trk.writtenFrames = 0;

    // First track sets shared output config
    bool wasNew = !gCtl.stream;
    if (!initFirstTrackStream(sr, ch)) {
        AMediaCodec_stop(codec); AMediaCodec_delete(codec);
        AMediaExtractor_delete(extractor); close(fd);
        trk.running = 0; return;
    }
    if (wasNew) LOGI("Media thread[%d]: shared AAudio stream created (sr=%d ch=%d)", ti, sr, ch);

    // Per-track EQ
    initTrackEq(ti, sr, ch);

    trk.running = 1;
    trk.fadeLen.store(crossfadeMsToFrames(gCtl.crossfadeMs.load()));

    bool inputDone = false, outputDone = false;
    int32_t outCh = ch;
    int32_t threshold = RingBuffer::pacingThreshold(ch);

    // Pre-alloc decode buffer — reused every output buffer (avoids heap alloc per codec output)
    std::vector<float> decodeBuf(4096 * ch);

    uint64_t _stopMedia = 0;
    while (read(trk.stopFd, &_stopMedia, sizeof(_stopMedia)) <= 0) {
        _stopMedia = 0;
        int rc = trk.repeatCount.load();
        int64_t seek = trk.seekToFrame.exchange(-1);
        if (seek >= 0 && sr > 0) {
            int64_t seekUs = seek * 1000000 / sr;
            AMediaExtractor_seekTo(extractor, seekUs, AMEDIAEXTRACTOR_SEEK_CLOSEST_SYNC);
            AMediaCodec_flush(codec);
            inputDone = false;
            outputDone = false;
            trk.writtenFrames = seek;
            if (trk.ringBuf) trk.ringBuf->reset();
            // If seek reaches/passes end and next track is queued, force gapless
            if (trk.totalFrames - seek <= SEEKGAP_THRESHOLD && trk.hasNext && trk.repeatCount.load() == 0 && trk.totalFrames > 0) {
                LOGI("Media thread[%d]: seek-to-end -> forcing gapless transition", ti);
                goto media_force_gapless;
            }
        }

        // Loop: seek back to beginning when track completes
        if (outputDone && !trk.paused) {
            if (rc != 0) {
                if (rc > 0) trk.repeatCount = rc - 1;
                AMediaExtractor_seekTo(extractor, 0, AMEDIAEXTRACTOR_SEEK_CLOSEST_SYNC);
                AMediaCodec_flush(codec);
                inputDone = false;
                outputDone = false;
                trk.writtenFrames = 0;
                if (trk.ringBuf) trk.ringBuf->reset();
                if (trk.repeatCount.load() <= 0 && rc > 0) {
                    break;
                }
                continue;
            } else if (trk.hasNext) {
              media_force_gapless:
                // CRITICAL: If next track is real FLAC, abort MediaCodec cascade.
                // The FLAC decoder provides true crossfade via flacEngineWriteCallback.
                // MediaCodec gapless would trap the FLAC in AMediaCodec, losing crossfade forever.
                {
                    ProbedFormat nextFmt = probeAudioFormat(trk.nextPath);
                    if (nextFmt == ProbedFormat::FLAC) {
                        LOGI("Media thread[%d]: next is real FLAC — aborting MediaCodec cascade to restore crossfade: %s",
                             ti, trk.nextPath);
                        // Quick fade-out (50ms) for smooth transition
                        if (trk.ringBuf && trk.writtenFrames > 0 && outCh > 0) {
                            int32_t fadeFrames = std::min((int32_t)(gCtl.sampleRate * 50 / 1000), trk.ringBuf->available(outCh));
                            if (fadeFrames > 0) {
                                std::vector<float> fadeBuf(fadeFrames * outCh);
                                for (int32_t i = 0; i < fadeFrames; i++) {
                                    float g = sinf((1.0f - (float)i / fadeFrames) * HALF_PI);
                                    for (int c = 0; c < outCh; c++)
                                        fadeBuf[i * outCh + c] = trk.lastFrame[c < 2 ? c : 0] * g;
                                }
                                trk.ringBuf->push(fadeBuf.data(), fadeFrames, outCh);
                            }
                        }
                        abortGapless(trk, true);
                        break;
                    }
                }
                // Pre-check: validate new file format before destroying old resources
                {
                    int tmpFd = open(trk.nextPath, O_RDONLY);
                    if (tmpFd < 0) {
                        LOGI("Media thread[%d]: gapless: cannot open %s", ti, trk.nextPath);
                        trk.hasNext = 0;
                        trk.nextPath[0] = '\0';
                        break;
                    }
                    AMediaExtractor *tmpExt = AMediaExtractor_new();
                    off64_t tmpLen = lseek64(tmpFd, 0, SEEK_END);
                    lseek64(tmpFd, 0, SEEK_SET);
                    if (AMediaExtractor_setDataSourceFd(tmpExt, tmpFd, 0, tmpLen) != AMEDIA_OK) {
                        LOGI("Media thread[%d]: gapless: setDataSource failed for %s", ti, trk.nextPath);
                        AMediaExtractor_delete(tmpExt); close(tmpFd);
                        trk.hasNext = 0;
                        trk.nextPath[0] = '\0';
                        break;
                    }
                    int32_t tmpSR = 0, tmpCh = 0;
                    bool fmtOk = checkMediaExtractorFormat(tmpExt, gCtl.sampleRate, gCtl.outChannels, tmpSR, tmpCh);
                    AMediaExtractor_delete(tmpExt); close(tmpFd);
                    if (!fmtOk) {
                        LOGI("Media thread[%d]: format mismatch — stream=%dHz/%dch, next=%s (%dHz/%dch). Aborting gapless.",
                             ti, gCtl.sampleRate, gCtl.outChannels, trk.nextPath, tmpSR, tmpCh);
                        abortGapless(trk, true);
                        break;
                    }
                }
                { int32_t _preFrames = writeGaplessCrossfade(trk, outCh);
                AMediaCodec_stop(codec); AMediaCodec_delete(codec);
                AMediaExtractor_delete(extractor); close(fd);
                fd = open(trk.nextPath, O_RDONLY);
                if (fd < 0) { trk.hasNext = 0; break; }
                extractor = AMediaExtractor_new();
                off64_t fileLen = lseek64(fd, 0, SEEK_END);
                lseek64(fd, 0, SEEK_SET);
                if (AMediaExtractor_setDataSourceFd(extractor, fd, 0, fileLen) != AMEDIA_OK) {
                    AMediaExtractor_delete(extractor); close(fd); trk.hasNext = 0; break;
                }
                int64_t durUs = 0;
                int32_t aT = findAudioTrack(extractor, sr, ch, durUs);
                if (aT < 0) { AMediaExtractor_delete(extractor); close(fd); trk.hasNext = 0; break; }
                AMediaFormat *trackFmt = AMediaExtractor_getTrackFormat(extractor, aT);
                const char *mime = NULL;
                AMediaFormat_getString(trackFmt, AMEDIAFORMAT_KEY_MIME, &mime);
                codec = createCodecFromExtractor(extractor, aT, mime, trackFmt);
                if (!codec) { AMediaExtractor_delete(extractor); close(fd); trk.hasNext = 0; break; }
                trk.sampleRate = sr; trk.channels = ch;
                trk.totalFrames = (durUs > 0 && sr > 0) ? (durUs * sr / 1000000) : 0;
                trk.writtenFrames = 0;
                outCh = ch; threshold = RingBuffer::pacingThreshold(ch);
                inputDone = false; outputDone = false;
                resetAfterGapless(trk, sr, ch, trk.totalFrames);
                // Push position 0 immediately so Dart shows correct position post-swap
                // (mirrors FLAC thread behavior at line 646-650)
                trk.lastCallbackMs = 0;
                if (trk.sampleRate > 0) {
                    pushPositionToDart(ti, 0, true, trk.lastCallbackMs, gCtl.dartPort);
                }
                LOGI("Media thread[%d]: gapless -> %s (pos pushed 0ms)", ti, trk.path);
                continue; }
            } else break;
        }

        int32_t effThreshold = (trk.hasNext && trk.repeatCount.load() == 0) ? std::min(threshold, 4096) : threshold;
        if (trk.ringBuf && trk.ringBuf->available(outCh) > effThreshold && !trk.paused && !trk.skipPacing) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        if (!inputDone && !trk.paused) {
            ssize_t inIdx = AMediaCodec_dequeueInputBuffer(codec, 10000);
            if (inIdx >= 0) {
                size_t inSize;
                uint8_t *inBuf = AMediaCodec_getInputBuffer(codec, inIdx, &inSize);
                if (inBuf) {
                    ssize_t sampleSize = AMediaExtractor_readSampleData(extractor, inBuf, inSize);
                    if (sampleSize < 0) {
                        AMediaCodec_queueInputBuffer(codec, inIdx, 0, 0, 0, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                        inputDone = true;
                    } else {
                        AMediaCodec_queueInputBuffer(codec, inIdx, 0, sampleSize, AMediaExtractor_getSampleTime(extractor), 0);
                        AMediaExtractor_advance(extractor);
                    }
                }
            }
        }

        AMediaCodecBufferInfo info;
        ssize_t outIdx = AMediaCodec_dequeueOutputBuffer(codec, &info, 10000);

        if (outIdx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            AMediaFormat *newFmt = AMediaCodec_getOutputFormat(codec);
            AMediaFormat_getInt32(newFmt, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &outCh);
            AMediaFormat_delete(newFmt);
            threshold = RingBuffer::pacingThreshold(outCh);
            continue;
        }

        if (outIdx >= 0) {
            bool eos = info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM;
            if (eos) outputDone = true;

            {
                int32_t frames = processCodecOutput(trk, ti, outCh, codec, outIdx, info, decodeBuf);
                if (frames > 0) {
                    trk.writtenFrames += frames;
                    pushPositionToDart(ti, trk.writtenFrames.load() * 1000 / gCtl.sampleRate, true, trk.lastCallbackMs, gCtl.dartPort);
                }
            }
            AMediaCodec_releaseOutputBuffer(codec, outIdx, false);

            if (trk.paused && inputDone && eos) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        } else if (outIdx == AMEDIACODEC_INFO_TRY_AGAIN_LATER ||
                   outIdx == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
            if (trk.paused && inputDone) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        } else {
            LOGE("Media thread[%d]: codec error %zd", ti, outIdx);
            pushErrorToDart(ti, "codec error", gCtl.dartPort);
            break;
        }
    }

    fadeOutAndDrain(trk, outCh);

    AMediaCodec_stop(codec); AMediaCodec_delete(codec);
    AMediaExtractor_delete(extractor); close(fd);
    trk.running = false;
    LOGI("Media thread[%d]: finished", ti);
}

// ─── Media Streaming Thread (URL-based AMediaExtractor) ────────────────────

void mediaStreamPlaybackThread(int ti) {
    TrackState &trk = gCtl.tracks[ti];

    AMediaExtractor *extractor = AMediaExtractor_new();
    media_status_t setStatus = AMediaExtractor_setDataSource(extractor, trk.path);
    if (setStatus != AMEDIA_OK) {
        AMediaExtractor_delete(extractor);
        LOGE("Media stream[%d]: setDataSource failed: status=%d", ti, setStatus);
        trk.running = false; return;
    }

    int32_t audioTrack = -1, sr = 0, ch = 0;
    int64_t durationUs = 0;
    audioTrack = findAudioTrack(extractor, sr, ch, durationUs);
    if (audioTrack < 0) { AMediaExtractor_delete(extractor); trk.running = false; return; }

    AMediaFormat *trackFmt = AMediaExtractor_getTrackFormat(extractor, audioTrack);
    const char *mime = NULL;
    AMediaFormat_getString(trackFmt, AMEDIAFORMAT_KEY_MIME, &mime);

    AMediaCodec *codec = createCodecFromExtractor(extractor, audioTrack, mime, trackFmt);
    if (!codec) { AMediaExtractor_delete(extractor); trk.running = false; return; }

    trk.sampleRate = sr;
    trk.channels = ch;
    trk.totalFrames = (durationUs > 0 && sr > 0) ? (durationUs * sr / 1000000) : 0;
    trk.writtenFrames = 0;

    // First track sets shared output config
    bool wasNew = !gCtl.stream;
    if (!initFirstTrackStream(sr, ch)) {
        AMediaCodec_stop(codec); AMediaCodec_delete(codec);
        AMediaExtractor_delete(extractor);
        trk.running = 0; return;
    }
    if (wasNew) LOGI("Media stream[%d]: shared AAudio stream created (sr=%d ch=%d)", ti, sr, ch);

    // Per-track EQ
    initTrackEq(ti, sr, ch);

    trk.running = 1;
    trk.fadeLen.store(crossfadeMsToFrames(gCtl.crossfadeMs.load()));

    bool inputDone = false, outputDone = false;
    int32_t outCh = ch;
    int32_t threshold = RingBuffer::pacingThreshold(ch);

    // Pre-alloc decode buffer — reused every output buffer (avoids heap alloc per codec output)
    std::vector<float> decodeBuf(4096 * ch);

    LOGI("Media stream[%d]: started sr=%d ch=%d path=%s", ti, sr, ch, trk.path);

    uint64_t _stopVal = 0;
    while (read(trk.stopFd, &_stopVal, sizeof(_stopVal)) <= 0) {
        _stopVal = 0;
        int rc = trk.repeatCount.load();
        int64_t seek = trk.seekToFrame.exchange(-1);
        if (seek >= 0 && sr > 0 && trk.totalFrames > 0) {
            int64_t seekUs = seek * 1000000 / sr;
            AMediaExtractor_seekTo(extractor, seekUs, AMEDIAEXTRACTOR_SEEK_CLOSEST_SYNC);
            AMediaCodec_flush(codec);
            inputDone = false;
            outputDone = false;
            trk.writtenFrames = seek;
            if (trk.ringBuf) trk.ringBuf->reset();
            // If seek reaches/passes end and next track is queued, force gapless
            if (trk.totalFrames - seek <= SEEKGAP_THRESHOLD && trk.hasNext && trk.repeatCount.load() == 0 && trk.totalFrames > 0) {
                LOGI("Media stream[%d]: seek-to-end -> forcing gapless transition", ti);
                goto stream_force_gapless;
            }
        }

        // Loop: seek back to beginning when track completes
        if (outputDone && !trk.paused) {
            if (rc != 0) {
                if (rc > 0) trk.repeatCount = rc - 1;
                AMediaExtractor_seekTo(extractor, 0, AMEDIAEXTRACTOR_SEEK_CLOSEST_SYNC);
                AMediaCodec_flush(codec);
                inputDone = false;
                outputDone = false;
                trk.writtenFrames = 0;
                if (trk.ringBuf) trk.ringBuf->reset();
                if (trk.repeatCount.load() <= 0 && rc > 0) {
                    break;
                }
                continue;
            } else if (trk.hasNext) {
              stream_force_gapless:
                // Pre-check: validate new file format before destroying old resources
                {
                    AMediaExtractor *tmpExt = AMediaExtractor_new();
                    media_status_t tmpSt = AMediaExtractor_setDataSource(tmpExt, trk.nextPath);
                    if (tmpSt != AMEDIA_OK) {
                        LOGI("Media stream[%d]: gapless: setDataSource failed for %s", ti, trk.nextPath);
                        AMediaExtractor_delete(tmpExt);
                        trk.hasNext = 0;
                        trk.nextPath[0] = '\0';
                        break;
                    }
                    int32_t tmpSR = 0, tmpCh = 0;
                    bool fmtOk = checkMediaExtractorFormat(tmpExt, gCtl.sampleRate, gCtl.outChannels, tmpSR, tmpCh);
                    AMediaExtractor_delete(tmpExt);
                    if (!fmtOk) {
                        LOGI("Media stream[%d]: format mismatch — stream=%dHz/%dch, next=%s (%dHz/%dch). Aborting gapless.",
                             ti, gCtl.sampleRate, gCtl.outChannels, trk.nextPath, tmpSR, tmpCh);
                        abortGapless(trk, true);
                        break;
                    }
                }
                { int32_t _preFrames = writeGaplessCrossfade(trk, outCh);
                AMediaCodec_stop(codec); AMediaCodec_delete(codec);
                AMediaExtractor_delete(extractor);
                extractor = AMediaExtractor_new();
                media_status_t setSt = AMediaExtractor_setDataSource(extractor, trk.nextPath);
                if (setSt != AMEDIA_OK) {
                    AMediaExtractor_delete(extractor); trk.hasNext = 0; break;
                }
                int32_t aT = -1; sr = 0; ch = 0; int64_t durUs = 0;
                aT = findAudioTrack(extractor, sr, ch, durUs);
                if (aT < 0) { AMediaExtractor_delete(extractor); trk.hasNext = 0; break; }
                AMediaFormat *trackFmt = AMediaExtractor_getTrackFormat(extractor, aT);
                const char *mime = NULL;
                AMediaFormat_getString(trackFmt, AMEDIAFORMAT_KEY_MIME, &mime);
                codec = createCodecFromExtractor(extractor, aT, mime, trackFmt);
                if (!codec) { AMediaExtractor_delete(extractor); trk.hasNext = 0; break; }
                trk.sampleRate = sr; trk.channels = ch;
                trk.totalFrames = (durUs > 0 && sr > 0) ? (durUs * sr / 1000000) : 0;
                trk.writtenFrames = 0;
                outCh = ch; threshold = RingBuffer::pacingThreshold(ch);
                inputDone = false; outputDone = false;
                resetAfterGapless(trk, sr, ch, trk.totalFrames);
                LOGI("Media stream[%d]: gapless -> %s", ti, trk.path);
                continue; }
            } else break;
        }

        int32_t effThreshold = (trk.hasNext && trk.repeatCount.load() == 0) ? std::min(threshold, 4096) : threshold;
        if (trk.ringBuf && trk.ringBuf->available(outCh) > effThreshold && !trk.paused && !trk.skipPacing) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        if (!inputDone && !trk.paused) {
            ssize_t inIdx = AMediaCodec_dequeueInputBuffer(codec, 10000);
            if (inIdx >= 0) {
                size_t inSize;
                uint8_t *inBuf = AMediaCodec_getInputBuffer(codec, inIdx, &inSize);
                if (inBuf) {
                    ssize_t sampleSize = AMediaExtractor_readSampleData(extractor, inBuf, inSize);
                    if (sampleSize < 0) {
                        AMediaCodec_queueInputBuffer(codec, inIdx, 0, 0, 0, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                        inputDone = true;
                    } else {
                        int64_t sampleTime = AMediaExtractor_getSampleTime(extractor);
                        AMediaCodec_queueInputBuffer(codec, inIdx, 0, sampleSize, sampleTime, 0);
                        AMediaExtractor_advance(extractor);
                        if (trk.totalFrames > 0 && sr > 0 && sampleTime > 0) {
                            trk.writtenFrames = sampleTime * sr / 1000000;
                            pushPositionToDart(ti, trk.writtenFrames.load() * 1000 / sr, true, trk.lastCallbackMs, gCtl.dartPort);
                        }
                    }
                }
            }
        }

        AMediaCodecBufferInfo info;
        ssize_t outIdx = AMediaCodec_dequeueOutputBuffer(codec, &info, 10000);

        if (outIdx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            AMediaFormat *newFmt = AMediaCodec_getOutputFormat(codec);
            AMediaFormat_getInt32(newFmt, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &outCh);
            AMediaFormat_delete(newFmt);
            threshold = RingBuffer::pacingThreshold(outCh);
            continue;
        }

        if (outIdx >= 0) {
            bool eos = info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM;
            if (eos) outputDone = true;

            processCodecOutput(trk, ti, outCh, codec, outIdx, info, decodeBuf);
            AMediaCodec_releaseOutputBuffer(codec, outIdx, false);

            if (trk.paused && inputDone && eos) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        } else if (outIdx == AMEDIACODEC_INFO_TRY_AGAIN_LATER ||
                   outIdx == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
            if (trk.paused && inputDone) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        } else {
            LOGE("Media stream[%d]: codec error %zd", ti, outIdx);
            pushErrorToDart(ti, "codec error", gCtl.dartPort);
            break;
        }
    }

    fadeOutAndDrain(trk, outCh);

    AMediaCodec_stop(codec); AMediaCodec_delete(codec);
    AMediaExtractor_delete(extractor);
    trk.running = false;
    LOGI("Media stream[%d]: finished", ti);
}
