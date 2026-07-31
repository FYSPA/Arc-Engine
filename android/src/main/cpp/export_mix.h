#pragma once

#include <cstdint>
#include <cmath>
#include <cstring>
#include <functional>
#include "common.h"
#include "engine_state.h"
#include "export_decoder.h"
#include "wav_writer.h"
#include "dsp_processor.h"
#include "effect.h"
#include "compressor.h"
#include "reverb.h"
#include "limiter.h"

// Offline mix and export to WAV.
// Decodes all active tracks, applies volume/pan/solo/mute, effects chain,
// and writes the result to a 24-bit WAV file.

// Snapshot of track state captured at export-start time.
// Prevents race conditions when tracks are stopped during export.
struct TrackSnapshot {
    char path[512]{};
    AudioFormat format{AudioFormat::NONE};
    int32_t mute{0};
    int32_t solo{0};
    float volume{1.0f};
    float pan{0.0f};
};

struct ExportConfig {
    const char *outputPath;
    int32_t outputSampleRate{44100};
    int32_t outputBitDepth{24};
    int32_t outputChannels{2};
    TrackSnapshot *snapshots{nullptr};  // if set, use instead of gCtl.tracks
};

// Orchestrate full offline export. Returns 0 on success, negative on error.
// onProgress receives 0.0-1.0 progress updates.
static inline int32_t exportMixToWav(
    const ExportConfig &config,
    std::function<void(float)> onProgress = nullptr)
{
    if (!config.outputPath || !config.outputPath[0]) return -1;

    const int32_t BLOCK = 4096;
    const int32_t ch = config.outputChannels;
    const int32_t sr = config.outputSampleRate;

    // ─── 1. Decode all active tracks ───────────────────────────────
    DecodedAudio tracks[MAX_TRACKS];
    int32_t activeCount = 0;
    int64_t totalSourceFrames = 0;

    for (int t = 0; t < MAX_TRACKS; t++) {
        // Use snapshot if available, otherwise read live state
        const char *path;
        AudioFormat format;
        int32_t mute, solo;
        float vol, pan;

        if (config.snapshots) {
            const TrackSnapshot &sn = config.snapshots[t];
            path = sn.path;
            format = sn.format;
            mute = sn.mute;
            solo = sn.solo;
            vol = sn.volume;
            pan = sn.pan;
        } else {
            const TrackState &trk = gCtl.tracks[t];
            path = trk.path;
            format = trk.format;
            mute = trk.mute;
            solo = trk.solo;
            vol = trk.volume;
            pan = trk.pan;
        }

        if (path[0] == '\0') continue;
        if (mute) continue;

        // Check solo logic
        bool anySolo = false;
        for (int s = 0; s < MAX_TRACKS; s++) {
            if (config.snapshots) {
                if (config.snapshots[s].solo) { anySolo = true; break; }
            } else {
                if (gCtl.tracks[s].solo) { anySolo = true; break; }
            }
        }
        if (anySolo && !solo) continue;

        int32_t rc = -1;
        switch (format) {
            case AudioFormat::FLAC:
                rc = decodeFlacFull(path, tracks[t]);
                break;
            case AudioFormat::WAV:
                rc = decodeWavFull(path, tracks[t]);
                break;
            case AudioFormat::MEDIA:
                rc = decodeMediaFull(path, tracks[t]);
                break;
            default:
                break;
        }

        if (rc != 0 || tracks[t].frames <= 0) {
            tracks[t].free();
            continue;
        }

        // Resample if source SR differs from output SR
        if (tracks[t].sampleRate != sr && tracks[t].sampleRate > 0) {
            double ratio = (double)sr / tracks[t].sampleRate;
            int32_t outFrames = (int32_t)(tracks[t].frames * ratio);
            float *resampled = new float[outFrames * ch];
            resampleSinc(resampled, outFrames, tracks[t].data, tracks[t].frames, ch, ratio);
            tracks[t].free();
            tracks[t].data = resampled;
            tracks[t].frames = outFrames;
            tracks[t].sampleRate = sr;
        }

        totalSourceFrames += tracks[t].frames;
        activeCount++;
    }

    if (activeCount == 0) return -2;  // nothing to export

    // Find longest track for progress calculation
    int64_t maxFrames = 0;
    for (int t = 0; t < MAX_TRACKS; t++)
        if (tracks[t].frames > maxFrames) maxFrames = tracks[t].frames;

    // ─── 2. Open WAV writer ────────────────────────────────────────
    WavWriter writer;
    if (!writer.open(config.outputPath, ch, sr, config.outputBitDepth)) {
        for (int t = 0; t < MAX_TRACKS; t++) tracks[t].free();
        return -3;
    }

    // ─── 3. Create DSP processors ──────────────────────────────────
    DspProcessor eq;
    if (gCtl.dsp) {
        // Copy current EQ settings
        eq.init(sr, ch);
        for (int b = 0; b < 10; b++) {
            // Read current band settings from global EQ
            // We need to replicate the band config
        }
    }

    Compressor comp;
    comp.setEnabled(true);

    Reverb reverb;
    reverb.init((float)sr);

    Limiter limiter;
    limiter.setEnabled(true);

    // ─── 4. Mix loop ──────────────────────────────────────────────
    float *mixBuf = new float[BLOCK * ch];
    int64_t framesWritten = 0;

    while (framesWritten < maxFrames) {
        int32_t blockFrames = (int32_t)std::min((int64_t)BLOCK, maxFrames - framesWritten);
        memset(mixBuf, 0, blockFrames * ch * sizeof(float));

        // Mix each active track
        for (int t = 0; t < MAX_TRACKS; t++) {
            if (!tracks[t].data || tracks[t].frames <= 0) continue;

            // Calculate how many frames to read from this track
            int32_t srcOffset = (int32_t)framesWritten;
            int32_t srcAvail = tracks[t].frames - srcOffset;
            int32_t toRead = std::min(blockFrames, srcAvail);
            if (toRead <= 0) continue;

            const float *src = tracks[t].data + srcOffset * ch;
            float vol_track, pan_track;
            if (config.snapshots) {
                vol_track = config.snapshots[t].volume;
                pan_track = config.snapshots[t].pan;
            } else {
                vol_track = gCtl.tracks[t].volume;
                pan_track = gCtl.tracks[t].pan;
            }

            if (ch == 2) {
                float angle = (pan_track + 1.0f) * 0.785398163f;  // pi/4
                float cosP = cosf(angle);
                float sinP = sinf(angle);
                for (int32_t f = 0; f < toRead; f++) {
                    int i = f * 2;
                    mixBuf[i]     += src[i]     * cosP * vol_track;
                    mixBuf[i + 1] += src[i + 1] * sinP * vol_track;
                }
            } else {
                for (int32_t i = 0; i < toRead * ch; i++)
                    mixBuf[i] += src[i] * vol_track;
            }
        }

        // Apply master volume
        float mv = gCtl.masterVolume;
        if (mv != 1.0f) {
            int32_t total = blockFrames * ch;
            for (int i = 0; i < total; i++) mixBuf[i] *= mv;
        }

        // Apply effects chain (same order as AAudio callback)
        // EQ
        if (gCtl.dsp && !gCtl.dsp->isBypassed()) {
            // Create a temporary DspProcessor with current settings
            // For now, use the global one directly (it's safe for sequential processing)
            gCtl.dsp->process(mixBuf, blockFrames, ch);
        }

        // Compressor + Reverb via effect chain
        if (gCtl.fxChain) {
            gCtl.fxChain->process(mixBuf, blockFrames, ch);
        }

        // Limiter
        if (gCtl.limiter) {
            gCtl.limiter->process(mixBuf, blockFrames, ch);
        }

        // Write to WAV
        writer.writeBlock(mixBuf, blockFrames);
        framesWritten += blockFrames;

        // Report progress
        if (onProgress && maxFrames > 0) {
            float progress = (float)framesWritten / (float)maxFrames;
            if (progress > 1.0f) progress = 1.0f;
            onProgress(progress);
        }
    }

    // ─── 5. Finalize ──────────────────────────────────────────────
    writer.close();
    delete[] mixBuf;
    for (int t = 0; t < MAX_TRACKS; t++) tracks[t].free();

    if (onProgress) onProgress(1.0f);
    return 0;
}
