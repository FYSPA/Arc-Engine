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

class RingBuffer;
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
};

struct EngineState {
    AAudioStream *stream{nullptr};
    int32_t sampleRate{0}, outChannels{0};

    DspProcessor *dsp{nullptr};
    Limiter *limiter{nullptr};
    EffectChain *fxChain{nullptr};

    TrackState tracks[MAX_TRACKS];

    float masterVolume{1.0f};

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
