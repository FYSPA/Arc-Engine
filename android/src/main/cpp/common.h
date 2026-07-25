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
#include <math.h>
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

enum class AudioFormat { NONE, FLAC, WAV, MEDIA };

#define FADE_FRAMES 256  // ~5.3ms fade at 48kHz for click-free stop/seek
#define SEEKGAP_THRESHOLD 8192  // ~186ms at 44100Hz — trigger gapless when seek is this close to end
#define CROSSFADE_FRAMES 512  // default crossfade ~10.7ms at 48kHz
#define MAX_CROSSFADE_FRAMES 132000  // max ~3s at 44100Hz (Spotify-style crossfade)
#define MAX_PREDECODE_FRAMES 144000  // ~3.26s — enough so after resample (worst case 48k→44.1k) still >= fadeLen
#define MAX_CHANNELS 8               // max channel count for buffer sizing

// Windowed sinc resampler for cross-sample-rate gapless transitions.
// Uses a sinc kernel with Hann window, SINC_WIDTH=8 taps per side.
// Much higher quality than linear interpolation — no aliasing or harmonic distortion.
static inline void resampleSinc(float *out, int32_t outFrames,
                                const float *in, int32_t inFrames,
                                int32_t channels, double ratio) {
    const int32_t W = 8;  // half-width of sinc kernel
    const double PI = 3.14159265358979;
    for (int32_t i = 0; i < outFrames; i++) {
        double pos = (double)i / ratio;
        int32_t center = (int32_t)pos;
        for (int32_t c = 0; c < channels; c++) {
            double sum = 0.0;
            for (int32_t k = -W; k <= W; k++) {
                int32_t idx = center + k;
                if (idx < 0) idx = 0;
                if (idx >= inFrames) idx = inFrames - 1;
                double x = pos - idx;
                double s = (x > -1e-10 && x < 1e-10) ? 1.0 : sin(PI * x) / (PI * x);
                double w = 0.5 * (1.0 + cos(PI * x / (W + 1)));
                sum += (double)in[idx * channels + c] * s * w;
            }
            out[i * channels + c] = (float)sum;
        }
    }
}
