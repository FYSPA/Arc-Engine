// ---------------------------------------------------------------------------
// File: audio_engine.cpp
// Purpose: AAudio data callback (high-priority audio thread) that sums all
//          active tracks with per-track volume + constant-power pan + master
//          volume + DSP EQ. Also exports legacy single-track FFI controls.
// Importance: Runs in the audio callback thread. Must be lock-free and fast.
// Missing: None
// Known issues: None
// ---------------------------------------------------------------------------

#include "dispatcher.h"
#include "engine_state.h"
#include "ring_buffer.h"
#include "common.h"
#include "dsp_processor.h"
#include "limiter.h"
#include "effect.h"
#include "compressor.h"
#include "reverb.h"
#include "aaudio_utils.h"
#include "export_mix.h"
#include "export_async.h"
#include "flac_handler.h"
#include "wav_handler.h"

#include <cmath>
#include <cstring>
#include <string>
#include <cctype>
#include <thread>
#include <jni.h>
#include <time.h>

// ─── AAudio data callback (runs in high-priority audio thread) ───────────────
// Sums all active tracks into a single output buffer with volume/pan per track.

aaudio_data_callback_result_t aaudioDataCallback(
    AAudioStream *stream, void *userData, void *audioData, int32_t numFrames) {

    struct timespec tStart;
    clock_gettime(CLOCK_MONOTONIC, &tStart);

    float *out = (float*)audioData;
    int32_t ch = gCtl.outChannels;

    // Zero buffer — AAudio doesn't guarantee clean buffer on all devices
    memset(out, 0, (size_t)numFrames * ch * sizeof(float));

    // If stream was disconnected, output silence while reconnecting
    if (gCtl.streamDisconnected.load(std::memory_order_relaxed)) {
        return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }
    int32_t maxFrames = 0;

    // Check if any track has solo enabled
    bool anySolo = false;
    for (int s = 0; s < MAX_TRACKS; s++)
        if (gCtl.tracks[s].solo) { anySolo = true; break; }

    // Sum all active tracks
    float temp[4096];  // single buffer outside loop — only one track processed at a time
    for (int t = 0; t < MAX_TRACKS; t++) {
        TrackState &trk = gCtl.tracks[t];
        if (!trk.running || !trk.ringBuf) continue;
        if (trk.paused) continue;
        if (trk.mute) continue;
        if (anySolo && !trk.solo) continue;

        int32_t frames = trk.ringBuf->pop(temp, numFrames, ch);
        if (frames <= 0) {
            trk.underrunCount.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        if (frames > maxFrames) maxFrames = frames;
        trk.totalCallbacks.fetch_add(1, std::memory_order_relaxed);
        trk.totalCallbackFrames.fetch_add(frames, std::memory_order_relaxed);

        // Log first-audio latency (time from track_play to first successful pop)
        int64_t playStart = trk.playStartTimeMs.load(std::memory_order_relaxed);
        if (playStart > 0) {
            trk.playStartTimeMs.store(0, std::memory_order_relaxed);
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            int64_t nowMs = (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
            LOGI("PERF[%d]: first-audio latency = %lld ms", t, (long long)(nowMs - playStart));
        }

        // Apply volume + constant-power pan (relaxed loads — stale values cause 1-frame glitch, inaudible)
        float vol = trk.volume.load(std::memory_order_relaxed);
        float cosP = trk.cosPan;
        float sinP = trk.sinPan;

        // Pause/Resume fade ramp — interpolate gain per frame
        int fadeState = trk.fadeState.load(std::memory_order_relaxed);
        if (fadeState != 0) {
            int32_t remaining = trk.fadeRemaining.load(std::memory_order_relaxed);
            int32_t fadeFrames = frames < remaining ? frames : remaining;
            float startGain = trk.fadeGain;
            float endGain = (fadeState == 1) ? 0.0f : 1.0f;
            float tStep = 1.0f / (float)remaining;

            if (ch == 2) {
                float t = 0.0f;
                for (int32_t f = 0; f < frames; f++) {
                    int i = f * 2;
                    float gain;
                    if (f < fadeFrames) {
                        float t2 = t * t;
                        gain = startGain + (endGain - startGain) * (3.0f * t2 - 2.0f * t2 * t);
                        t += tStep;
                    } else {
                        gain = endGain;
                    }
                    float g = vol * gain;
                    out[i]     += temp[i]   * cosP * g;
                    out[i + 1] += temp[i+1] * sinP * g;
                }
            } else {
                float t = 0.0f;
                for (int32_t f = 0; f < frames; f++) {
                    float gain;
                    if (f < fadeFrames) {
                        float t2 = t * t;
                        gain = startGain + (endGain - startGain) * (3.0f * t2 - 2.0f * t2 * t);
                        t += tStep;
                    } else {
                        gain = endGain;
                    }
                    out[f] += temp[f] * vol * gain;
                }
            }

            trk.fadeRemaining.fetch_sub(fadeFrames, std::memory_order_relaxed);
            if (trk.fadeRemaining.load(std::memory_order_relaxed) <= 0) {
                if (fadeState == 1) trk.paused = true;
                trk.fadeState.store(0, std::memory_order_relaxed);
                trk.fadeGain = endGain;
            } else {
                trk.fadeGain = endGain;
            }
        } else {
            if (ch == 2) {
                for (int32_t f = 0; f < frames; f++) {
                    int i = f * 2;
                    out[i]     += temp[i]   * cosP * vol;
                    out[i + 1] += temp[i+1] * sinP * vol;
                }
            } else {
                for (int32_t i = 0; i < frames * ch; i++) {
                    out[i] += temp[i] * vol;
                }
            }
        }
    }

    // Master volume
    float mv = gCtl.masterVolume.load(std::memory_order_relaxed);
    if (mv != 1.0f && maxFrames > 0) {
        int32_t total = maxFrames * ch;
        for (int i = 0; i < total; i++) out[i] *= mv;
    }

    // EQ is now applied per-track in decoder threads (not post-mixdown here)

    // Apply effect chain (post-EQ, pre-limiter — compressor, etc.)
    if (gCtl.fxChain && maxFrames > 0) {
        gCtl.fxChain->process(out, maxFrames, ch);
    }

    // Apply limiter (post-effects, protects against EQ + compressor boost)
    if (gCtl.limiter && maxFrames > 0) {
        gCtl.limiter->process(out, maxFrames, ch);
    }

    // ─── Performance telemetry ───────────────────────────────────────────
    struct timespec tEnd;
    clock_gettime(CLOCK_MONOTONIC, &tEnd);
    int64_t durNs = (int64_t)(tEnd.tv_sec - tStart.tv_sec) * 1000000000LL
                   + (int64_t)(tEnd.tv_nsec - tStart.tv_nsec);
    gCtl.callbackCount.fetch_add(1, std::memory_order_relaxed);
    gCtl.callbackSumNs.fetch_add(durNs, std::memory_order_relaxed);
    // Update max (relaxed CAS loop — rare contention, negligible cost)
    int64_t prevMax = gCtl.callbackMaxNs.load(std::memory_order_relaxed);
    while (durNs > prevMax) {
        if (gCtl.callbackMaxNs.compare_exchange_weak(prevMax, durNs, std::memory_order_relaxed))
            break;
    }

    // Periodic perf log every 5 seconds
    if (tStart.tv_sec - gCtl.lastPerfLogNs >= 5) {
        gCtl.lastPerfLogNs = tStart.tv_sec;
        int64_t count = gCtl.callbackCount.exchange(0, std::memory_order_relaxed);
        int64_t sumNs = gCtl.callbackSumNs.exchange(0, std::memory_order_relaxed);
        int64_t maxNs = gCtl.callbackMaxNs.exchange(0, std::memory_order_relaxed);
        int32_t underruns = gCtl.engineUnderruns.exchange(0, std::memory_order_relaxed);
        float avgUs = (count > 0) ? (float)(sumNs / count) / 1000.0f : 0;
        float maxUs = (float)maxNs / 1000.0f;

        // Per-track ring buffer fill and underruns
        for (int t = 0; t < MAX_TRACKS; t++) {
            TrackState &trk = gCtl.tracks[t];
            if (!trk.running || !trk.ringBuf) continue;
            int32_t fill = trk.ringBuf->available(ch);
            int32_t cap = trk.ringBuf->capacity(ch);
            int32_t pct = (cap > 0) ? (fill * 100 / cap) : 0;
            int32_t trackUnderruns = trk.underrunCount.exchange(0, std::memory_order_relaxed);
            int32_t trackCallbacks = trk.totalCallbacks.exchange(0, std::memory_order_relaxed);
            int64_t trackFrames = trk.totalCallbackFrames.exchange(0, std::memory_order_relaxed);
            float trackFps = (trackCallbacks > 0 && ch > 0)
                ? (float)(trackFrames / ch) / (float)trackCallbacks : 0;
            LOGI("PERF[%d]: ring=%d/%d (%d%%) underruns=%d f_per_cb=%.1f",
                 t, fill, cap, pct, trackUnderruns, trackFps);
        }
        // AAudio xrun count (hardware-level)
        int32_t hwXruns = AAudioStream_getXRunCount(stream);
        LOGI("PERF callback: avg=%.1fus max=%.1fus underruns=%d hw_xruns=%d callbacks=%lld",
             avgUs, maxUs, underruns, hwXruns, (long long)count);
    }

    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

// ─── FLAC pre-decode for zero-gap crossfade ────────────────────────────────

struct PreDecodeCtx {
    float *buf;
    int32_t maxFrames;
    int32_t channels;
    int32_t totalFrames;
    int32_t sampleRate;
};

static void predecodeMetadataCb(
    const FLAC__StreamDecoder*, const FLAC__StreamMetadata *metadata, void *clientData) {
    auto *ctx = (PreDecodeCtx*)clientData;
    if (metadata->type == FLAC__METADATA_TYPE_STREAMINFO) {
        ctx->sampleRate = metadata->data.stream_info.sample_rate;
    }
}

static FLAC__StreamDecoderWriteStatus predecodeWriteCb(
    const FLAC__StreamDecoder *, const FLAC__Frame *frame,
    const FLAC__int32 *const buffer[], void *clientData)
{
    auto *ctx = (PreDecodeCtx*)clientData;
    int32_t frames = frame->header.blocksize;
    int32_t ch = frame->header.channels;
    int32_t maxFrames = ctx->maxFrames;
    int32_t avail = maxFrames - ctx->totalFrames;
    if (avail <= 0) return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
    if (frames > avail) frames = avail;
    ctx->channels = ch;
    int32_t bps = frame->header.bits_per_sample;
    float scale = 1.0f / (float)(1 << (bps - 1));
    for (int32_t i = 0; i < frames; i++) {
        for (int32_t c = 0; c < ch && c < 2; c++) {
            float s = buffer[c][i] * scale;
            if (s > 1.0f) s = 1.0f; else if (s < -1.0f) s = -1.0f;
            ctx->buf[(ctx->totalFrames + i) * 2 + c] = s;
        }
        if (ch == 1) {
            ctx->buf[(ctx->totalFrames + i) * 2 + 1] = ctx->buf[(ctx->totalFrames + i) * 2];
        }
    }
    ctx->totalFrames += frames;
    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

bool predecodeFlac(TrackState &trk, const char *path, int32_t expectedGen) {
    FLAC__StreamDecoder *decoder = FLAC__stream_decoder_new();
    if (!decoder) { LOGE("  predecode FLAC: decoder_new failed: %s", path); return false; }
    auto *ctx = new PreDecodeCtx();
    ctx->buf = new float[MAX_PREDECODE_FRAMES * 2];
    ctx->maxFrames = MAX_PREDECODE_FRAMES;
    ctx->channels = 0;
    ctx->totalFrames = 0;
    ctx->sampleRate = 0;
    FLAC__StreamDecoderInitStatus st = FLAC__stream_decoder_init_file(
        decoder, path, predecodeWriteCb, predecodeMetadataCb, errorCallback, ctx);
    if (st == FLAC__STREAM_DECODER_INIT_STATUS_OK) {
        if (!FLAC__stream_decoder_process_until_end_of_metadata(decoder)) {
            FLAC__StreamDecoderState ds = FLAC__stream_decoder_get_state(decoder);
            LOGE("  predecode FLAC: metadata failed (state=%d) — file may be corrupted: %s", ds, path);
        } else {
            int32_t fadeLen = crossfadeMsToFrames(gCtl.crossfadeMs.load());
            if (fadeLen > 0 && gCtl.sampleRate > 0 && ctx->sampleRate > 0
                && ctx->sampleRate != gCtl.sampleRate) {
                int32_t requiredInput = (int32_t)((int64_t)fadeLen * ctx->sampleRate / gCtl.sampleRate);
                if (requiredInput > ctx->maxFrames) {
                    float *newBuf = new float[requiredInput * 2];
                    int32_t copied = ctx->totalFrames * 2;
                    for (int32_t i = 0; i < copied; i++) newBuf[i] = ctx->buf[i];
                    delete[] ctx->buf;
                    ctx->buf = newBuf;
                    ctx->maxFrames = requiredInput;
                    LOGI("  predecode FLAC: SR mismatch — expanded buffer %d→%d input frames (%dHz→%dHz, fadeLen=%d)",
                         MAX_PREDECODE_FRAMES, requiredInput, ctx->sampleRate, gCtl.sampleRate, fadeLen);
                }
            }
            while (ctx->totalFrames < ctx->maxFrames) {
                FLAC__StreamDecoderState ds = FLAC__stream_decoder_get_state(decoder);
                if (ds == FLAC__STREAM_DECODER_END_OF_STREAM) break;
                if (ds == FLAC__STREAM_DECODER_ABORTED) break;
                if (!FLAC__stream_decoder_process_single(decoder)) break;
            }
        }
        FLAC__StreamDecoderState finalState = FLAC__stream_decoder_get_state(decoder);
        LOGI("  predecode FLAC: %d frames decoded (state=%d)", ctx->totalFrames, finalState);
    } else {
        LOGE("  predecode FLAC: init_file failed: %d — %s", st, path);
    }
    FLAC__stream_decoder_finish(decoder);
    FLAC__stream_decoder_delete(decoder);
    if (ctx->totalFrames > 0) {
        // Check generation — if track_set_next was called again while we were decoding,
        // discard results to avoid writing stale data into a new track slot.
        // Skip check when expectedGen < 0 (synchronous callers on decoder thread).
        if (expectedGen >= 0 && trk.preBufGeneration.load(std::memory_order_acquire) != expectedGen) {
            LOGI("  predecode FLAC: generation mismatch (expected=%d, current=%d) — discarding",
                 expectedGen, trk.preBufGeneration.load(std::memory_order_acquire));
            delete[] ctx->buf;
            delete ctx;
            return false;
        }
        trk.preBuf = ctx->buf;
        trk.preBufFrames = ctx->totalFrames;
        trk.preBufChannels = 2;  // buffer is always stereo (mono duped to both channels)
        trk.preBufSampleRate = ctx->sampleRate;
        trk.preBufOrigFrames = 0;
        // Pre-resample if SR mismatch — eliminates 70-158ms delay during gapless transition
        if (gCtl.sampleRate > 0 && ctx->sampleRate > 0 && ctx->sampleRate != gCtl.sampleRate) {
            double ratio = (double)gCtl.sampleRate / ctx->sampleRate;
            int32_t outFrames = (int32_t)(ctx->totalFrames * ratio);
            float *resampled = new float[outFrames * 2];
            resampleSinc(resampled, outFrames, ctx->buf, ctx->totalFrames, 2, ratio);
            trk.preBufOrigFrames = ctx->totalFrames;
            delete[] trk.preBuf;
            trk.preBuf = resampled;
            trk.preBufFrames = outFrames;
            trk.preBufSampleRate = gCtl.sampleRate;  // mark as already resampled
            LOGI("  predecode FLAC: pre-resampled %d→%d frames (%dHz→%dHz)",
                 ctx->totalFrames, outFrames, ctx->sampleRate, gCtl.sampleRate);
        }
        trk.preBufReady.store(1, std::memory_order_release);
        LOGI("  predecode FLAC: %d frames ready, %d ch, sr=%d", ctx->totalFrames, trk.preBufChannels, ctx->sampleRate);
    } else {
        LOGW("  predecode FLAC: zero frames decoded — not using preBuf: %s", path);
        delete[] ctx->buf;
    }
    delete ctx;
    return ctx->totalFrames > 0;
}

// ─── EQ pending apply (called by decoder thread after creating gCtl.dsp) ───

void applyPendingEq() {
    if (!gCtl.dsp || !gCtl.eqPending.load(std::memory_order_acquire)) return;
    for (int i = 0; i < 10; i++) {
        gCtl.dsp->setBand(i, static_cast<FilterType>(gCtl.eqTypes[i]),
                          gCtl.eqFreqs[i], gCtl.eqGains[i], gCtl.eqQs[i]);
        gCtl.dsp->setBandEnabled(i, gCtl.eqEnabled[i] != 0);
    }
    gCtl.dsp->setBypass(gCtl.eqBypass != 0);
    gCtl.eqPending.store(0, std::memory_order_release);
    LOGI("applyPendingEq: applied %d-band EQ (bypass=%d)", 10, gCtl.eqBypass);
}

extern "C" {

// ─── Legacy single-track controls (operate on track 0) ─────────────────────

EXPORT void stop_audio() {
    stopTrack(0);
    cleanupEngine();
}

EXPORT void pause_audio() {
    TrackState &trk = gCtl.tracks[0];
    trk.paused = true;
    if (trk.ringBuf) trk.ringBuf->reset();
}

EXPORT void resume_audio() {
    gCtl.tracks[0].paused = false;
}

EXPORT int32_t seek_audio(int32_t positionMs) {
    if (gCtl.sampleRate <= 0) return -1;
    int64_t frame = (int64_t)positionMs * gCtl.sampleRate / 1000;
    gCtl.tracks[0].seekToFrame = frame;
    return 0;
}

EXPORT int32_t get_position() {
    if (gCtl.sampleRate <= 0) return 0;
    return (int32_t)(gCtl.tracks[0].writtenFrames.load() * 1000 / gCtl.sampleRate);
}

EXPORT int32_t get_duration() {
    if (gCtl.sampleRate <= 0) return 0;
    return (int32_t)(gCtl.tracks[0].totalFrames * 1000 / gCtl.sampleRate);
}

EXPORT int32_t is_playing() {
    for (int i = 0; i < MAX_TRACKS; i++)
        if (gCtl.tracks[i].running) return 1;
    return 0;
}

EXPORT int32_t get_pcm_available() {
    TrackState &trk = gCtl.tracks[0];
    if (!trk.running || !trk.pcmRingBuf || gCtl.outChannels <= 0) return 0;
    return trk.pcmRingBuf->available(gCtl.outChannels);
}

EXPORT int32_t read_pcm_samples(float *out, int32_t maxFrames) {
    TrackState &trk = gCtl.tracks[0];
    if (!trk.running || !trk.pcmRingBuf || gCtl.outChannels <= 0) return 0;
    int32_t frames = trk.pcmRingBuf->pop(out, maxFrames, gCtl.outChannels);
    return frames * gCtl.outChannels;
}

EXPORT int32_t track_get_pcm_available(int32_t index) {
    if (index < 0 || index >= MAX_TRACKS) return 0;
    TrackState &trk = gCtl.tracks[index];
    if (!trk.running || !trk.pcmRingBuf || gCtl.outChannels <= 0) return 0;
    return trk.pcmRingBuf->available(gCtl.outChannels);
}

EXPORT int32_t track_read_pcm_samples(int32_t index, float *out, int32_t maxFrames) {
    if (index < 0 || index >= MAX_TRACKS) return 0;
    TrackState &trk = gCtl.tracks[index];
    if (!trk.running || !trk.pcmRingBuf || gCtl.outChannels <= 0) return 0;
    int32_t frames = trk.pcmRingBuf->pop(out, maxFrames, gCtl.outChannels);
    return frames * gCtl.outChannels;
}

EXPORT int32_t track_get_gap_less_version(int32_t index) {
    if (index < 0 || index >= MAX_TRACKS) return 0;
    return gCtl.tracks[index].gapLessVersion;
}

EXPORT int32_t track_get_gap_less_abort(int32_t index) {
    if (index < 0 || index >= MAX_TRACKS) return 0;
    return gCtl.tracks[index].gapLessAbort.exchange(0);
}

// ─── Multi-track controls ─────────────────────────────────────────────────

EXPORT void track_stop(int32_t index) {
    stopTrack(index);
}

EXPORT void track_pause(int32_t index) {
    if (index < 0 || index >= MAX_TRACKS) return;
    TrackState &trk = gCtl.tracks[index];
    int32_t ms = trk.fadeDurationMs.load();
    if (ms > 0 && trk.sampleRate > 0 && !trk.paused.load() && trk.running.load()) {
        int32_t dur = (int32_t)((int64_t)ms * trk.sampleRate / 1000);
        trk.fadeDuration = dur;
        trk.fadeGain = 1.0f;
        trk.fadeRemaining = dur;
        trk.fadeState = 1;  // fading out
        LOGI("track_pause[%d]: fade-out %dms (%d frames)", index, ms, dur);
    } else {
        LOGI("track_pause[%d]: instant (ms=%d sr=%d running=%d)", index, ms, trk.sampleRate, trk.running.load());
        trk.paused = true;
    }
}

EXPORT void track_resume(int32_t index) {
    if (index < 0 || index >= MAX_TRACKS) return;
    TrackState &trk = gCtl.tracks[index];
    int32_t ms = trk.fadeDurationMs.load();
    if (ms > 0 && trk.sampleRate > 0 && trk.running.load()) {
        int32_t dur = (int32_t)((int64_t)ms * trk.sampleRate / 1000);
        trk.fadeDuration = dur;
        trk.paused = false;
        trk.fadeGain = 0.0f;
        trk.fadeRemaining = dur;
        trk.fadeState = 2;  // fading in
        LOGI("track_resume[%d]: fade-in %dms (%d frames)", index, ms, dur);
    } else {
        LOGI("track_resume[%d]: instant (ms=%d sr=%d)", index, ms, trk.sampleRate);
        trk.paused = false;
    }
}

EXPORT int32_t track_seek(int32_t index, int32_t positionMs) {
    if (index < 0 || index >= MAX_TRACKS) return -1;
    TrackState &trk = gCtl.tracks[index];
    if (trk.sampleRate <= 0) return -1;
    int64_t frame = (int64_t)positionMs * trk.sampleRate / 1000;
    trk.seekToFrame = frame;
    return 0;
}

EXPORT int32_t track_get_position(int32_t index) {
    if (index < 0 || index >= MAX_TRACKS) return 0;
    TrackState &trk = gCtl.tracks[index];
    if (trk.sampleRate <= 0) return 0;
    return (int32_t)(trk.writtenFrames.load() * 1000 / trk.sampleRate);
}

EXPORT int32_t track_get_duration(int32_t index) {
    if (index < 0 || index >= MAX_TRACKS) return 0;
    TrackState &trk = gCtl.tracks[index];
    if (trk.sampleRate <= 0) return 0;
    return (int32_t)(trk.totalFrames * 1000 / trk.sampleRate);
}

EXPORT int32_t track_is_playing(int32_t index) {
    if (index < 0 || index >= MAX_TRACKS) return 0;
    return gCtl.tracks[index].running ? 1 : 0;
}

EXPORT int32_t track_is_crossfading(int32_t index) {
    if (index < 0 || index >= MAX_TRACKS) return 0;
    return gCtl.tracks[index].crossfading.load(std::memory_order_relaxed);
}

EXPORT int32_t track_get_crossfade_remaining(int32_t index) {
    if (index < 0 || index >= MAX_TRACKS) return 0;
    return gCtl.tracks[index].crossfadeRemaining.load(std::memory_order_relaxed);
}

EXPORT int32_t track_get_fade_len(int32_t index) {
    if (index < 0 || index >= MAX_TRACKS) return 0;
    return gCtl.tracks[index].fadeLen.load(std::memory_order_relaxed);
}

EXPORT void track_set_volume(int32_t index, float vol) {
    if (index >= 0 && index < MAX_TRACKS)
        gCtl.tracks[index].volume = vol < 0.0f ? 0.0f : (vol > 1.0f ? 1.0f : vol);
}

EXPORT void track_set_pan(int32_t index, float pan) {
    if (index >= 0 && index < MAX_TRACKS) {
        if (pan < -1.0f) pan = -1.0f;
        if (pan > 1.0f) pan = 1.0f;
        gCtl.tracks[index].pan.store(pan, std::memory_order_relaxed);
        // Cache cos/sin for AAudio callback — avoids cosf/sinf per frame
        float angle = (pan + 1.0f) * 0.785398163f;
        gCtl.tracks[index].cosPan = cosf(angle);
        gCtl.tracks[index].sinPan = sinf(angle);
    }
}

EXPORT void track_set_mute(int32_t index, int32_t mute) {
    if (index >= 0 && index < MAX_TRACKS)
        gCtl.tracks[index].mute = mute != 0;
}

EXPORT void track_set_solo(int32_t index, int32_t solo) {
    if (index >= 0 && index < MAX_TRACKS)
        gCtl.tracks[index].solo = solo != 0;
}

EXPORT void track_set_loop(int32_t index, int32_t loop) {
    if (index >= 0 && index < MAX_TRACKS)
        gCtl.tracks[index].repeatCount = loop;
}

EXPORT void track_set_fade_ms(int32_t index, int32_t ms) {
    if (index < 0 || index >= MAX_TRACKS) return;
    TrackState &trk = gCtl.tracks[index];
    trk.fadeDurationMs = ms > 0 ? ms : 0;
    LOGI("track_set_fade_ms[%d]: ms=%d sr=%d", index, ms, trk.sampleRate);
}

EXPORT int32_t track_get_fade_ms(int32_t index) {
    if (index < 0 || index >= MAX_TRACKS) return 0;
    return gCtl.tracks[index].fadeDurationMs.load();
}

EXPORT void track_set_next(int32_t index, const char *path) {
    if (index < 0 || index >= MAX_TRACKS) return;
    if (!path || !path[0]) return;
    TrackState &trk = gCtl.tracks[index];
    strncpy(trk.nextPath, path, sizeof(trk.nextPath) - 1);
    trk.hasNext = 1;
    LOGI("track_set_next[%d]: %s", index, path);
    // Pre-decode first frames for zero-gap crossfade (FLAC only)
    if (trk.preBuf) { delete[] trk.preBuf; trk.preBuf = nullptr; }
    trk.preBufReady = 0;
    trk.preBufFrames = 0;
    trk.preBufOrigFrames = 0;
    trk.crossfadePreBufPos = 0;
    const char *ext = strrchr(path, '.');
    if (ext && (strcasecmp(ext, ".flac") == 0 || strcasecmp(ext, ".FLAC") == 0)) {
        // Probe real format — skip FLAC predecode for mislabeled files
        ProbedFormat realFmt = probeAudioFormat(path);
        if (realFmt != ProbedFormat::FLAC && realFmt != ProbedFormat::UNKNOWN) {
            LOGI("track_set_next[%d]: .flac ext but real format is %d — skipping FLAC predecode",
                 index, (int)realFmt);
            return;
        }
        if (gCtl.outChannels >= 2) {
            // Run predecode on a background thread to avoid blocking the Dart main thread.
            // Bump generation so a stale predecode (from a previous track_set_next) discards.
            int32_t gen = trk.preBufGeneration.fetch_add(1) + 1;
            std::string pathCopy(path);
            std::thread([index, pathCopy, gen]() {
                TrackState &t = gCtl.tracks[index];
                if (!predecodeFlac(t, pathCopy.c_str(), gen)) {
                    LOGW("track_set_next[%d]: async predecode failed — gapless will be skipped", index);
                }
            }).detach();
        } else {
            LOGI("track_set_next[%d]: skipping predecode (outChannels=%d)", index, gCtl.outChannels);
        }
    }
}

EXPORT void track_clear_next(int32_t index) {
    if (index < 0 || index >= MAX_TRACKS) return;
    gCtl.tracks[index].hasNext = 0;
    gCtl.tracks[index].nextPath[0] = '\0';
}

EXPORT void mixer_set_master_volume(float vol) {
    gCtl.masterVolume.store(vol < 0.0f ? 0.0f : (vol > 1.0f ? 1.0f : vol),
                            std::memory_order_relaxed);
}

EXPORT void engine_set_crossfade_ms(int32_t ms) {
    int32_t v = ms < 0 ? 0 : (ms > 10000 ? 10000 : ms);
    gCtl.crossfadeMs.store(v, std::memory_order_release);
    LOGI("engine_set_crossfade_ms: %d ms", v);
}

EXPORT void engine_set_crossfade_volume(float vol) {
    float v = vol < 0.0f ? 0.0f : (vol > 1.0f ? 1.0f : vol);
    gCtl.crossfadeVolume.store(v, std::memory_order_release);
}

// ─── Dart DL API init + native position push ───────────────────────────────

EXPORT void track_init_dart_api_dl(void* data) {
    Dart_InitializeApiDL(data);
    LOGI("track_init_dart_api_dl: initialized");
}

EXPORT void track_register_callback(int64_t port) {
    gCtl.dartPort = port;
    LOGI("track_register_callback: port=%lld", (long long)port);
}

EXPORT int32_t track_analyze_waveform(int32_t index, int32_t numBars, float *outPeaks) {
    if (index < 0 || index >= MAX_TRACKS) return -1;
    if (numBars <= 0 || numBars > 512 || !outPeaks) return -1;

    TrackState &trk = gCtl.tracks[index];
    const char *path = trk.path;
    if (!path[0]) return -2;

    std::string ext;
    const char *dot = strrchr(path, '.');
    if (dot) {
        ext = dot;
        for (auto &c : ext) c = (char)tolower(c);
    }

    if (ext == ".flac") {
        return analyzeFlacWaveform(path, numBars, outPeaks);
    } else if (ext == ".wav") {
        return analyzeWavWaveform(path, numBars, outPeaks);
    }
    return -3;  // unsupported format
}

// ─── EQ control exports ─────────────────────────────────────────────────────

EXPORT void eq_set_band(int32_t index, int32_t type, double freq, double gain, double q) {
    if (index < 0 || index >= 10) return;
    // Always store in pending arrays (kept in sync for next decoder thread)
    gCtl.eqTypes[index] = type;
    gCtl.eqFreqs[index] = freq;
    gCtl.eqGains[index] = gain;
    gCtl.eqQs[index] = q;
    if (!gCtl.dsp) {
        gCtl.eqPending.store(1, std::memory_order_release);
        return;
    }
    gCtl.dsp->setBand(index, static_cast<FilterType>(type), freq, gain, q);
}

EXPORT void eq_set_band_enabled(int32_t index, int32_t enabled) {
    if (index < 0 || index >= 10) return;
    gCtl.eqEnabled[index] = enabled;
    if (!gCtl.dsp) {
        gCtl.eqPending.store(1, std::memory_order_release);
        return;
    }
    gCtl.dsp->setBandEnabled(index, enabled != 0);
}

EXPORT void eq_set_bypass(int32_t bypass) {
    gCtl.eqBypass = bypass;
    if (!gCtl.dsp) {
        gCtl.eqPending.store(1, std::memory_order_release);
        return;
    }
    gCtl.dsp->setBypass(bypass != 0);
}

EXPORT void eq_reset() {
    if (!gCtl.dsp) return;
    gCtl.dsp->resetAllBands();
}

// ─── Per-track EQ exports ──────────────────────────────────────────────────

EXPORT void eq_set_track_band(int32_t trackIndex, int32_t bandIndex,
                               int32_t type, double freq, double gain, double q) {
    if (trackIndex < 0 || trackIndex >= MAX_TRACKS || bandIndex < 0 || bandIndex >= 10) return;
    gCtl.trackEqHasConfig[trackIndex] = 1;
    gCtl.trackEqTypes[trackIndex][bandIndex] = type;
    gCtl.trackEqFreqs[trackIndex][bandIndex] = freq;
    gCtl.trackEqGains[trackIndex][bandIndex] = gain;
    gCtl.trackEqQs[trackIndex][bandIndex] = q;
    TrackState &trk = gCtl.tracks[trackIndex];
    if (!trk.dsp) {
        gCtl.trackEqPending[trackIndex].store(1, std::memory_order_release);
        return;
    }
    trk.dsp->setBand(bandIndex, static_cast<FilterType>(type), freq, gain, q);
}

EXPORT void eq_set_track_band_enabled(int32_t trackIndex, int32_t bandIndex, int32_t enabled) {
    if (trackIndex < 0 || trackIndex >= MAX_TRACKS || bandIndex < 0 || bandIndex >= 10) return;
    gCtl.trackEqEnabled[trackIndex][bandIndex] = enabled;
    TrackState &trk = gCtl.tracks[trackIndex];
    if (!trk.dsp) {
        gCtl.trackEqPending[trackIndex].store(1, std::memory_order_release);
        return;
    }
    trk.dsp->setBandEnabled(bandIndex, enabled != 0);
}

EXPORT void eq_set_track_bypass(int32_t trackIndex, int32_t bypass) {
    if (trackIndex < 0 || trackIndex >= MAX_TRACKS) return;
    gCtl.trackEqBypass[trackIndex] = bypass;
    TrackState &trk = gCtl.tracks[trackIndex];
    if (!trk.dsp) {
        gCtl.trackEqPending[trackIndex].store(1, std::memory_order_release);
        return;
    }
    trk.dsp->setBypass(bypass != 0);
}

EXPORT void eq_reset_track(int32_t trackIndex) {
    if (trackIndex < 0 || trackIndex >= MAX_TRACKS) return;
    TrackState &trk = gCtl.tracks[trackIndex];
    if (trk.dsp) trk.dsp->resetAllBands();
}

EXPORT void eq_clear_track(int32_t trackIndex) {
    if (trackIndex < 0 || trackIndex >= MAX_TRACKS) return;
    gCtl.trackEqHasConfig[trackIndex] = 0;
    gCtl.trackEqPending[trackIndex] = 0;
    TrackState &trk = gCtl.tracks[trackIndex];
    if (trk.dsp) { delete trk.dsp; trk.dsp = nullptr; }
}

// ─── Limiter exports ────────────────────────────────────────────────────────

EXPORT void limiter_set_enabled(int32_t enabled) {
    if (!gCtl.limiter) return;
    gCtl.limiter->setEnabled(enabled != 0);
}

EXPORT void limiter_set_threshold(float db) {
    if (!gCtl.limiter) return;
    gCtl.limiter->setThresholdDb(db);
}

EXPORT void limiter_set_attack(float ms) {
    if (!gCtl.limiter) return;
    gCtl.limiter->setAttackMs(ms);
}

EXPORT void limiter_set_release(float ms) {
    if (!gCtl.limiter) return;
    gCtl.limiter->setReleaseMs(ms);
}

EXPORT void limiter_set_lookahead(float ms) {
    if (!gCtl.limiter) return;
    gCtl.limiter->setLookAheadMs(ms);
}

// ─── FX Chain exports ───────────────────────────────────────────────────────

EXPORT int32_t fx_add(const char *name) {
    if (!gCtl.fxChain) return -1;
    AudioEffect *existing = gCtl.fxChain->find(name);
    if (existing) {
        existing->setEnabled(true);
        LOGI("fx_add: '%s' already in chain, enabled", name);
        return 0;
    }
    auto it = fxRegistry().find(name);
    if (it == fxRegistry().end()) { LOGE("fx_add: unknown effect '%s'", name); return -2; }
    AudioEffect *fx = it->second();
    gCtl.fxChain->add(fx);
    LOGI("fx_add: '%s' added to chain", name);
    return 0;
}

EXPORT int32_t fx_remove(const char *name) {
    if (!gCtl.fxChain) return -1;
    bool ok = gCtl.fxChain->remove(name);
    if (ok) LOGI("fx_remove: '%s' removed", name);
    return ok ? 0 : -2;
}

EXPORT void fx_clear() {
    if (gCtl.fxChain) gCtl.fxChain->clear();
}

EXPORT int32_t fx_set_enabled(const char *name, int32_t enabled) {
    if (!gCtl.fxChain) return -1;
    AudioEffect *fx = gCtl.fxChain->find(name);
    if (!fx) return -2;
    fx->setEnabled(enabled != 0);
    return 0;
}

// ─── Compressor exports ─────────────────────────────────────────────────────

EXPORT void compressor_set_threshold(float db) {
    if (!gCtl.fxChain) return;
    AudioEffect *fx = gCtl.fxChain->find("compressor");
    if (fx) static_cast<Compressor*>(fx)->setThresholdDb(db);
}

EXPORT void compressor_set_ratio(float r) {
    if (!gCtl.fxChain) return;
    AudioEffect *fx = gCtl.fxChain->find("compressor");
    if (fx) static_cast<Compressor*>(fx)->setRatio(r);
}

EXPORT void compressor_set_attack(float ms) {
    if (!gCtl.fxChain) return;
    AudioEffect *fx = gCtl.fxChain->find("compressor");
    if (fx) static_cast<Compressor*>(fx)->setAttackMs(ms);
}

EXPORT void compressor_set_release(float ms) {
    if (!gCtl.fxChain) return;
    AudioEffect *fx = gCtl.fxChain->find("compressor");
    if (fx) static_cast<Compressor*>(fx)->setReleaseMs(ms);
}

EXPORT void compressor_set_knee(float db) {
    if (!gCtl.fxChain) return;
    AudioEffect *fx = gCtl.fxChain->find("compressor");
    if (fx) static_cast<Compressor*>(fx)->setKneeDb(db);
}

EXPORT void compressor_set_makeup(float db) {
    if (!gCtl.fxChain) return;
    AudioEffect *fx = gCtl.fxChain->find("compressor");
    if (fx) static_cast<Compressor*>(fx)->setMakeupDb(db);
}

// ─── Reverb exports ─────────────────────────────────────────────────────────

EXPORT void reverb_set_mix(float v) {
    if (!gCtl.fxChain) return;
    AudioEffect *fx = gCtl.fxChain->find("reverb");
    if (fx) static_cast<Reverb*>(fx)->setMix(v);
}

EXPORT void reverb_set_decay(float v) {
    if (!gCtl.fxChain) return;
    AudioEffect *fx = gCtl.fxChain->find("reverb");
    if (fx) static_cast<Reverb*>(fx)->setDecay(v);
}

EXPORT void reverb_set_room_size(float v) {
    if (!gCtl.fxChain) return;
    AudioEffect *fx = gCtl.fxChain->find("reverb");
    if (fx) static_cast<Reverb*>(fx)->setRoomSize(v);
}

EXPORT void reverb_set_damping(float v) {
    if (!gCtl.fxChain) return;
    AudioEffect *fx = gCtl.fxChain->find("reverb");
    if (fx) static_cast<Reverb*>(fx)->setDamping(v);
}

EXPORT void reverb_set_pre_delay(float ms) {
    if (!gCtl.fxChain) return;
    AudioEffect *fx = gCtl.fxChain->find("reverb");
    if (fx) static_cast<Reverb*>(fx)->setPreDelayMs(ms);
}

// ─── WAV Export ────────────────────────────────────────────────────────

EXPORT int32_t export_mix_to_wav(const char *outputPath, int32_t sampleRate, int32_t bitDepth) {
    if (!outputPath || !outputPath[0]) return -1;
    if (sampleRate <= 0) sampleRate = 44100;
    if (bitDepth != 16 && bitDepth != 24) bitDepth = 24;

    ExportConfig config;
    config.outputPath = outputPath;
    config.outputSampleRate = sampleRate;
    config.outputBitDepth = bitDepth;
    config.outputChannels = 2;

    return exportMixToWav(config);
}

// ─── Single-file conversion to WAV ──────────────────────────────────────

EXPORT int32_t convert_file_to_wav(const char *inputPath, const char *outputPath,
                                   int32_t outputSampleRate, int32_t outputBitDepth) {
    if (!inputPath || !inputPath[0]) return -1;
    if (!outputPath || !outputPath[0]) return -2;
    if (outputSampleRate <= 0) outputSampleRate = 44100;
    if (outputBitDepth != 16 && outputBitDepth != 24) outputBitDepth = 24;

    // Detect format by extension
    const char *ext = strrchr(inputPath, '.');
    if (!ext) return -3;
    ext++;  // skip dot

    DecodedAudio decoded;
    int32_t rc = -4;

    if (strcasecmp(ext, "flac") == 0) {
        rc = decodeFlacFull(inputPath, decoded);
    } else if (strcasecmp(ext, "wav") == 0) {
        rc = decodeWavFull(inputPath, decoded);
    } else if (strcasecmp(ext, "mp3") == 0 || strcasecmp(ext, "aac") == 0 ||
               strcasecmp(ext, "m4a") == 0 || strcasecmp(ext, "ogg") == 0 ||
               strcasecmp(ext, "opus") == 0) {
        rc = decodeMediaFull(inputPath, decoded);
    } else {
        return -5;  // unsupported format
    }

    if (rc != 0 || decoded.frames <= 0) {
        decoded.free();
        return -6;
    }

    const int32_t outSR = outputSampleRate;
    const int32_t ch = decoded.channels;

    // Resample if source SR differs from output SR
    if (decoded.sampleRate != outSR && decoded.sampleRate > 0) {
        double ratio = (double)outSR / decoded.sampleRate;
        int32_t outFrames = (int32_t)(decoded.frames * ratio);
        float *resampled = new float[outFrames * ch];
        resampleSinc(resampled, outFrames, decoded.data, decoded.frames, ch, ratio);
        decoded.free();
        decoded.data = resampled;
        decoded.frames = outFrames;
    }

    // Write WAV
    WavWriter writer;
    if (!writer.open(outputPath, ch, outSR, outputBitDepth)) {
        decoded.free();
        return -7;
    }

    const int32_t BLOCK = 4096;
    int32_t written = 0;
    while (written < decoded.frames) {
        int32_t blockFrames = std::min(BLOCK, decoded.frames - written);
        writer.writeBlock(decoded.data + written * ch, blockFrames);
        written += blockFrames;
    }
    writer.close();
    decoded.free();
    return 0;
}

// ─── JNI bridge for Kotlin notification controls (when Flutter engine is dead) ──

JNIEXPORT void JNICALL
Java_com_fyspa_audio_engine_NativeBridge_pauseAll(JNIEnv *env, jclass clazz) {
    for (int i = 0; i < MAX_TRACKS; i++) {
        if (gCtl.tracks[i].running && !gCtl.tracks[i].paused)
            track_pause(i);
    }
}

JNIEXPORT void JNICALL
Java_com_fyspa_audio_engine_NativeBridge_resumeAll(JNIEnv *env, jclass clazz) {
    for (int i = 0; i < MAX_TRACKS; i++) {
        if (gCtl.tracks[i].running && gCtl.tracks[i].paused)
            track_resume(i);
    }
}

JNIEXPORT void JNICALL
Java_com_fyspa_audio_engine_NativeBridge_stopAll(JNIEnv *env, jclass clazz) {
    for (int i = 0; i < MAX_TRACKS; i++) {
        if (gCtl.tracks[i].running)
            track_stop(i);
    }
}

JNIEXPORT jboolean JNICALL
Java_com_fyspa_audio_engine_NativeBridge_isAnyPlaying(JNIEnv *env, jclass clazz) {
    for (int i = 0; i < MAX_TRACKS; i++) {
        if (gCtl.tracks[i].running && !gCtl.tracks[i].paused)
            return JNI_TRUE;
    }
    return JNI_FALSE;
}

}
