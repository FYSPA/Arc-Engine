#pragma once

#include <atomic>
#include <thread>
#include <aaudio/AAudio.h>
#include "common.h"

class RingBuffer;

struct EngineState {
    std::thread worker;

    AudioFormat format{AudioFormat::NONE};
    AAudioStream *stream{nullptr};
    int32_t sampleRate{0}, channels{0}, bitsPerSample{0};
    int64_t totalFrames{0};
    std::atomic<int64_t> writtenFrames{0};

    uint8_t *wavData{nullptr};
    uint32_t wavDataSize{0};
    int32_t wavFrameSize{0};

    char path[512]{0};

    // Ring buffer for callback-mode playback
    RingBuffer *ringBuf{nullptr};
    int32_t outChannels{0};

    // Stop eventfd: kernel-based cross-thread signaling (created before thread spawn)
    int stopFd{-1};

    // Control flags
    volatile int running{0};
    volatile int paused{0};
    std::atomic<int64_t> seekToFrame{-1};

    // Debug counters
    std::atomic<int32_t> callbackCount{0};
    std::atomic<int32_t> callbackFramesTotal{0};
};

extern EngineState gCtl;

void resetCtl();
void stopEngine();
