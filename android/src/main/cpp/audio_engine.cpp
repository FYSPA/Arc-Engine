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
#include "aaudio_utils.h"

#include <cmath>

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

    // Apply limiter (post-EQ, protects against EQ boost + track summing)
    if (gCtl.limiter && maxFrames > 0) {
        gCtl.limiter->process(out, maxFrames, ch);
    }

    gCtl.callbackFramesTotal.fetch_add(maxFrames, std::memory_order_relaxed);

    return AAUDIO_CALLBACK_RESULT_CONTINUE;
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

EXPORT void mixer_set_master_volume(float vol) {
    gCtl.masterVolume = vol < 0.0f ? 0.0f : (vol > 1.0f ? 1.0f : vol);
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

}
