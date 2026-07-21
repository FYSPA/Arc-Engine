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

EngineState gCtl;

void resetCtl() {
    gCtl.stream = nullptr;
    gCtl.sampleRate = 0;
    gCtl.outChannels = 0;
    gCtl.dsp = nullptr;
    gCtl.masterVolume = 1.0f;
    gCtl.crossfadeFrames = CROSSFADE_FRAMES;
    gCtl.callbackCount = 0;
    gCtl.callbackFramesTotal = 0;
}

void stopTrack(int index) {
    if (index < 0 || index >= MAX_TRACKS) return;
    TrackState &trk = gCtl.tracks[index];

    LOGI("stopTrack[%d]: signaling stop (format=%d running=%d)",
         index, (int)trk.format, trk.running);

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

    // Cleanup track resources
    if (trk.wavData) { delete[] trk.wavData; trk.wavData = nullptr; }
    trk.wavDataSize = trk.wavFrameSize = 0;
    if (trk.ringBuf) { delete trk.ringBuf; trk.ringBuf = nullptr; }
    if (trk.pcmRingBuf) { delete trk.pcmRingBuf; trk.pcmRingBuf = nullptr; }
    if (trk.stopFd >= 0) { close(trk.stopFd); trk.stopFd = -1; }

    trk.format = AudioFormat::NONE;
    trk.sampleRate = trk.channels = trk.bitsPerSample = 0;
    trk.totalFrames = 0;
    trk.writtenFrames = 0;
    trk.path[0] = 0;
    trk.running = 0;
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
    trk.fadeLen = CROSSFADE_FRAMES;
    if (trk.preBuf) { delete[] trk.preBuf; trk.preBuf = nullptr; }
    trk.preBufReady = 0;
    trk.preBufFrames = 0;

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

static void cleanupEngine() {
    // Close shared AAudio stream
    if (gCtl.stream) {
        LOGI("cleanupEngine: closing shared AAudio stream");
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
}

void stopEngine() {
    LOGI("stopEngine: stopping all tracks");
    stopAllTracks();
    cleanupEngine();
    resetCtl();
    LOGI("stopEngine: done");
}

bool pushPreBuf(TrackState &trk, int32_t &outPreFrames) {
    outPreFrames = 0;
    if (!trk.preBufReady || trk.preBufFrames <= 0) return false;
    outPreFrames = trk.preBufFrames;
    trk.crossfading = 1;
    trk.crossfadeRemaining = gCtl.crossfadeFrames;
    trk.fadeLen = gCtl.crossfadeFrames;
    applyFadeIn(trk, trk.preBuf, outPreFrames, trk.preBufChannels);
    trk.ringBuf->push(trk.preBuf, outPreFrames, trk.preBufChannels);
    if (trk.pcmRingBuf) {
        trk.pcmRingBuf->push(trk.preBuf, outPreFrames, trk.preBufChannels);
    }
    LOGI("  gapless zero-gap: %d frames from preBuf (%d ch)", outPreFrames, trk.preBufChannels);
    delete[] trk.preBuf;
    trk.preBuf = nullptr;
    trk.preBufReady = 0;
    trk.preBufFrames = 0;
    return true;
}
