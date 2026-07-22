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
#include "flac_handler.h"

#include <cmath>
#include <cstring>

// ─── AAudio data callback (runs in high-priority audio thread) ───────────────
// Sums all active tracks into a single output buffer with volume/pan per track.

aaudio_data_callback_result_t aaudioDataCallback(
    AAudioStream *stream, void *userData, void *audioData, int32_t numFrames) {

    gCtl.callbackCount.fetch_add(1, std::memory_order_relaxed);

    float *out = (float*)audioData;
    int32_t ch = gCtl.outChannels;

    // Zero output buffer
    memset(out, 0, (size_t)numFrames * ch * sizeof(float));

    int32_t maxFrames = 0;

    // Check if any track has solo enabled
    bool anySolo = false;
    for (int s = 0; s < MAX_TRACKS; s++)
        if (gCtl.tracks[s].solo) { anySolo = true; break; }

    // Sum all active tracks
    for (int t = 0; t < MAX_TRACKS; t++) {
        TrackState &trk = gCtl.tracks[t];
        if (!trk.running || !trk.ringBuf) continue;
        if (trk.mute) continue;
        if (anySolo && !trk.solo) continue;

        // Temp buffer on stack (max 2048 stereo frames = 16384 bytes)
        float temp[4096];
        int32_t frames = trk.ringBuf->pop(temp, numFrames, ch);
        if (frames <= 0) continue;
        if (frames > maxFrames) maxFrames = frames;

        // Apply volume + constant-power pan
        float vol = trk.volume;
        float pan = trk.pan;
        if (ch == 2) {
            float angle = (pan + 1.0f) * 0.785398163f; // (pan+1)*pi/4
            float cosP = cosf(angle);
            float sinP = sinf(angle);
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

    // Master volume
    float mv = gCtl.masterVolume;
    if (mv != 1.0f && maxFrames > 0) {
        int32_t total = maxFrames * ch;
        for (int i = 0; i < total; i++) out[i] *= mv;
    }

    // Apply shared DSP (EQ)
    if (gCtl.dsp && maxFrames > 0) {
        gCtl.dsp->process(out, maxFrames, ch);
    }

    // Apply effect chain (post-EQ, pre-limiter — compressor, etc.)
    if (gCtl.fxChain && maxFrames > 0) {
        gCtl.fxChain->process(out, maxFrames, ch);
    }

    // Apply limiter (post-effects, protects against EQ + compressor boost)
    if (gCtl.limiter && maxFrames > 0) {
        gCtl.limiter->process(out, maxFrames, ch);
    }

    gCtl.callbackFramesTotal.fetch_add(maxFrames, std::memory_order_relaxed);

    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

// ─── FLAC pre-decode for zero-gap crossfade ────────────────────────────────

struct PreDecodeCtx {
    float *buf;
    int32_t maxFrames;
    int32_t channels;
    int32_t totalFrames;
};

static void predecodeMetadataCb(
    const FLAC__StreamDecoder*, const FLAC__StreamMetadata*, void*) {}

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

void predecodeFlac(TrackState &trk, const char *path) {
    FLAC__StreamDecoder *decoder = FLAC__stream_decoder_new();
    if (!decoder) { LOGE("  predecode FLAC: decoder_new failed"); return; }
    auto *ctx = new PreDecodeCtx();
    ctx->buf = new float[MAX_PREDECODE_FRAMES * 2];
    ctx->maxFrames = MAX_PREDECODE_FRAMES;
    ctx->channels = 0;
    ctx->totalFrames = 0;
    FLAC__StreamDecoderInitStatus st = FLAC__stream_decoder_init_file(
        decoder, path, predecodeWriteCb, predecodeMetadataCb, errorCallback, ctx);
    if (st == FLAC__STREAM_DECODER_INIT_STATUS_OK) {
        FLAC__stream_decoder_process_until_end_of_metadata(decoder);
        while (ctx->totalFrames < MAX_PREDECODE_FRAMES) {
            FLAC__StreamDecoderState ds = FLAC__stream_decoder_get_state(decoder);
            if (ds == FLAC__STREAM_DECODER_END_OF_STREAM)
                break;
            if (ds == FLAC__STREAM_DECODER_ABORTED) {
                LOGE("  predecode FLAC: decoder aborted after %d frames", ctx->totalFrames);
                break;
            }
            FLAC__stream_decoder_process_single(decoder);
        }
        LOGI("  predecode FLAC: %d frames decoded", ctx->totalFrames);
    } else {
        LOGE("  predecode FLAC: init_file failed: %d", st);
    }
    FLAC__stream_decoder_finish(decoder);
    FLAC__stream_decoder_delete(decoder);
    if (ctx->totalFrames > 0) {
        trk.preBuf = ctx->buf;
        trk.preBufFrames = ctx->totalFrames;
        trk.preBufChannels = 2;  // buffer is always stereo (mono duped to both channels)
        trk.preBufReady = 1;
        LOGI("  predecode FLAC: %d frames ready, %d ch", ctx->totalFrames, trk.preBufChannels);
    } else {
        LOGI("  predecode FLAC: zero frames decoded — not using preBuf");
        delete[] ctx->buf;
    }
    delete ctx;
}

extern "C" {

// ─── Legacy single-track controls (operate on track 0) ─────────────────────

EXPORT void stop_audio() {
    stopTrack(0);
    // Check if all tracks stopped, then close stream
    bool anyRunning = false;
    for (int i = 0; i < MAX_TRACKS; i++)
        if (gCtl.tracks[i].running) { anyRunning = true; break; }
    if (!anyRunning && gCtl.stream) {
        closeAAudioStream(gCtl.stream);
        gCtl.stream = nullptr;
        if (gCtl.dsp) { delete gCtl.dsp; gCtl.dsp = nullptr; }
        if (gCtl.fxChain) { delete gCtl.fxChain; gCtl.fxChain = nullptr; }
        if (gCtl.limiter) { delete gCtl.limiter; gCtl.limiter = nullptr; }
        gCtl.sampleRate = 0;
        gCtl.outChannels = 0;
    }
}

EXPORT void pause_audio() {
    gCtl.tracks[0].paused = true;
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
    // Close shared stream if no tracks remain
    bool anyRunning = false;
    for (int i = 0; i < MAX_TRACKS; i++)
        if (gCtl.tracks[i].running) { anyRunning = true; break; }
    if (!anyRunning && gCtl.stream) {
        closeAAudioStream(gCtl.stream);
        gCtl.stream = nullptr;
        if (gCtl.dsp) { delete gCtl.dsp; gCtl.dsp = nullptr; }
        if (gCtl.fxChain) { delete gCtl.fxChain; gCtl.fxChain = nullptr; }
        if (gCtl.limiter) { delete gCtl.limiter; gCtl.limiter = nullptr; }
        gCtl.sampleRate = 0;
        gCtl.outChannels = 0;
    }
}

EXPORT void track_pause(int32_t index) {
    if (index >= 0 && index < MAX_TRACKS)
        gCtl.tracks[index].paused = true;
}

EXPORT void track_resume(int32_t index) {
    if (index >= 0 && index < MAX_TRACKS)
        gCtl.tracks[index].paused = false;
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

EXPORT void track_set_volume(int32_t index, float vol) {
    if (index >= 0 && index < MAX_TRACKS)
        gCtl.tracks[index].volume = vol < 0.0f ? 0.0f : (vol > 1.0f ? 1.0f : vol);
}

EXPORT void track_set_pan(int32_t index, float pan) {
    if (index >= 0 && index < MAX_TRACKS) {
        if (pan < -1.0f) pan = -1.0f;
        if (pan > 1.0f) pan = 1.0f;
        gCtl.tracks[index].pan = pan;
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
        gCtl.tracks[index].loop = loop != 0;
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
    const char *ext = strrchr(path, '.');
    if (ext && (strcasecmp(ext, ".flac") == 0 || strcasecmp(ext, ".FLAC") == 0)) {
        if (gCtl.outChannels >= 2) {
            predecodeFlac(trk, path);
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
    gCtl.masterVolume = vol < 0.0f ? 0.0f : (vol > 1.0f ? 1.0f : vol);
}

EXPORT void engine_set_crossfade_frames(int32_t frames) {
    int32_t v = frames < 0 ? 0 : (frames > MAX_CROSSFADE_FRAMES ? MAX_CROSSFADE_FRAMES : frames);
    gCtl.crossfadeFrames.store(v, std::memory_order_release);
}

// ─── EQ control exports ─────────────────────────────────────────────────────

EXPORT void eq_set_band(int32_t index, int32_t type, double freq, double gain, double q) {
    if (!gCtl.dsp) return;
    gCtl.dsp->setBand(index, static_cast<FilterType>(type), freq, gain, q);
}

EXPORT void eq_set_band_enabled(int32_t index, int32_t enabled) {
    if (!gCtl.dsp) return;
    gCtl.dsp->setBandEnabled(index, enabled != 0);
}

EXPORT void eq_set_bypass(int32_t bypass) {
    if (!gCtl.dsp) return;
    gCtl.dsp->setBypass(bypass != 0);
}

EXPORT void eq_reset() {
    if (!gCtl.dsp) return;
    gCtl.dsp->resetAllBands();
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

}
