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
#include "common.h"
#include "dsp_processor.h"

#include <cstdio>
#include <cstring>
#include <thread>
#include <chrono>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <aaudio/AAudio.h>
#include <errno.h>
#include <FLAC/stream_decoder.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>

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
    if (!gCtl.stream && sr > 0 && ch > 0) {
        gCtl.outChannels = ch;
        if (!gCtl.dsp) gCtl.dsp = new DspProcessor();
        gCtl.dsp->init(sr, ch);
        gCtl.stream = createAAudioStreamCallback(sr, ch, aaudioDataCallback, nullptr);
        if (!gCtl.stream) {
            LOGE("WAV thread[%d]: createAAudioStreamCallback failed", ti);
            delete[] data; trk.wavData = nullptr; trk.running = 0; return;
        }
        gCtl.sampleRate = sr;
        LOGI("WAV thread[%d]: shared AAudio stream created (sr=%d ch=%d)", ti, sr, ch);
    }

    trk.running = 1;
    int32_t blockSize = 4096;
    int32_t threshold = RingBuffer::pacingThreshold(ch);

    uint64_t _st = 0;
    read(trk.stopFd, &_st, sizeof(_st)); // Drain phantom data

    while (trk.writtenFrames.load() < total) {
        _st = 0;
        if (read(trk.stopFd, &_st, sizeof(_st)) > 0) {
            LOGI("WAV thread[%d]: got stop signal", ti); break;
        }
        if (trk.paused) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        int64_t seek = trk.seekToFrame.exchange(-1);
        if (seek >= 0) {
            trk.writtenFrames = seek < total ? seek : total;
            if (trk.ringBuf) trk.ringBuf->reset();
        }

        if (trk.ringBuf && trk.ringBuf->available(ch) > threshold) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        int32_t rem = (int32_t)(total - trk.writtenFrames);
        int32_t chunk = rem < blockSize ? rem : blockSize;
        int32_t base = (int32_t)trk.writtenFrames;

        float floatBuf[chunk * ch];
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
            int32_t pushed = trk.ringBuf->push(floatBuf, chunk, ch);
            trk.writtenFrames += pushed;
        }
        if (trk.pcmRingBuf) {
            trk.pcmRingBuf->push(floatBuf, chunk, ch);
        }
    }

    LOGI("WAV thread[%d]: loop exit wf=%lld total=%lld", ti,
         (long long)trk.writtenFrames.load(), (long long)total);

    // Drain ring buffer
    if (trk.writtenFrames >= total && trk.ringBuf) {
        while (trk.ringBuf->available(ch) > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

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
    if (!decoder) { trk.running = false; return; }

    FLAC__stream_decoder_set_metadata_respond_all(decoder);
    FLAC__StreamDecoderInitStatus st = FLAC__stream_decoder_init_file(
        decoder, trk.path, flacEngineWriteCallback, metadataCallback, errorCallback, &ps);
    if (st != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
        FLAC__stream_decoder_delete(decoder);
        trk.running = false; return;
    }

    FLAC__stream_decoder_process_until_end_of_metadata(decoder);
    if (ps.info.sampleRate == 0 || ps.info.channels == 0) {
        FLAC__stream_decoder_delete(decoder);
        trk.running = false; return;
    }

    trk.sampleRate = ps.info.sampleRate;
    trk.channels = ps.info.channels;
    trk.totalFrames = ps.info.totalSamples;

    // First track sets shared output config
    if (!gCtl.stream && ps.info.sampleRate > 0 && ps.info.channels > 0) {
        gCtl.outChannels = ps.info.channels;
        if (!gCtl.dsp) gCtl.dsp = new DspProcessor();
        gCtl.dsp->init(ps.info.sampleRate, ps.info.channels);
        gCtl.stream = createAAudioStreamCallback(
            ps.info.sampleRate, ps.info.channels, aaudioDataCallback, nullptr);
        if (!gCtl.stream) {
            FLAC__stream_decoder_delete(decoder);
            trk.running = 0; return;
        }
        gCtl.sampleRate = ps.info.sampleRate;
        LOGI("FLAC thread[%d]: shared AAudio stream created", ti);
    }

    trk.running = 1;
    ps.stream = gCtl.stream;

    int32_t ch = ps.info.channels;
    int32_t threshold = RingBuffer::pacingThreshold(ch);

    uint64_t _stopFlac = 0;
    while (read(trk.stopFd, &_stopFlac, sizeof(_stopFlac)) <= 0) {
        _stopFlac = 0;
        if (ps.info.totalSamples > 0 && trk.writtenFrames >= ps.info.totalSamples)
            break;

        int64_t seek = trk.seekToFrame.exchange(-1);
        if (seek >= 0) {
            if (seek < ps.info.totalSamples || ps.info.totalSamples == 0) {
                FLAC__stream_decoder_seek_absolute(decoder, seek);
                trk.writtenFrames = seek;
                if (trk.ringBuf) trk.ringBuf->reset();
            }
        }

        if (trk.paused) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        if (trk.ringBuf && trk.ringBuf->available(ch) > threshold) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        if (!FLAC__stream_decoder_process_single(decoder)) break;
        if (FLAC__stream_decoder_get_state(decoder) == FLAC__STREAM_DECODER_END_OF_STREAM) break;
    }

    bool finished = (ps.info.totalSamples > 0 && trk.writtenFrames >= ps.info.totalSamples);
    if (finished && trk.ringBuf) {
        while (trk.ringBuf->available(ch) > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    FLAC__stream_decoder_finish(decoder);
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

    int32_t audioTrack = -1, sr = 0, ch = 0;
    int64_t durationUs = 0;
    for (int32_t i = 0; i < AMediaExtractor_getTrackCount(extractor); i++) {
        AMediaFormat *fmt = AMediaExtractor_getTrackFormat(extractor, i);
        const char *m = NULL;
        AMediaFormat_getString(fmt, AMEDIAFORMAT_KEY_MIME, &m);
        if (m && strncmp(m, "audio/", 6) == 0) {
            audioTrack = i;
            AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_SAMPLE_RATE, &sr);
            AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &ch);
            AMediaFormat_getInt64(fmt, AMEDIAFORMAT_KEY_DURATION, &durationUs);
            AMediaFormat_delete(fmt); break;
        }
        AMediaFormat_delete(fmt);
    }
    if (audioTrack < 0) { AMediaExtractor_delete(extractor); close(fd); trk.running = false; return; }

    AMediaFormat *trackFmt = AMediaExtractor_getTrackFormat(extractor, audioTrack);
    const char *mime = NULL;
    AMediaFormat_getString(trackFmt, AMEDIAFORMAT_KEY_MIME, &mime);
    AMediaExtractor_selectTrack(extractor, audioTrack);

    AMediaCodec *codec = AMediaCodec_createDecoderByType(mime);
    if (!codec || AMediaCodec_configure(codec, trackFmt, NULL, NULL, 0) != AMEDIA_OK ||
        AMediaCodec_start(codec) != AMEDIA_OK) {
        if (codec) AMediaCodec_delete(codec);
        AMediaFormat_delete(trackFmt); AMediaExtractor_delete(extractor); close(fd);
        trk.running = false; return;
    }
    AMediaFormat_delete(trackFmt);

    trk.sampleRate = sr;
    trk.channels = ch;
    trk.totalFrames = (durationUs > 0 && sr > 0) ? (durationUs * sr / 1000000) : 0;
    trk.writtenFrames = 0;

    // First track sets shared output config
    if (!gCtl.stream && sr > 0 && ch > 0) {
        gCtl.outChannels = ch;
        if (!gCtl.dsp) gCtl.dsp = new DspProcessor();
        gCtl.dsp->init(sr, ch);
        gCtl.stream = createAAudioStreamCallback(sr, ch, aaudioDataCallback, nullptr);
        if (!gCtl.stream) {
            AMediaCodec_stop(codec); AMediaCodec_delete(codec);
            AMediaExtractor_delete(extractor); close(fd);
            trk.running = 0; return;
        }
        gCtl.sampleRate = sr;
        LOGI("Media thread[%d]: shared AAudio stream created (sr=%d ch=%d)", ti, sr, ch);
    }

    trk.running = 1;

    bool inputDone = false, outputDone = false;
    int32_t outCh = ch;
    int32_t threshold = RingBuffer::pacingThreshold(ch);

    uint64_t _stopMedia = 0;
    while (read(trk.stopFd, &_stopMedia, sizeof(_stopMedia)) <= 0) {
        _stopMedia = 0;
        int64_t seek = trk.seekToFrame.exchange(-1);
        if (seek >= 0 && sr > 0) {
            int64_t seekUs = seek * 1000000 / sr;
            AMediaExtractor_seekTo(extractor, seekUs, AMEDIAEXTRACTOR_SEEK_CLOSEST_SYNC);
            AMediaCodec_flush(codec);
            inputDone = false;
            outputDone = false;
            trk.writtenFrames = seek;
            if (trk.ringBuf) trk.ringBuf->reset();
        }

        if (outputDone && !trk.paused) break;

        if (trk.ringBuf && trk.ringBuf->available(outCh) > threshold && !trk.paused) {
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
            continue;
        }

        if (outIdx >= 0) {
            bool eos = info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM;
            if (eos) outputDone = true;

            if (info.size > 0 && gCtl.stream && !trk.paused) {
                size_t outSize;
                uint8_t *outBuf = AMediaCodec_getOutputBuffer(codec, outIdx, &outSize);
                if (outBuf) {
                    outBuf += info.offset;
                    int32_t totalS = info.size / 2;
                    int32_t frames = totalS / outCh;
                    float *fb = new float[totalS];
                    for (int32_t i = 0; i < totalS; i++) {
                        int16_t vs = outBuf[i*2] | (outBuf[i*2+1]<<8);
                        fb[i] = vs / 32768.0f;
                    }
                    if (trk.ringBuf) {
                        trk.ringBuf->push(fb, frames, outCh);
                    }
                    if (trk.pcmRingBuf) {
                        trk.pcmRingBuf->push(fb, frames, outCh);
                    }
                    delete[] fb;
                    trk.writtenFrames += frames;
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
            break;
        }
    }

    // Drain ring buffer
    if (outputDone && trk.ringBuf) {
        while (trk.running && trk.ringBuf->available(outCh) > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

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
    for (int32_t i = 0; i < AMediaExtractor_getTrackCount(extractor); i++) {
        AMediaFormat *fmt = AMediaExtractor_getTrackFormat(extractor, i);
        const char *m = NULL;
        AMediaFormat_getString(fmt, AMEDIAFORMAT_KEY_MIME, &m);
        if (m && strncmp(m, "audio/", 6) == 0) {
            audioTrack = i;
            AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_SAMPLE_RATE, &sr);
            AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &ch);
            AMediaFormat_getInt64(fmt, AMEDIAFORMAT_KEY_DURATION, &durationUs);
            AMediaFormat_delete(fmt); break;
        }
        AMediaFormat_delete(fmt);
    }
    if (audioTrack < 0) { AMediaExtractor_delete(extractor); trk.running = false; return; }

    AMediaFormat *trackFmt = AMediaExtractor_getTrackFormat(extractor, audioTrack);
    const char *mime = NULL;
    AMediaFormat_getString(trackFmt, AMEDIAFORMAT_KEY_MIME, &mime);
    AMediaExtractor_selectTrack(extractor, audioTrack);

    AMediaCodec *codec = AMediaCodec_createDecoderByType(mime);
    if (!codec || AMediaCodec_configure(codec, trackFmt, NULL, NULL, 0) != AMEDIA_OK ||
        AMediaCodec_start(codec) != AMEDIA_OK) {
        if (codec) AMediaCodec_delete(codec);
        AMediaFormat_delete(trackFmt); AMediaExtractor_delete(extractor);
        trk.running = false; return;
    }
    AMediaFormat_delete(trackFmt);

    trk.sampleRate = sr;
    trk.channels = ch;
    trk.totalFrames = (durationUs > 0 && sr > 0) ? (durationUs * sr / 1000000) : 0;
    trk.writtenFrames = 0;

    // First track sets shared output config
    if (!gCtl.stream && sr > 0 && ch > 0) {
        gCtl.outChannels = ch;
        if (!gCtl.dsp) gCtl.dsp = new DspProcessor();
        gCtl.dsp->init(sr, ch);
        gCtl.stream = createAAudioStreamCallback(sr, ch, aaudioDataCallback, nullptr);
        if (!gCtl.stream) {
            AMediaCodec_stop(codec); AMediaCodec_delete(codec);
            AMediaExtractor_delete(extractor);
            trk.running = 0; return;
        }
        gCtl.sampleRate = sr;
        LOGI("Media stream[%d]: shared AAudio stream created (sr=%d ch=%d)", ti, sr, ch);
    }

    trk.running = 1;

    bool inputDone = false, outputDone = false;
    int32_t outCh = ch;
    int32_t threshold = RingBuffer::pacingThreshold(ch);

    LOGI("Media stream[%d]: started sr=%d ch=%d path=%s", ti, sr, ch, trk.path);

    uint64_t _stopVal = 0;
    while (read(trk.stopFd, &_stopVal, sizeof(_stopVal)) <= 0) {
        _stopVal = 0;
        int64_t seek = trk.seekToFrame.exchange(-1);
        if (seek >= 0 && sr > 0 && trk.totalFrames > 0) {
            int64_t seekUs = seek * 1000000 / sr;
            AMediaExtractor_seekTo(extractor, seekUs, AMEDIAEXTRACTOR_SEEK_CLOSEST_SYNC);
            AMediaCodec_flush(codec);
            inputDone = false;
            outputDone = false;
            trk.writtenFrames = seek;
            if (trk.ringBuf) trk.ringBuf->reset();
        }

        if (outputDone && !trk.paused) break;

        if (trk.ringBuf && trk.ringBuf->available(outCh) > threshold && !trk.paused) {
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
            continue;
        }

        if (outIdx >= 0) {
            bool eos = info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM;
            if (eos) outputDone = true;

            if (info.size > 0 && gCtl.stream && !trk.paused) {
                size_t outSize;
                uint8_t *outBuf = AMediaCodec_getOutputBuffer(codec, outIdx, &outSize);
                if (outBuf) {
                    outBuf += info.offset;
                    int32_t totalS = info.size / 2;
                    int32_t frames = totalS / outCh;
                    float *fb = new float[totalS];
                    for (int32_t i = 0; i < totalS; i++) {
                        int16_t vs = outBuf[i*2] | (outBuf[i*2+1]<<8);
                        fb[i] = vs / 32768.0f;
                    }
                    if (trk.ringBuf) {
                        trk.ringBuf->push(fb, frames, outCh);
                    }
                    if (trk.pcmRingBuf) {
                        trk.pcmRingBuf->push(fb, frames, outCh);
                    }
                    delete[] fb;
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
            break;
        }
    }

    if (outputDone && trk.ringBuf) {
        while (trk.running && trk.ringBuf->available(outCh) > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    AMediaCodec_stop(codec); AMediaCodec_delete(codec);
    AMediaExtractor_delete(extractor);
    trk.running = false;
    LOGI("Media stream[%d]: finished", ti);
}
