#include "aaudio_utils.h"
#include "common.h"

AAudioStream* createAAudioStream(int32_t sampleRate, int32_t channels) {
    AAudioStreamBuilder *builder;
    if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK) return NULL;

    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setChannelCount(builder, channels);
    AAudioStreamBuilder_setSampleRate(builder, sampleRate);
    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);

    AAudioStream *stream;
    aaudio_result_t ar = AAudioStreamBuilder_openStream(builder, &stream);
    AAudioStreamBuilder_delete(builder);
    if (ar != AAUDIO_OK) { LOGE("AAudio open failed: %d", ar); return NULL; }

    AAudioStream_requestStart(stream);
    return stream;
}

void closeAAudioStream(AAudioStream *stream) {
    if (!stream) return;
    AAudioStream_requestStop(stream);
    AAudioStream_waitForStateChange(stream, AAUDIO_STREAM_STATE_STOPPING, NULL, 5000000000LL);
    AAudioStream_close(stream);
}

AAudioStream* createAAudioStreamCallback(int32_t sampleRate, int32_t channels,
    AAudioStream_dataCallback callback, void *userData) {
    AAudioStreamBuilder *builder;
    if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK) {
        LOGE("createAAudioStreamCallback: builder creation failed");
        return NULL;
    }

    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setChannelCount(builder, channels);
    AAudioStreamBuilder_setSampleRate(builder, sampleRate);
    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setDataCallback(builder, callback, userData);
    AAudioStreamBuilder_setFramesPerDataCallback(builder, 192);

    AAudioStream *stream;
    aaudio_result_t ar = AAudioStreamBuilder_openStream(builder, &stream);
    AAudioStreamBuilder_delete(builder);
    if (ar != AAUDIO_OK) {
        LOGE("AAudio callback open failed: sampleRate=%d channels=%d error=%d", sampleRate, channels, ar);
        return NULL;
    }

    LOGI("AAudio callback stream created OK: sr=%d ch=%d framesPerCallback=%d",
         sampleRate, channels,
         AAudioStream_getFramesPerDataCallback(stream));

    AAudioStream_requestStart(stream);
    LOGI("AAudio callback stream started OK: state=%d", AAudioStream_getState(stream));
    return stream;
}

int32_t writeFrames(AAudioStream *stream, const float *data, int32_t frames, int32_t channels) {
    if (!stream) { LOGE("writeFrames: null stream"); return -1; }
    int32_t written = 0;
    while (written < frames) {
        int32_t ret = AAudioStream_write(stream, data + written * channels, frames - written, 1000000000LL);
        if (ret < 0) { LOGE("AAudio write error: %d", ret); return ret; }
        written += ret;
    }
    return 0;
}
