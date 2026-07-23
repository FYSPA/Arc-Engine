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
    trk.fadeLen.store(crossfadeMsToFrames(gCtl.crossfadeMs.load()));
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
            int32_t space = trk.ringBuf->capacity(fadeCh) - avail;
            int32_t n = (histCount < space) ? histCount : (space > 0 ? space : 0);
            if (n > 0) {
                // Scan backwards to skip trailing silence in old track
                float silenceThresh = 1e-4f;
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
                LOGI("  gapless fade-out: %d frames (no preBuf, ch=%d, startIdx=%d)", n, fadeCh, startIdx);
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
    // Crossfade starts from fadeHistPos-2 (second-to-last decoded frame, avoiding double-play
    // of the newest frame which is already in the ring buffer).
    int32_t space = trk.ringBuf->capacity(fadeCh) - avail;

    // Scan backwards through fadeHistory to skip trailing silence in old track.
    // This avoids a volume dip when the old track ends with digital silence.
    float silenceThresh = 1e-4f;
    int32_t newestIdx = (trk.fadeHistPos - 2 + MAX_CROSSFADE_FRAMES) % MAX_CROSSFADE_FRAMES;
    bool audioFound = false;
    int32_t scannedSilent = 0;
    for (int32_t s = 0; s < histCount; s++) {
        int checkIdx = (trk.fadeHistPos - 2 - s + MAX_CROSSFADE_FRAMES) % MAX_CROSSFADE_FRAMES;
        float absMax = std::max(fabsf(trk.fadeHistory[checkIdx * 2]),
                                fabsf(trk.fadeHistory[checkIdx * 2 + 1]));
        if (absMax > silenceThresh) {
            newestIdx = checkIdx;
            audioFound = true;
            scannedSilent = s;
            break;
        }
    }

    LOGI("║ SILENCE SCAN: scanned=%d/%d audioFound=%s silentFrames=%d newestIdx=%d default=%d",
         histCount, histCount, audioFound ? "YES" : "NO", scannedSilent, newestIdx,
         (trk.fadeHistPos - 2 + MAX_CROSSFADE_FRAMES) % MAX_CROSSFADE_FRAMES);

    // Scan preBuf forwards to skip leading silence in the new track.
    // This avoids mixing silence+silence when both tracks have silence at boundaries.
    int32_t preBufStart = 0;
    for (int32_t p = 0; p < preFrames; p++) {
        float absMax = std::max(fabsf(trk.preBuf[p * trk.preBufChannels]),
                                fabsf(trk.preBuf[p * trk.preBufChannels + 1]));
        if (absMax > silenceThresh) {
            preBufStart = p;
            break;
        }
    }
    int32_t availPreBuf = preFrames - preBufStart;
    if (preBufStart > 0) {
        LOGI("║ PREBUF SILENCE SKIP: preBufStart=%d availPreBuf=%d/%d (skipped %d leading silent frames)",
             preBufStart, availPreBuf, preFrames, preBufStart);
    }

    // Log fadeHistory at key positions to understand the audio profile
    auto logFadeHist = [&](const char* label, int32_t idx) {
        int i1 = (idx + MAX_CROSSFADE_FRAMES) % MAX_CROSSFADE_FRAMES;
        int i2 = (idx - 1 + MAX_CROSSFADE_FRAMES) % MAX_CROSSFADE_FRAMES;
        int i3 = (idx - fadeLen/2 + MAX_CROSSFADE_FRAMES) % MAX_CROSSFADE_FRAMES;
        LOGI("║   %s [%d]={%.6f,%.6f} [%d]={%.6f,%.6f} [%d(mid)]={%.6f,%.6f}",
             label, i1, trk.fadeHistory[i1*2], trk.fadeHistory[i1*2+1],
             i2, trk.fadeHistory[i2*2], trk.fadeHistory[i2*2+1],
             i3, trk.fadeHistory[i3*2], trk.fadeHistory[i3*2+1]);
    };
    logFadeHist("fadeHist @newest/next/mid:", newestIdx);

    // Log preBuf at key positions (showing from preBufStart)
    LOGI("║   preBuf [%d]={%.6f,%.6f} [%d+1]={%.6f,%.6f} [%d+100]={%.6f,%.6f} [%d+mid]={%.6f,%.6f}",
         preBufStart, trk.preBuf[preBufStart*trk.preBufChannels], trk.preBuf[preBufStart*trk.preBufChannels+1],
         preBufStart, trk.preBuf[(preBufStart+1)*trk.preBufChannels], trk.preBuf[(preBufStart+1)*trk.preBufChannels+1],
         preBufStart, trk.preBuf[(preBufStart+100)*trk.preBufChannels], trk.preBuf[(preBufStart+100)*trk.preBufChannels+1],
         preBufStart, trk.preBuf[(preBufStart+preFrames/2)*trk.preBufChannels], trk.preBuf[(preBufStart+preFrames/2)*trk.preBufChannels+1]);

    // Calculate mixLen using availPreBuf (frames after skipping leading silence)
    int32_t mixLen = std::min({fadeLen, histCount, availPreBuf, space});

    if (mixLen <= 0) {
        if (availPreBuf > 0 && space > 0) {
            int32_t n = availPreBuf < space ? availPreBuf : space;
            for (int32_t j = 0; j < n; j++) {
                float angle = ((float)j / fadeLen) * 1.57079632679f;
                float gainNew = sinf(angle);
                for (int32_t c = 0; c < trk.preBufChannels; c++)
                    trk.preBuf[(preBufStart + j) * trk.preBufChannels + c] *= gainNew;
            }
            trk.ringBuf->push(trk.preBuf + preBufStart * trk.preBufChannels, n, trk.preBufChannels);
        }
        trk.crossfading = 1;
        trk.crossfadeRemaining = fadeLen;
        delete[] trk.preBuf; trk.preBuf = nullptr;
        trk.preBufReady = 0; trk.preBufFrames = 0;
        return preFrames;
    }

    std::vector<float> mixBuf(mixLen * fadeCh);
    // Goes backwards through history — old track fades out while new track fades in.
    // newestIdx starts at the last non-silent frame (or fadeHistPos-2 if all silent).
    // preBufStart skips leading silence in the new track.
    for (int32_t i = 0; i < mixLen; i++) {
        float angle = ((float)i / fadeLen) * 1.57079632679f;
        float gainOld = cosf(angle);
        float gainNew = sinf(angle);
        int hi = (newestIdx - i + MAX_CROSSFADE_FRAMES) % MAX_CROSSFADE_FRAMES;
        for (int32_t c = 0; c < fadeCh && c < 2; c++)
            mixBuf[i * fadeCh + c] = trk.fadeHistory[hi * 2 + c] * gainOld
                                   + trk.preBuf[(preBufStart + i) * trk.preBufChannels + c] * gainNew;
        for (int32_t c = 2; c < fadeCh; c++)
            mixBuf[i * fadeCh + c] = 0;
    }

    LOGI("╔════════════════════════════════════════════════════════════════════════╗");
    LOGI("║                    CROSSFADE DIAGNOSTIC START                          ║");
    LOGI("╚════════════════════════════════════════════════════════════════════════╝");

    LOGI("║ SYNCHRONIZATION: avail=%d fadeHistPos=%d newestIdx=%d (default was %d)",
         avail, trk.fadeHistPos, newestIdx,
         (trk.fadeHistPos - 2 + MAX_CROSSFADE_FRAMES) % MAX_CROSSFADE_FRAMES);

    LOGI("║ MIX BUFFER SAMPLES:");
    LOGI("║   [0] mix={%.6f,%.6f}  [1] mix={%.6f,%.6f}  [mid] mix={%.6f,%.6f}",
         mixBuf[0], mixBuf[1], mixBuf[2], mixBuf[3],
         mixBuf[(mixLen/2)*fadeCh], mixBuf[(mixLen/2)*fadeCh+1]);

    LOGI("║ FADEHISTORY @ newestIdx[%d]: {%.6f,%.6f}  @ [%d]: {%.6f,%.6f}",
         newestIdx, trk.fadeHistory[(newestIdx)*2], trk.fadeHistory[(newestIdx)*2+1],
         (newestIdx-1+MAX_CROSSFADE_FRAMES)%MAX_CROSSFADE_FRAMES,
         trk.fadeHistory[((newestIdx-1+MAX_CROSSFADE_FRAMES)%MAX_CROSSFADE_FRAMES)*2],
         trk.fadeHistory[((newestIdx-1+MAX_CROSSFADE_FRAMES)%MAX_CROSSFADE_FRAMES)*2+1]);

    LOGI("║ PREBUFFER SAMPLES (from preBufStart=%d):", preBufStart);
    LOGI("║   [%d]={%.6f,%.6f}  [%d+1]={%.6f,%.6f}  [%d+mid]={%.6f,%.6f}",
         preBufStart, trk.preBuf[preBufStart*trk.preBufChannels], trk.preBuf[preBufStart*trk.preBufChannels+1],
         preBufStart, trk.preBuf[(preBufStart+1)*trk.preBufChannels], trk.preBuf[(preBufStart+1)*trk.preBufChannels+1],
         preBufStart, trk.preBuf[(preBufStart+availPreBuf/2)*trk.preBufChannels], trk.preBuf[(preBufStart+availPreBuf/2)*trk.preBufChannels+1]);

    float gain0_old = cosf(0.0f);
    float gain0_new = sinf(0.0f);
    float gainMid_old = cosf(((float)(mixLen/2)/fadeLen)*1.57079632679f);
    float gainMid_new = sinf(((float)(mixLen/2)/fadeLen)*1.57079632679f);

    LOGI("║ GAIN CURVE (equal-power crossfade):");
    LOGI("║   @[0]: oldTrack=%.6f newTrack=%.6f", gain0_old, gain0_new);
    LOGI("║   @[mid]: oldTrack=%.6f newTrack=%.6f", gainMid_old, gainMid_new);

    LOGI("║ RING BUFFER STATE:");
    LOGI("║   Before: avail=%d capacity=%d space=%d", avail, trk.ringBuf->capacity(fadeCh), space);

    int32_t pushedMix = trk.ringBuf->push(mixBuf.data(), mixLen, fadeCh);
    int32_t availAfterMix = trk.ringBuf->available(fadeCh);

    LOGI("║   After push: pushed=%d availAfter=%d (expected ~%d)",
         pushedMix, availAfterMix, avail + mixLen);

    LOGI("║ SUMMARY:");
    LOGI("║   mixLen=%d fadeLen=%d histCount=%d preFrames=%d preBufStart=%d availPreBuf=%d ch=%d",
         mixLen, fadeLen, histCount, preFrames, preBufStart, availPreBuf, fadeCh);
    LOGI("║   Result: %s",
         (mixBuf.size() >= 2 && mixBuf[0] != 0.0f && mixBuf[1] != 0.0f) ? "✅ MIX HAS AUDIO" : "❌ MIX IS ZERO!");

    LOGI("╔════════════════════════════════════════════════════════════════════════╗");
    LOGI("║                    CROSSFADE DIAGNOSTIC END                            ║");
    LOGI("╚════════════════════════════════════════════════════════════════════════╝");

    int32_t remaining = availPreBuf - mixLen;
    if (remaining > 0) {
        float *remPtr = trk.preBuf + (preBufStart + mixLen) * trk.preBufChannels;
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
