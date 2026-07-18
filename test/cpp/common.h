#pragma once

#include <stdint.h>

#define LOGI(...)
#define LOGE(...)

#define EXPORT

struct FlacInfo {
    int64_t totalSamples;
    int32_t sampleRate;
    int32_t channels;
    int32_t bitsPerSample;
    int32_t durationMs;
};

enum class AudioFormat { NONE, FLAC, WAV, MEDIA };
