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
    trk.fadeLen.store(crossfadeMsToFrames(gCtl.crossfadeMs.load()));
    if (trk.preBuf) { delete[] trk.preBuf; trk.preBuf = nullptr; }
    trk.preBufReady = 0;
    trk.preBufFrames = 0;
    trk.skipPacing = 0;
    trk.resampleToStream = 0;
    trk.streamSampleRate = 0;
    trk.resampleOverlapCount = 0;
    memset(trk.resampleOverlap, 0, sizeof(trk.resampleOverlap));

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
    int32_t avail = trk.ringBuf->available(fadeCh);

    // Convert ms→frames using actual sample rate
    int32_t fadeLen = crossfadeMsToFrames(gCtl.crossfadeMs.load());
    trk.fadeLen.store(fadeLen);
    int32_t histCount = trk.fadeHistCount < MAX_CROSSFADE_FRAMES ? trk.fadeHistCount : MAX_CROSSFADE_FRAMES;
    if (histCount > 0) histCount--;  // exclude newest frame (already in ring buffer)
    int32_t preFrames = trk.preBufReady ? trk.preBufFrames : 0;

    // ─── Caso 1: crossfade=0 ───
    if (fadeLen <= 0) {
        if (preFrames > 0) {
            trk.ringBuf->push(trk.preBuf, preFrames, trk.preBufChannels);
        }
        delete[] trk.preBuf; trk.preBuf = nullptr;
        trk.preBufReady = 0; trk.preBufFrames = 0;
        trk.crossfading = 0;
        trk.crossfadeRemaining = 0;
        return preFrames;
    }

    // ─── Caso 2: sin preBuf → solo fade-out ───
    if (preFrames <= 0) {
        if (histCount > 0) {
            int32_t space = trk.ringBuf->capacity(fadeCh) - avail;
            int32_t n = (histCount < space) ? histCount : (space > 0 ? space : 0);
            if (n > 0) {
                // Scan backwards to skip trailing silence in old track
                float silenceThresh = 1e-3f;
                int32_t startIdx = (trk.fadeHistPos - 2 + MAX_CROSSFADE_FRAMES) % MAX_CROSSFADE_FRAMES;
                for (int32_t s = 0; s < histCount; s++) {
                    int checkIdx = (trk.fadeHistPos - 2 - s + MAX_CROSSFADE_FRAMES) % MAX_CROSSFADE_FRAMES;
                    if (fabsf(trk.fadeHistory[checkIdx * 2]) > silenceThresh ||
                        fabsf(trk.fadeHistory[checkIdx * 2 + 1]) > silenceThresh) {
                        startIdx = checkIdx;
                        break;
                    }
                }
                std::vector<float> fadeBuf(n * fadeCh);
                for (int32_t i = 0; i < n; i++) {
                    float g = 1.0f - (float)(i + 1) / (n + 1);
                    int idx = (startIdx - i + MAX_CROSSFADE_FRAMES) % MAX_CROSSFADE_FRAMES;
                    for (int32_t c = 0; c < fadeCh && c < 2; c++)
                        fadeBuf[i * fadeCh + c] = trk.fadeHistory[idx * 2 + c] * g;
                    for (int32_t c = 2; c < fadeCh; c++)
                        fadeBuf[i * fadeCh + c] = 0;
                }
                trk.ringBuf->push(fadeBuf.data(), n, fadeCh);
            }
        }
        trk.crossfading = 1;
        trk.crossfadeRemaining = fadeLen;
        return 0;
    }

    // ─── Caso 3: preBuf + fadeLen > 0 → MEZCLA REAL ───
    // NO reset() — the race between reset() and the AAudio callback's pop()
    // causes push() to fail silently (unsigned underflow → used >= capacity → return 0).
    // Instead, we append the crossfade AFTER existing old-track data in the ring buffer.
    int32_t space = trk.ringBuf->capacity(fadeCh) - avail;

    // mixLen: use full preFrames (no silence skip)
    int32_t mixLen = std::min({fadeLen, histCount, preFrames, space});

    if (mixLen <= 0) {
        if (preFrames > 0 && space > 0) {
            int32_t n = preFrames < space ? preFrames : space;
            for (int32_t j = 0; j < n; j++) {
                float angle = ((float)j / fadeLen) * 1.57079632679f;
                float gainNew = sinf(angle);
                for (int32_t c = 0; c < trk.preBufChannels; c++)
                    trk.preBuf[j * trk.preBufChannels + c] *= gainNew;
            }
            trk.ringBuf->push(trk.preBuf, n, trk.preBufChannels);
        }
        trk.crossfading = 1;
        trk.crossfadeRemaining = fadeLen;
        delete[] trk.preBuf; trk.preBuf = nullptr;
        trk.preBufReady = 0; trk.preBufFrames = 0;
        return preFrames;
    }

    // Equal-power crossfade: compute and push in chunks to avoid ring buffer underrun.
    // Full-buffer computation (132k frames) takes ~22ms — during that time the ring buffer
    // empties and the callback outputs silence → audible click.
    static const int32_t CHUNK = 192;  // 1 callback period at 44100Hz
    float chunkBuf[CHUNK * fadeCh];
    int32_t newestIdx = (trk.fadeHistPos - 1 + MAX_CROSSFADE_FRAMES) % MAX_CROSSFADE_FRAMES;
    for (int32_t i = 0; i < mixLen; i += CHUNK) {
        int32_t n = std::min(CHUNK, mixLen - i);
        for (int32_t j = 0; j < n; j++) {
            float angle = ((float)(i + j) / fadeLen) * 1.57079632679f;
            float gainOld = cosf(angle);
            float gainNew = sinf(angle);
            int hi = (newestIdx - (i + j) + MAX_CROSSFADE_FRAMES) % MAX_CROSSFADE_FRAMES;
            for (int32_t c = 0; c < fadeCh && c < 2; c++)
                chunkBuf[j * fadeCh + c] = trk.fadeHistory[hi * 2 + c] * gainOld
                                        + trk.preBuf[(i + j) * trk.preBufChannels + c] * gainNew;
            for (int32_t c = 2; c < fadeCh; c++)
                chunkBuf[j * fadeCh + c] = 0;
        }
        trk.ringBuf->push(chunkBuf, n, fadeCh);
    }

    int32_t remaining = preFrames - mixLen;
    if (remaining > 0) {
        float *remPtr = trk.preBuf + mixLen * trk.preBufChannels;
        if (mixLen < fadeLen) {
            // Crossfade incomplete — continue sin curve into remaining frames
            for (int32_t j = 0; j < remaining; j++) {
                int32_t fi = mixLen + j;
                float angle = ((float)fi / fadeLen) * 1.57079632679f;
                float g = (angle >= 1.57079632679f) ? 1.0f : sinf(angle);
                for (int32_t c = 0; c < trk.preBufChannels; c++)
                    remPtr[j * trk.preBufChannels + c] *= g;
            }
        }
        // If mixLen >= fadeLen: push at full volume (already at sin(π/2) = 1.0)
        trk.ringBuf->push(remPtr, remaining, trk.preBufChannels);
    }

    int32_t postRemaining = std::max(0, fadeLen - mixLen);
    trk.crossfading = (postRemaining > 0) ? 1 : 0;
    trk.crossfadeRemaining = postRemaining > 0 ? postRemaining : 0;

    delete[] trk.preBuf; trk.preBuf = nullptr;
    trk.preBufReady = 0; trk.preBufFrames = 0;

    return preFrames;
}
