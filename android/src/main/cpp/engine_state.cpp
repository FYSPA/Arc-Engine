// ---------------------------------------------------------------------------
// File: engine_state.cpp
// Purpose: Implementation of global engine state management: track lifecycle
//          (stop, cleanup), free track search, and full engine shutdown.
// Importance: Core lifecycle logic for the multi-track mixer.
// Missing: None
// Known issues: None
// ---------------------------------------------------------------------------

#include "engine_state.h"
#include "ring_buffer.h"
#include "aaudio_utils.h"
#include "dsp_processor.h"
#include "limiter.h"

#include <unistd.h>
#include <sys/eventfd.h>
#include <algorithm>
#include <cmath>

EngineState gCtl;

void resetCtl() {
    gCtl.stream = nullptr;
    gCtl.sampleRate = 0;
    gCtl.outChannels = 0;
    gCtl.dsp = nullptr;
    gCtl.masterVolume = 1.0f;
    gCtl.eqPending = 0;
    // crossfadeMs is a user preference — don't reset
}

void stopTrack(int index) {
    if (index < 0 || index >= MAX_TRACKS) return;
    TrackState &trk = gCtl.tracks[index];

    LOGI("stopTrack[%d]: signaling stop (format=%d running=%d)",
         index, (int)trk.format, (int)trk.running.load());

    // Signal stop via eventfd
    if (trk.stopFd >= 0) {
        uint64_t val = 1;
        write(trk.stopFd, &val, sizeof(val));
    }

    // Wait for decoder thread to finish
    if (trk.worker.joinable()) {
        trk.worker.join();
        LOGI("stopTrack[%d]: worker joined", index);
    }

    // Set running=0 BEFORE cleanup — the AAudio callback may still be
    // mid-flight reading from ringBuf. Setting running=0 ensures no future
    // callback invocation will enter this track.
    trk.running = 0;

    // Cleanup non-callback resources
    if (trk.wavData) { delete[] trk.wavData; trk.wavData = nullptr; }
    trk.wavDataSize = trk.wavFrameSize = 0;
    if (trk.stopFd >= 0) { close(trk.stopFd); trk.stopFd = -1; }

    // DON'T delete ringBuf/pcmRingBuf here — the AAudio callback may still
    // be reading from them. Defer to track_play() or cleanupEngine().
    trk.format = AudioFormat::NONE;
    trk.sampleRate = trk.channels = trk.bitsPerSample = 0;
    trk.totalFrames = 0;
    trk.writtenFrames = 0;
    trk.path[0] = 0;
    trk.paused = 0;
    trk.seekToFrame = -1;
    trk.volume = 1.0f;
    trk.pan = 0.0f;
    trk.mute = 0;
    trk.solo = 0;
    trk.loop = 0;
    trk.hasNext = 0;
    trk.nextPath[0] = '\0';
    trk.crossfading = 0;
    trk.crossfadeRemaining = 0;
    trk.fadeHistPos = 0;
    trk.fadeHistCount = 0;
    trk.fadeLen.store(crossfadeMsToFrames(gCtl.crossfadeMs.load()));
    if (trk.preBuf) { delete[] trk.preBuf; trk.preBuf = nullptr; }
    trk.preBufReady = 0;
    trk.preBufFrames = 0;
    trk.skipPacing = 0;
    trk.resampleToStream = 0;
    trk.streamSampleRate = 0;
    trk.resampleOverlapCount = 0;
    memset(trk.resampleOverlap, 0, sizeof(trk.resampleOverlap));

    // Per-track EQ is NOT deleted here — it persists across play/stop cycles.
    // It's only deleted in cleanupEngine() or when explicitly cleared via FFI.

    LOGI("stopTrack[%d]: done", index);
}

void stopAllTracks() {
    for (int i = 0; i < MAX_TRACKS; i++) {
        stopTrack(i);
    }
}

int findFreeTrack() {
    for (int i = 0; i < MAX_TRACKS; i++) {
        if (!gCtl.tracks[i].running && gCtl.tracks[i].format == AudioFormat::NONE) {
            return i;
        }
    }
    return -1;
}

void cleanupEngine() {
    // Don't cleanup if any track is still running
    for (int i = 0; i < MAX_TRACKS; i++)
        if (gCtl.tracks[i].running) return;

    // Clean up deferred ring buffers from all tracks
    for (int i = 0; i < MAX_TRACKS; i++) {
        if (gCtl.tracks[i].ringBuf) { delete gCtl.tracks[i].ringBuf; gCtl.tracks[i].ringBuf = nullptr; }
        if (gCtl.tracks[i].pcmRingBuf) { delete gCtl.tracks[i].pcmRingBuf; gCtl.tracks[i].pcmRingBuf = nullptr; }
        // Per-track EQ
        if (gCtl.tracks[i].dsp) { delete gCtl.tracks[i].dsp; gCtl.tracks[i].dsp = nullptr; }
    }

    // Close shared AAudio stream
    if (gCtl.stream) {
        closeAAudioStream(gCtl.stream);
        gCtl.stream = nullptr;
    }

    // Delete shared DSP
    if (gCtl.dsp) {
        delete gCtl.dsp;
        gCtl.dsp = nullptr;
    }

    // Delete shared limiter
    if (gCtl.limiter) {
        delete gCtl.limiter;
        gCtl.limiter = nullptr;
    }

    // Delete shared effect chain
    if (gCtl.fxChain) {
        delete gCtl.fxChain;
        gCtl.fxChain = nullptr;
    }

    gCtl.sampleRate = 0;
    gCtl.outChannels = 0;
}

void stopEngine() {
    LOGI("stopEngine: stopping all tracks");
    stopAllTracks();
    cleanupEngine();
    resetCtl();
    LOGI("stopEngine: done");
}

int32_t writeGaplessCrossfade(TrackState &trk, int32_t fadeCh) {
    if (!trk.ringBuf) return 0;

    trk.skipPacing = 1;

    // Convert ms→frames using actual sample rate
    int32_t fadeLen = crossfadeMsToFrames(gCtl.crossfadeMs.load());
    trk.fadeLen.store(fadeLen);
    int32_t histCount = trk.fadeHistCount < MAX_CROSSFADE_FRAMES ? trk.fadeHistCount : MAX_CROSSFADE_FRAMES;
    if (histCount > 0) histCount--;  // exclude newest frame (already in ring buffer)
    int32_t preFrames = trk.preBufReady ? trk.preBufFrames : 0;

    // ─── Caso 1: crossfade=0 ───
    if (fadeLen <= 0) {
        if (preFrames > 0) {
            // Push ALL preBuf frames — retry until ring buffer has space
            int32_t pushed = 0;
            while (pushed < preFrames) {
                int32_t n = trk.ringBuf->push(trk.preBuf + pushed * trk.preBufChannels,
                                              preFrames - pushed, trk.preBufChannels);
                pushed += n;
                if (pushed < preFrames) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
            }
        }
        delete[] trk.preBuf; trk.preBuf = nullptr;
        trk.preBufReady = 0; trk.preBufFrames = 0;
        trk.crossfading = 0;
        trk.crossfadeRemaining = 0;
        return preFrames;
    }

    // ─── Caso 2: sin preBuf → activar fade-in para decoder nuevo ───
    // Old track's frames are already in the ring buffer — no replay needed.
    if (preFrames <= 0) {
        trk.crossfading = 1;
        trk.crossfadeRemaining = fadeLen;
        return 0;
    }

    // ─── Caso 3: preBuf + fadeLen > 0 → GAPLESS TRANSITION ───
    // Push ALL preBuf at full volume. Old track's frames in the ring buffer
    // play naturally, then seamlessly transition to the new track.
    // Retry until ring buffer has space — preBuf MUST be fully pushed
    // because the new decoder seeks past origPreFrames in the source file.
    {
        int32_t pushed = 0;
        while (pushed < preFrames) {
            int32_t n = trk.ringBuf->push(trk.preBuf + pushed * trk.preBufChannels,
                                          preFrames - pushed, trk.preBufChannels);
            pushed += n;
            if (pushed < preFrames) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
    }

    trk.crossfading = 0;
    trk.crossfadeRemaining = 0;

    delete[] trk.preBuf; trk.preBuf = nullptr;
    trk.preBufReady = 0; trk.preBufFrames = 0;

    return preFrames;
}

void applyPendingTrackEq(int trackIndex) {
    if (trackIndex < 0 || trackIndex >= MAX_TRACKS) return;
    TrackState &trk = gCtl.tracks[trackIndex];
    if (!trk.dsp) return;
    if (!gCtl.trackEqPending[trackIndex].load(std::memory_order_acquire)) return;

    for (int i = 0; i < 10; i++) {
        if (gCtl.trackEqEnabled[trackIndex][i]) {
            trk.dsp->setBand(i, static_cast<FilterType>(gCtl.trackEqTypes[trackIndex][i]),
                              gCtl.trackEqFreqs[trackIndex][i], gCtl.trackEqGains[trackIndex][i],
                              gCtl.trackEqQs[trackIndex][i]);
            trk.dsp->setBandEnabled(i, true);
        } else {
            trk.dsp->setBandEnabled(i, false);
        }
    }
    trk.dsp->setBypass(gCtl.trackEqBypass[trackIndex] != 0);
    gCtl.trackEqPending[trackIndex].store(0, std::memory_order_release);
}

DspProcessor* getTrackEq(int trackIndex) {
    if (trackIndex < 0 || trackIndex >= MAX_TRACKS) return nullptr;
    TrackState &trk = gCtl.tracks[trackIndex];
    // Per-track EQ if configured, otherwise global
    if (trk.dsp && gCtl.trackEqHasConfig[trackIndex]) return trk.dsp;
    return gCtl.dsp;
}
