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
    // crossfadeFrames is a user preference — don't reset
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
    trk.fadeLen.store(gCtl.crossfadeFrames.load());
    if (trk.preBuf) { delete[] trk.preBuf; trk.preBuf = nullptr; }
    trk.preBufReady = 0;
    trk.preBufFrames = 0;
    trk.skipPacing = 0;

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

int32_t writeGaplessCrossfade(TrackState &trk, int32_t fadeCh) {
    if (!trk.ringBuf) return 0;

    // Flush remaining old-track frames and make room for crossfade
    trk.ringBuf->reset();
    trk.skipPacing = 1;

    // Sync con slider actual + clamp
    trk.fadeLen.store(gCtl.crossfadeFrames.load());
    int32_t rawFadeLen = trk.fadeLen.load();
    int32_t fadeLen = rawFadeLen < MAX_CROSSFADE_FRAMES ? rawFadeLen : MAX_CROSSFADE_FRAMES;
    int32_t histCount = trk.fadeHistCount < MAX_CROSSFADE_FRAMES ? trk.fadeHistCount : MAX_CROSSFADE_FRAMES;
    int32_t preFrames = trk.preBufReady ? trk.preBufFrames : 0;

    // ─── Caso 1: crossfade=0 ───
    if (fadeLen <= 0) {
        if (preFrames > 0) {
            trk.ringBuf->push(trk.preBuf, preFrames, trk.preBufChannels);
            LOGI("  gapless direct: %d frames (no crossfade)", preFrames);
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
            int32_t space = trk.ringBuf->capacity(fadeCh) - trk.ringBuf->available(fadeCh);
            int32_t n = (histCount < space) ? histCount : (space > 0 ? space : 0);
            if (n > 0) {
                std::vector<float> fadeBuf(n * fadeCh);
                for (int32_t i = 0; i < n; i++) {
                    float g = 1.0f - (float)i / n;
                    int idx = (trk.fadeHistPos - 1 - i + MAX_CROSSFADE_FRAMES) % MAX_CROSSFADE_FRAMES;
                    for (int32_t c = 0; c < fadeCh && c < 2; c++)
                        fadeBuf[i * fadeCh + c] = trk.fadeHistory[idx * 2 + c] * g;
                    for (int32_t c = 2; c < fadeCh; c++)
                        fadeBuf[i * fadeCh + c] = 0;
                }
                trk.ringBuf->push(fadeBuf.data(), n, fadeCh);
                LOGI("  gapless fade-out: %d frames (no preBuf, ch=%d)", n, fadeCh);
            }
        }
        trk.crossfading = 1;
        trk.crossfadeRemaining = fadeLen;
        return 0;
    }

    // ─── Caso 3: preBuf + fadeLen > 0 → MEZCLA REAL ───
    // Use capacity directly: we just reset() the buffer, and querying available()
    // races with the AAudio callback thread which can corrupt m_readIndex.
    int32_t space = trk.ringBuf->capacity(fadeCh);
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

    std::vector<float> mixBuf(mixLen * fadeCh);
    int32_t startIdx = (trk.fadeHistPos - mixLen + MAX_CROSSFADE_FRAMES) % MAX_CROSSFADE_FRAMES;

    for (int32_t i = 0; i < mixLen; i++) {
        float angle = ((float)i / mixLen) * 1.57079632679f;
        float gainOld = cosf(angle);
        float gainNew = sinf(angle);
        int hi = (startIdx + i) % MAX_CROSSFADE_FRAMES;
        for (int32_t c = 0; c < fadeCh && c < 2; c++)
            mixBuf[i * fadeCh + c] = trk.fadeHistory[hi * 2 + c] * gainOld
                                   + trk.preBuf[i * trk.preBufChannels + c] * gainNew;
        for (int32_t c = 2; c < fadeCh; c++)
            mixBuf[i * fadeCh + c] = 0;
    }
    trk.ringBuf->push(mixBuf.data(), mixLen, fadeCh);
    LOGI("  gapless crossfade: %d frames mixed (hist=%d, preBuf=%d, ch=%d)",
         mixLen, histCount, preFrames, fadeCh);

    int32_t remaining = preFrames - mixLen;
    if (remaining > 0) {
        float *remPtr = trk.preBuf + mixLen * trk.preBufChannels;
        for (int32_t j = 0; j < remaining; j++) {
            float angle = ((float)(mixLen + j) / fadeLen) * 1.57079632679f;
            float gainNew = sinf(angle);
            for (int32_t c = 0; c < trk.preBufChannels; c++)
                remPtr[j * trk.preBufChannels + c] *= gainNew;
        }
        trk.ringBuf->push(remPtr, remaining, trk.preBufChannels);
    }

    int32_t postRemaining = std::max(0, fadeLen - preFrames);
    trk.crossfading = (postRemaining > 0) ? 1 : 0;
    trk.crossfadeRemaining = postRemaining > 0 ? postRemaining : 0;

    delete[] trk.preBuf; trk.preBuf = nullptr;
    trk.preBufReady = 0; trk.preBufFrames = 0;

    return preFrames;
}
