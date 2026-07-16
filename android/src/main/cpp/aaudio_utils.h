#pragma once

#include <stdint.h>
#include <aaudio/AAudio.h>

AAudioStream* createAAudioStream(int32_t sampleRate, int32_t channels);
AAudioStream* createAAudioStreamCallback(int32_t sampleRate, int32_t channels,
    AAudioStream_dataCallback callback, void *userData);
void closeAAudioStream(AAudioStream *stream);
int32_t writeFrames(AAudioStream *stream, const float *data, int32_t frames, int32_t channels);

// AAudio data callback (defined in audio_engine.cpp)
aaudio_data_callback_result_t aaudioDataCallback(
    AAudioStream *stream, void *userData, void *audioData, int32_t numFrames);
