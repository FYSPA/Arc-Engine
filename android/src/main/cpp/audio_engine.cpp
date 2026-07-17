// ─── FFI exports (engine controls) ───────────────────────────────────────────
// Non-blocking engine control functions exposed to Dart.
// Legacy exports (play_wav, play_flac, etc.) are in their respective handler files.

#include "dispatcher.h"
#include "engine_state.h"
#include "ring_buffer.h"
#include "common.h"

// ─── AAudio data callback (runs in high-priority audio thread) ───────────────

aaudio_data_callback_result_t aaudioDataCallback(
    AAudioStream *stream, void *userData, void *audioData, int32_t numFrames) {

    gCtl.callbackCount.fetch_add(1, std::memory_order_relaxed);

    RingBuffer *rb = gCtl.ringBuf;
    if (!rb || !gCtl.running) {
        memset(audioData, 0, (size_t)numFrames * gCtl.outChannels * sizeof(float));
        return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }

    int32_t frames = rb->pop((float*)audioData, numFrames, gCtl.outChannels);
    gCtl.callbackFramesTotal.fetch_add(frames, std::memory_order_relaxed);
    if (frames < numFrames) {
        float *out = (float*)audioData;
        memset(out + frames * gCtl.outChannels, 0,
               (size_t)(numFrames - frames) * gCtl.outChannels * sizeof(float));
    }

    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

extern "C" {

EXPORT void stop_audio() {
    stopEngine();
}

EXPORT void pause_audio() {
    gCtl.paused = true;
}

EXPORT void resume_audio() {
    gCtl.paused = false;
}

EXPORT int32_t seek_audio(int32_t positionMs) {
    if (gCtl.sampleRate <= 0) return -1;
    int64_t frame = (int64_t)positionMs * gCtl.sampleRate / 1000;
    gCtl.seekToFrame = frame;
    return 0;
}

EXPORT int32_t get_position() {
    if (gCtl.sampleRate <= 0) return 0;
    return (int32_t)(gCtl.writtenFrames.load() * 1000 / gCtl.sampleRate);
}

EXPORT int32_t get_duration() {
    if (gCtl.sampleRate <= 0) return 0;
    return (int32_t)(gCtl.totalFrames * 1000 / gCtl.sampleRate);
}

EXPORT int32_t is_playing() {
    return gCtl.running ? 1 : 0;
}

EXPORT int32_t get_pcm_available() {
    if (!gCtl.running || !gCtl.pcmRingBuf || gCtl.outChannels <= 0) return 0;
    return gCtl.pcmRingBuf->available(gCtl.outChannels);
}

EXPORT int32_t read_pcm_samples(float *out, int32_t maxFrames) {
    if (!gCtl.running || !gCtl.pcmRingBuf || gCtl.outChannels <= 0) return 0;
    int32_t frames = gCtl.pcmRingBuf->pop(out, maxFrames, gCtl.outChannels);
    return frames * gCtl.outChannels; // return total samples written
}

}
