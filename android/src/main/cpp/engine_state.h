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

    // Per-track EQ (nullptr = use global gCtl.dsp)
    DspProcessor *dsp{nullptr};

    int stopFd{-1};

    std::atomic<int> running{0};
    std::atomic<int> paused{0};
    std::atomic<int> mute{0};
    std::atomic<int> solo{0};
    std::atomic<int> loop{0};
    std::atomic<int64_t> seekToFrame{-1};

    std::atomic<float> volume{1.0f};
    std::atomic<float> pan{0.0f};
    float lastFrame[2]{};

    char nextPath[512]{0};
    std::atomic<int> hasNext{0};
    std::atomic<int32_t> gapLessVersion{0};
    std::atomic<int> gapLessAbort{0};

    // Crossfade state for smooth gapless transitions
    float fadeHistory[MAX_CROSSFADE_FRAMES * 2]{};
    int fadeHistPos{0};
    int fadeHistCount{0};
    std::atomic<int> crossfading{0};
    std::atomic<int> crossfadeRemaining{0};
    std::atomic<int> fadeLen{0};  // set by crossfadeMsToFrames() before use
    int32_t crossfadePreBufPos{0};  // read position in preBuf during crossfade mixing

    // Pre-decoded buffer for zero-gap gapless transitions (FLAC only)
    float *preBuf{nullptr};
    int preBufFrames{0};
    int preBufChannels{0};
    int preBufSampleRate{0};           // source sample rate of preBuf (for resampling on SR mismatch)
    int32_t preBufOrigFrames{0};       // original frame count before SR resampling (for seek in new decoder)
    std::atomic<int> preBufReady{0};

    // Flag: skip pacing check after gapless transition to prevent decoder stall
    std::atomic<int> skipPacing{0};

    // Real-time resampling: when set, flacEngineWriteCallback resamples decoded blocks
    // from native SR (trk.sampleRate) to stream SR before pushing to ring buffer.
    // Used for SR mismatch gapless transitions to avoid AAudio stream recreation.
    std::atomic<int> resampleToStream{0};
    int32_t streamSampleRate{0};  // target SR for resampling (= gCtl.sampleRate at crossfade time)

    // Overlap buffer for block-continuous sinc resampling (W=8 samples per channel)
    float resampleOverlap[8 * MAX_CHANNELS]{0};
    int32_t resampleOverlapCount{0};  // 0 = first block, 8 = subsequent blocks

    // Rate-limit for Dart position push callbacks (max 20/sec)
    std::atomic<int64_t> lastCallbackMs{0};

    // Pre-allocated scratch buffers for crossfade/resample in flacEngineWriteCallback.
    // Avoids heap alloc on every FLAC decode callback (audio-adjacent thread).
    float *xmixBuf{nullptr};
    int32_t xmixBufCapacity{0};
    float *xresampleBuf{nullptr};
    int32_t xresampleBufCapacity{0};
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
// Uses smoothstep S-Curve for even perceptual distribution.
inline void applyFadeIn(TrackState &trk, float *buf, int32_t frames, int32_t ch) {
    if (!trk.crossfading || trk.fadeLen <= 0) return;
    int32_t fadeLen = trk.fadeLen.load();
    int32_t rem = trk.crossfadeRemaining.load();
    if (rem <= 0) {
        trk.crossfading = 0;
        trk.crossfadeRemaining = 0;
        return;
    }
    for (int32_t i = 0; i < frames; i++) {
        if (rem <= 0) break;
        float t = (float)(fadeLen - rem) / fadeLen;
        float t2 = t * t;
        float t3 = t2 * t;
        float g = 3.0f * t2 - 2.0f * t3;  // smoothstep fade-in
        for (int32_t c = 0; c < ch; c++)
            buf[i * ch + c] *= g;
        rem--;
    }
    trk.crossfadeRemaining = rem;
    if (rem <= 0) {
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
    std::atomic<int32_t> crossfadeMs{3000};  // default 3000ms (Spotify-style), converted to frames by crossfadeMsToFrames()
    std::atomic<float> crossfadeVolume{1.0f};  // volume multiplier during crossfade zone (0.0-1.0)

    // AAudio stream disconnect detection
    std::atomic<int> streamDisconnected{0};

    // Dart Native Port for position push callbacks
    int64_t dartPort{0};

    // Pending EQ state: stored when gCtl.dsp is null, applied by decoder thread
    std::atomic<int> eqPending{0};
    int32_t eqTypes[10]{};
    double eqFreqs[10]{};
    double eqGains[10]{};
    double eqQs[10]{};
    int32_t eqEnabled[10]{};
    int32_t eqBypass{0};

    // Per-track EQ pending state: stored when track DSP is null, applied by decoder thread
    std::atomic<int> trackEqPending[MAX_TRACKS]{};
    int32_t trackEqTypes[MAX_TRACKS][10]{};
    double trackEqFreqs[MAX_TRACKS][10]{};
    double trackEqGains[MAX_TRACKS][10]{};
    double trackEqQs[MAX_TRACKS][10]{};
    int32_t trackEqEnabled[MAX_TRACKS][10]{};
    int32_t trackEqBypass[MAX_TRACKS]{};
    int32_t trackEqHasConfig[MAX_TRACKS]{};  // 1 = this track has per-track EQ config
};

extern EngineState gCtl;

// Convert crossfade milliseconds to frames using the current sample rate.
inline int32_t crossfadeMsToFrames(int32_t ms) {
    if (ms <= 0 || gCtl.sampleRate <= 0) return 0;
    return std::min((int32_t)((int64_t)ms * gCtl.sampleRate / 1000),
                    (int32_t)MAX_CROSSFADE_FRAMES);
}

void resetCtl();
void cleanupEngine();
void stopEngine();
void stopTrack(int index);
void stopAllTracks();
int findFreeTrack();
int32_t writeGaplessCrossfade(TrackState &trk, int32_t fadeCh);
void applyPendingEq();
void applyPendingTrackEq(int trackIndex);
DspProcessor* getTrackEq(int trackIndex);
