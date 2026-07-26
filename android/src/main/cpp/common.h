// ---------------------------------------------------------------------------
// File: common.h
// Purpose: Shared macros (LOGI, LOGE, EXPORT), FlacInfo struct, and
//          AudioFormat enum used by all engine modules.
// Importance: Every C++ file in the engine includes this header.
// Missing: None
// Known issues: None
// ---------------------------------------------------------------------------

#pragma once

#include <stdint.h>
#include <vector>
#include <math.h>
#include <cstring>
#include <android/log.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "AudioEngine", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "AudioEngine", __VA_ARGS__)

#define EXPORT __attribute__((visibility("default"))) __attribute__((used))

struct FlacInfo {
    int64_t totalSamples;
    int32_t sampleRate;
    int32_t channels;
    int32_t bitsPerSample;
    int32_t durationMs;
};

struct FlacMetadata {
    // Technical properties
    int32_t sampleRate;
    int32_t bitDepth;
    int32_t channels;
    int64_t totalSamples;
    int32_t bitrate;          // kbps

    // Descriptive tags
    char title[256];
    char artist[256];
    char album[256];
    char isrc[16];
    int32_t trackNumber;
    int32_t year;
    int32_t durationMs;
};

enum class AudioFormat { NONE, FLAC, WAV, MEDIA };

#define FADE_FRAMES 256  // ~5.3ms fade at 48kHz for click-free stop/seek
#define SEEKGAP_THRESHOLD 8192  // ~186ms at 44100Hz — trigger gapless when seek is this close to end
#define CROSSFADE_FRAMES 512  // default crossfade ~10.7ms at 48kHz
#define MAX_CROSSFADE_FRAMES 132000  // max ~3s at 44100Hz (Spotify-style crossfade)
#define MAX_PREDECODE_FRAMES 144000  // ~3.26s — enough so after resample (worst case 48k→44.1k) still >= fadeLen
#define MAX_CHANNELS 8               // max channel count for buffer sizing

// ─── Sinc lookup table ────────────────────────────────────────────────────────
// Precomputed sinc×Hann weights for the resampler. Eliminates ~4.5M sin/cos calls
// per resampleSinc invocation (reduces 447ms → ~10ms on mobile ARM).
// 256 bins for fractional position × 17 taps (W=8 per side + center) = 4352 floats.
#define SINC_TABLE_BINS 256
#define SINC_W 8

struct SincTable {
    float w[SINC_TABLE_BINS][2 * SINC_W + 1];
    SincTable() {
        const double PI = 3.14159265358979;
        for (int32_t bin = 0; bin < SINC_TABLE_BINS; bin++) {
            double frac = (double)bin / SINC_TABLE_BINS;
            for (int32_t k = -SINC_W; k <= SINC_W; k++) {
                double x = frac - k;
                double s = (fabs(x) < 1e-10) ? 1.0 : sin(PI * x) / (PI * x);
                double ww = 0.5 * (1.0 + cos(PI * x / (SINC_W + 1)));
                w[bin][k + SINC_W] = (float)(s * ww);
            }
        }
    }
};

static inline const SincTable& getSincTable() {
    static const SincTable table;
    return table;
}

// ─── Sinc resamplers ──────────────────────────────────────────────────────────

// Windowed sinc resampler for cross-sample-rate gapless transitions.
// Uses precomputed sinc×Hann weights via lookup table for speed.
static inline void resampleSinc(float *out, int32_t outFrames,
                                const float *in, int32_t inFrames,
                                int32_t channels, double ratio) {
    const SincTable& st = getSincTable();
    for (int32_t i = 0; i < outFrames; i++) {
        double pos = (double)i / ratio;
        int32_t center = (int32_t)pos;
        double frac = pos - center;
        int32_t bin = (int32_t)(frac * SINC_TABLE_BINS);
        if (bin >= SINC_TABLE_BINS) bin = SINC_TABLE_BINS - 1;
        for (int32_t c = 0; c < channels; c++) {
            double sum = 0.0;
            for (int32_t k = -SINC_W; k <= SINC_W; k++) {
                int32_t idx = center + k;
                if (idx < 0) idx = 0;
                if (idx >= inFrames) idx = inFrames - 1;
                sum += (double)in[idx * channels + c] * st.w[bin][k + SINC_W];
            }
            out[i * channels + c] = (float)sum;
        }
    }
}

// Block-continuous sinc resampler for real-time streaming.
// Maintains overlap between decoded blocks to eliminate clicks at block boundaries.
// overlap: pre-allocated buffer of at least 8 * MAX_CHANNELS floats (from TrackState)
// overlapCount: 0 for first block, 8 for subsequent blocks
// outFrames: output parameter, set to actual number of output frames produced
static inline void resampleSincStream(float *out, int32_t &outFrames,
                                      const float *in, int32_t inFrames,
                                      int32_t channels, double ratio,
                                      float *overlap, int32_t &overlapCount) {
    const SincTable& st = getSincTable();

    // Build extended input: [overlap | current block]
    int32_t extLen = overlapCount + inFrames;
    std::vector<float> ext(extLen * channels);
    if (overlapCount > 0) {
        memcpy(ext.data(), overlap, overlapCount * channels * sizeof(float));
    }
    memcpy(ext.data() + overlapCount * channels, in, inFrames * channels * sizeof(float));

    // Compute output range: skip frames that correspond to the overlap region
    int32_t skipOutput = (overlapCount > 0) ? (int32_t)(overlapCount * ratio + 0.5) : 0;
    int32_t totalOut = (int32_t)(extLen * ratio + 0.5);
    outFrames = totalOut - skipOutput;
    if (outFrames <= 0) { overlapCount = 0; return; }

    // Resample extended buffer, starting from skipOutput
    for (int32_t i = 0; i < outFrames; i++) {
        int32_t outIdx = skipOutput + i;
        double pos = (double)outIdx / ratio;
        int32_t center = (int32_t)pos;
        double frac = pos - center;
        int32_t bin = (int32_t)(frac * SINC_TABLE_BINS);
        if (bin >= SINC_TABLE_BINS) bin = SINC_TABLE_BINS - 1;
        for (int32_t c = 0; c < channels; c++) {
            double sum = 0.0;
            for (int32_t k = -SINC_W; k <= SINC_W; k++) {
                int32_t idx = center + k;
                if (idx < 0) idx = 0;
                if (idx >= extLen) idx = extLen - 1;
                sum += (double)ext[idx * channels + c] * st.w[bin][k + SINC_W];
            }
            out[i * channels + c] = (float)sum;
        }
    }

    // Save last W samples of current block as overlap for next call
    overlapCount = (inFrames >= SINC_W) ? SINC_W : inFrames;
    memcpy(overlap, in + (inFrames - overlapCount) * channels, overlapCount * channels * sizeof(float));
}
