// ---------------------------------------------------------------------------
// File: aaudio_utils.cpp
// Purpose: Implements AAudio stream creation (blocking and callback modes),
//          stream closure, and blocking write helper.
// Importance: Central AAudio utility used by all playback paths.
// Missing: None
// Known issues: None
// ---------------------------------------------------------------------------

#include "aaudio_utils.h"
#include "common.h"
#include "engine_state.h"
#include <thread>
#include <chrono>

static void reconnectStream() {
    LOGI("AAudio reconnect: starting...");
    for (int attempt = 0; attempt < 3; attempt++) {
        if (attempt > 0) {
            LOGI("AAudio reconnect: attempt %d, waiting 500ms...", attempt + 1);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        closeAAudioStream(gCtl.stream);
        gCtl.stream = nullptr;

        AAudioStream *newStream = createAAudioStreamCallback(
            gCtl.sampleRate, gCtl.outChannels,
            aaudioDataCallback, nullptr);
        if (newStream) {
            gCtl.stream = newStream;
            gCtl.streamDisconnected = 0;
            LOGI("AAudio reconnect: SUCCESS on attempt %d", attempt + 1);
            return;
        }
        LOGE("AAudio reconnect: attempt %d FAILED", attempt + 1);
    }
    LOGE("AAudio reconnect: all attempts failed, audio will be silent");
    gCtl.streamDisconnected = 0;
}

void aaudioErrorCallback(AAudioStream *stream, void *userData, aaudio_result_t error) {
    LOGE("AAudio ERROR CALLBACK: error=%d stream=%p", error, stream);
    if (error == AAUDIO_ERROR_DISCONNECTED) {
        LOGE("AAudio STREAM DISCONNECTED — spawning reconnect thread");
        gCtl.streamDisconnected = 1;
        std::thread(reconnectStream).detach();
    }
}

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
    AAudioStreamBuilder_setErrorCallback(builder, aaudioErrorCallback, nullptr);
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
