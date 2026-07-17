#include "dispatcher.h"
#include "engine_state.h"
#include "engine_threads.h"
#include "ring_buffer.h"
#include "wav_handler.h"
#include "flac_handler.h"
#include "media_handler.h"
#include "common.h"

#include <cstdio>
#include <cstring>
#include <thread>
#include <unistd.h>
#include <fcntl.h>
#include <sys/eventfd.h>

// ─── play_audio (legacy dispatch by extension) ────────────────────────────────

int32_t play_audio(const char* path) {
    const char *ext = strrchr(path, '.');
    if (!ext) { LOGE("play_audio: no extension"); return -1; }

    char extLower[8] = {0};
    for (int i = 0; i < 7 && ext[i]; i++) extLower[i] = ext[i] | 0x20;

    if (strcmp(extLower, ".flac") == 0) return play_flac(path);
    if (strcmp(extLower, ".wav") == 0) return play_wav(path);
    if (strcmp(extLower, ".mp3") == 0 || strcmp(extLower, ".aac") == 0
        || strcmp(extLower, ".ogg") == 0 || strcmp(extLower, ".m4a") == 0) {
        return play_media(path);
    }

    LOGE("Unsupported format: %s", ext);
    return -1;
}

// ─── start_audio (non-blocking engine dispatch) ──────────────────────────────

int32_t start_audio(const char* path) {
    stopEngine();
    resetCtl();

    const char *ext = strrchr(path, '.');
    if (!ext) return -1;

    char extLower[8] = {0};
    for (int i = 0; i < 7 && ext[i]; i++) extLower[i] = ext[i] | 0x20;

    strncpy(gCtl.path, path, sizeof(gCtl.path) - 1);

    if (strcmp(extLower, ".wav") == 0) {
        FILE *f = fopen(path, "rb");
        if (!f) return -1;
        uint8_t riff[12];
        if (fread(riff, 1, 12, f) != 12 || memcmp(riff, "RIFF", 4) != 0 || memcmp(riff + 8, "WAVE", 4) != 0) { fclose(f); return -2; }
        int32_t bps = 0; bool fmtFound = false;
        uint8_t chunk[8];
        while (fread(chunk, 1, 8, f) == 8) {
            uint32_t cs = readInt32LE(chunk + 4);
            if (memcmp(chunk, "fmt ", 4) == 0) {
                uint8_t fmt[16];
                if (cs < 16 || fread(fmt, 1, 16, f) != 16) { fclose(f); return -3; }
                if ((fmt[0]|(fmt[1]<<8)) != 1) { fclose(f); return -4; }
                gCtl.channels = fmt[2]|(fmt[3]<<8);
                gCtl.sampleRate = fmt[4]|(fmt[5]<<8)|(fmt[6]<<16)|(fmt[7]<<24);
                bps = fmt[14]|(fmt[15]<<8);
                gCtl.bitsPerSample = bps;
                fmtFound = true;
                if (cs > 16) fseek(f, cs - 16, SEEK_CUR);
            } else if (memcmp(chunk, "data", 4) == 0) {
                if (!fmtFound) { fclose(f); return -5; }
                gCtl.wavDataSize = cs;
                gCtl.wavData = new uint8_t[cs];
                if (fread(gCtl.wavData, 1, cs, f) != cs) { delete[] gCtl.wavData; fclose(f); return -6; }
                break;
            } else { fseek(f, cs, SEEK_CUR); }
        }
        fclose(f);
        if (!gCtl.wavData) return -7;
        gCtl.wavFrameSize = gCtl.channels * (bps / 8);
        gCtl.totalFrames = gCtl.wavDataSize / gCtl.wavFrameSize;
        gCtl.writtenFrames = 0;
        gCtl.ringBuf = new RingBuffer();
        gCtl.pcmRingBuf = new RingBuffer();
    gCtl.stopFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (gCtl.stopFd < 0) { delete gCtl.ringBuf; gCtl.ringBuf = nullptr; delete gCtl.pcmRingBuf; gCtl.pcmRingBuf = nullptr; return -8; }
    gCtl.format = AudioFormat::WAV;
    gCtl.running = 1;
        gCtl.worker = std::thread(wavPlaybackThread);
        return 0;
    }

    if (strcmp(extLower, ".flac") == 0) {
        gCtl.ringBuf = new RingBuffer();
        gCtl.pcmRingBuf = new RingBuffer();
        gCtl.stopFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (gCtl.stopFd < 0) { delete gCtl.ringBuf; gCtl.ringBuf = nullptr; delete gCtl.pcmRingBuf; gCtl.pcmRingBuf = nullptr; return -8; }
        gCtl.format = AudioFormat::FLAC;
        gCtl.running = true;
        gCtl.worker = std::thread(flacPlaybackThread);
        return 0;
    }

    if (strcmp(extLower, ".mp3") == 0 || strcmp(extLower, ".aac") == 0
        || strcmp(extLower, ".ogg") == 0 || strcmp(extLower, ".m4a") == 0) {
        gCtl.ringBuf = new RingBuffer();
        gCtl.pcmRingBuf = new RingBuffer();
        gCtl.stopFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (gCtl.stopFd < 0) { delete gCtl.ringBuf; gCtl.ringBuf = nullptr; delete gCtl.pcmRingBuf; gCtl.pcmRingBuf = nullptr; return -8; }
        gCtl.format = AudioFormat::MEDIA;
        gCtl.running = true;
        gCtl.worker = std::thread(mediaPlaybackThread);
        return 0;
    }

    return -1;
}

// ─── start_media_stream (URL-based streaming dispatch) ─────────────────────

int32_t start_media_stream(const char* url) {
    stopEngine();
    resetCtl();

    strncpy(gCtl.path, url, sizeof(gCtl.path) - 1);

    gCtl.ringBuf = new RingBuffer();
    gCtl.pcmRingBuf = new RingBuffer();
    gCtl.stopFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (gCtl.stopFd < 0) { delete gCtl.ringBuf; gCtl.ringBuf = nullptr; delete gCtl.pcmRingBuf; gCtl.pcmRingBuf = nullptr; return -8; }
    gCtl.format = AudioFormat::MEDIA;
    gCtl.running = true;
    gCtl.worker = std::thread(mediaStreamPlaybackThread);
    return 0;
}
