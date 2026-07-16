#include "engine_state.h"
#include "ring_buffer.h"
#include "aaudio_utils.h"

#include <unistd.h>
#include <sys/eventfd.h>

EngineState gCtl;

void resetCtl() {
    gCtl.running = 0;
    gCtl.paused = 0;
    gCtl.seekToFrame = -1;
    gCtl.stream = nullptr;
    gCtl.sampleRate = gCtl.channels = gCtl.bitsPerSample = 0;
    gCtl.totalFrames = 0;
    gCtl.writtenFrames = 0;
    gCtl.wavData = nullptr;
    gCtl.wavDataSize = gCtl.wavFrameSize = 0;
    gCtl.path[0] = 0;
    gCtl.ringBuf = nullptr;
    gCtl.outChannels = 0;
    gCtl.callbackCount = 0;
    gCtl.callbackFramesTotal = 0;
    gCtl.stopFd = -1;
}

static void cleanupEngine() {
    if (gCtl.stream) { closeAAudioStream(gCtl.stream); gCtl.stream = nullptr; }
    if (gCtl.wavData) { delete[] gCtl.wavData; gCtl.wavData = nullptr; }
    gCtl.wavDataSize = gCtl.wavFrameSize = 0;
    if (gCtl.ringBuf) { delete gCtl.ringBuf; gCtl.ringBuf = nullptr; }
    if (gCtl.stopFd >= 0) { close(gCtl.stopFd); gCtl.stopFd = -1; }
}

void stopEngine() {
    if (gCtl.stopFd >= 0) {
        uint64_t val = 1;
        write(gCtl.stopFd, &val, sizeof(val));
    }
    if (gCtl.worker.joinable()) {
        gCtl.worker.join();
    }
    cleanupEngine();
    resetCtl();
}
