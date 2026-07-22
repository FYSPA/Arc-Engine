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
#include <vector>
#include <algorithm>
#include <cmath>
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
    std::atomic<int> preBufReady{0};

    // Flag: skip pacing check after gapless transition to prevent decoder stall
    std::atomic<int> skipPacing{0};
};

// ─── Crossfade helpers (inline, after TrackState is complete) ─────────────────

// Update circular fade history with the last fadeLen decoded frames.
inline void updateFadeHistory(TrackState &trk, const float *buf, int32_t frames, int32_t ch) {
    for (int32_t i = 0; i < frames; i++) {
        int pos = trk.fadeHistPos * 2;
        trk.fadeHistory[pos] = buf[i * ch];
        trk.fadeHistory[pos + 1] = (ch > 1) ? buf[i * ch + 1] : buf[i * ch];
        trk.fadeHistPos = (trk.fadeHistPos + 1) % MAX_CROSSFADE_FRAMES;
        if (trk.fadeHistCount < MAX_CROSSFADE_FRAMES) trk.fadeHistCount++;
    }
}

// Apply fade-in gain ramp (0→1) over fadeLen when crossfading.
// Uses sin/cos equal-power curve matching writeGaplessCrossfade for continuity.
inline void applyFadeIn(TrackState &trk, float *buf, int32_t frames, int32_t ch) {
    if (!trk.crossfading || trk.fadeLen <= 0) return;
    int32_t fadeLen = trk.fadeLen.load();
    for (int32_t i = 0; i < frames; i++) {
        int32_t rem = trk.crossfadeRemaining.load();
        if (rem <= 0) {
            trk.crossfading = 0;
            trk.crossfadeRemaining = 0;
            return;
        }
        float angle = ((float)(fadeLen - rem) / fadeLen) * 1.57079632679f;
        float g = sinf(angle);
        for (int32_t c = 0; c < ch; c++)
            buf[i * ch + c] *= g;
        trk.crossfadeRemaining--;
    }
    if (trk.crossfadeRemaining.load() <= 0) {
        trk.crossfading = 0;
        trk.crossfadeRemaining = 0;
    }
}

struct EngineState {
    AAudioStream *stream{nullptr};
    int32_t sampleRate{0}, outChannels{0};

    DspProcessor *dsp{nullptr};
    Limiter *limiter{nullptr};
    EffectChain *fxChain{nullptr};

    TrackState tracks[MAX_TRACKS];

    float masterVolume{1.0f};
    std::atomic<int32_t> crossfadeFrames{CROSSFADE_FRAMES};

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
int32_t writeGaplessCrossfade(TrackState &trk, int32_t fadeCh);
