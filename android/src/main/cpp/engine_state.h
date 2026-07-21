// ---------------------------------------------------------------------------
// File: engine_state.h
// Purpose: Global engine state (EngineState, TrackState) and helper
//          functions (resetCtl, stopTrack, stopAllTracks, findFreeTrack).
// Importance: Central state shared between decoder threads and the AAudio
//            callback. Core of the multi-track mixer.
// Missing: None
// Known issues: None
// ---------------------------------------------------------------------------

#pragma once

#include <atomic>
#include <thread>
#include <aaudio/AAudio.h>
#include "common.h"
#include "ring_buffer.h"
class DspProcessor;
class Limiter;
class EffectChain;

#define MAX_TRACKS 4

struct TrackState {
    std::thread worker;
    AudioFormat format{AudioFormat::NONE};
    int32_t sampleRate{0}, channels{0}, bitsPerSample{0};
    int64_t totalFrames{0};
    std::atomic<int64_t> writtenFrames{0};

    uint8_t *wavData{nullptr};
    uint32_t wavDataSize{0};
    int32_t wavFrameSize{0};

    char path[512]{0};

    RingBuffer *ringBuf{nullptr};
    RingBuffer *pcmRingBuf{nullptr};

    int stopFd{-1};

    volatile int running{0};
    volatile int paused{0};
    volatile int mute{0};
    volatile int solo{0};
    volatile int loop{0};
    std::atomic<int64_t> seekToFrame{-1};

    float volume{1.0f};
    float pan{0.0f};
    float lastFrame[2]{};

    char nextPath[512]{0};
    volatile int hasNext{0};
    volatile int32_t gapLessVersion{0};
    std::atomic<int> gapLessAbort{0};

    // Crossfade state for smooth gapless transitions
    float fadeHistory[MAX_CROSSFADE_FRAMES * 2]{};
    int fadeHistPos{0};
    int fadeHistCount{0};
    std::atomic<int> crossfading{0};
    std::atomic<int> crossfadeRemaining{0};
    std::atomic<int> fadeLen{CROSSFADE_FRAMES};

    // Pre-decoded buffer for zero-gap gapless transitions (FLAC only)
    float *preBuf{nullptr};
    int preBufFrames{0};
    int preBufChannels{0};
    volatile int preBufReady{0};
};

// ─── Crossfade helpers (inline, after TrackState is complete) ─────────────────

// Update circular fade history with the last fadeLen decoded frames.
inline void updateFadeHistory(TrackState &trk, const float *buf, int32_t frames, int32_t ch) {
    if (trk.fadeLen <= 0) return;
    for (int32_t i = 0; i < frames; i++) {
        int pos = trk.fadeHistPos * 2;
        trk.fadeHistory[pos] = buf[i * ch];
        trk.fadeHistory[pos + 1] = (ch > 1) ? buf[i * ch + 1] : buf[i * ch];
        trk.fadeHistPos = (trk.fadeHistPos + 1) % trk.fadeLen;
        if (trk.fadeHistCount < trk.fadeLen) trk.fadeHistCount++;
    }
}

// Apply fade-in gain ramp (0→1) over fadeLen when crossfading.
inline void applyFadeIn(TrackState &trk, float *buf, int32_t frames, int32_t ch) {
    if (!trk.crossfading || trk.fadeLen <= 0) return;
    for (int32_t i = 0; i < frames; i++) {
        float g = 1.0f - (float)trk.crossfadeRemaining / trk.fadeLen;
        if (g < 0.0f) g = 0.0f;
        if (g > 1.0f) g = 1.0f;
        for (int32_t c = 0; c < ch; c++)
            buf[i * ch + c] *= g;
        trk.crossfadeRemaining--;
    }
    if (trk.crossfadeRemaining <= 0) {
        trk.crossfading = 0;
        trk.crossfadeRemaining = 0;
    }
}

// Write a fade-out ramp (full→silence) to the ring buffer using fade history.
// Called BEFORE decoder switch during gapless transition.
// fadeCh = channel count of the OLD track (captured before transition).
inline void writeGaplessFadeOut(TrackState &trk, int32_t fadeCh) {
    if (!trk.ringBuf || trk.fadeHistCount <= 0 || trk.fadeLen <= 0) return;
    int32_t n = trk.fadeHistCount;
    int32_t space = trk.ringBuf->capacity(fadeCh) - trk.ringBuf->available(fadeCh);
    if (n > space) n = (space > 0) ? space : 0;
    if (n <= 0) return;
    float fadeBuf[MAX_CROSSFADE_FRAMES * 2];
    int32_t startIdx = (trk.fadeHistPos - n + trk.fadeLen) % trk.fadeLen;
    for (int32_t i = 0; i < n; i++) {
        float g = 1.0f - (float)i / n;
        int idx = (startIdx + i) % trk.fadeLen;
        int pos = idx * 2;
        for (int32_t c = 0; c < fadeCh && c < 2; c++)
            fadeBuf[i * fadeCh + c] = trk.fadeHistory[pos + c] * g;
        for (int32_t c = 2; c < fadeCh; c++)
            fadeBuf[i * fadeCh + c] = 0;
    }
    trk.ringBuf->push(fadeBuf, n, fadeCh);
    LOGI("  gapless fade-out: %d frames (hist=%d, ch=%d)", n, trk.fadeHistCount, fadeCh);
}

struct EngineState {
    AAudioStream *stream{nullptr};
    int32_t sampleRate{0}, outChannels{0};

    DspProcessor *dsp{nullptr};
    Limiter *limiter{nullptr};
    EffectChain *fxChain{nullptr};

    TrackState tracks[MAX_TRACKS];

    float masterVolume{1.0f};
    int32_t crossfadeFrames{CROSSFADE_FRAMES};

    // Debug counters (shared across all tracks)
    std::atomic<int32_t> callbackCount{0};
    std::atomic<int32_t> callbackFramesTotal{0};
};

extern EngineState gCtl;

void resetCtl();
void stopEngine();
void stopTrack(int index);
void stopAllTracks();
int findFreeTrack();
bool pushPreBuf(TrackState &trk, int32_t &outPreFrames);
