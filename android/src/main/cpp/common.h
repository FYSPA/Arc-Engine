#pragma once

#include <stdint.h>
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
