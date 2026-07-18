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

// ─── track_play: start playback on a specific track slot ─────────────────────

int32_t track_play(int32_t index, const char* path) {
    if (index < 0 || index >= MAX_TRACKS) {
        LOGE("track_play: invalid index %d", index);
        return -9;
    }

    // Stop track if already in use
    if (gCtl.tracks[index].running || gCtl.tracks[index].format != AudioFormat::NONE) {
        LOGI("track_play[%d]: stopping existing track", index);
        stopTrack(index);
    }

    TrackState &trk = gCtl.tracks[index];

    const char *ext = strrchr(path, '.');
    if (!ext) { LOGE("track_play[%d]: no extension", index); return -1; }

    char extLower[8] = {0};
    for (int i = 0; i < 7 && ext[i]; i++) extLower[i] = ext[i] | 0x20;

    strncpy(trk.path, path, sizeof(trk.path) - 1);
    LOGI("track_play[%d]: path=%s ext=%s", index, path, extLower);

    if (strcmp(extLower, ".wav") == 0) {
        // Parse WAV RIFF header
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
                trk.channels = fmt[2]|(fmt[3]<<8);
                trk.sampleRate = fmt[4]|(fmt[5]<<8)|(fmt[6]<<16)|(fmt[7]<<24);
                bps = fmt[14]|(fmt[15]<<8);
                trk.bitsPerSample = bps;
                fmtFound = true;
                if (cs > 16) fseek(f, cs - 16, SEEK_CUR);
            } else if (memcmp(chunk, "data", 4) == 0) {
                if (!fmtFound) { fclose(f); return -5; }
                trk.wavDataSize = cs;
                trk.wavData = new uint8_t[cs];
                if (fread(trk.wavData, 1, cs, f) != cs) {
                    delete[] trk.wavData; trk.wavData = nullptr; fclose(f); return -6;
                }
                break;
            } else { fseek(f, cs, SEEK_CUR); }
        }
        fclose(f);
        if (!trk.wavData) return -7;
        trk.wavFrameSize = trk.channels * (bps / 8);
        trk.totalFrames = trk.wavDataSize / trk.wavFrameSize;
        trk.writtenFrames = 0;
        trk.ringBuf = new RingBuffer();
        trk.pcmRingBuf = new RingBuffer();
        trk.stopFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (trk.stopFd < 0) {
            delete trk.ringBuf; trk.ringBuf = nullptr;
            delete trk.pcmRingBuf; trk.pcmRingBuf = nullptr;
            return -8;
        }
        trk.format = AudioFormat::WAV;
        trk.volume = 1.0f;
        trk.pan = 0.0f;
        trk.worker = std::thread(wavPlaybackThread, index);
        LOGI("track_play[%d]: WAV thread launched", index);
        return 0;
    }

    if (strcmp(extLower, ".flac") == 0) {
        trk.ringBuf = new RingBuffer();
        trk.pcmRingBuf = new RingBuffer();
        trk.stopFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (trk.stopFd < 0) {
            delete trk.ringBuf; trk.ringBuf = nullptr;
            delete trk.pcmRingBuf; trk.pcmRingBuf = nullptr;
            return -8;
        }
        trk.format = AudioFormat::FLAC;
        trk.volume = 1.0f;
        trk.pan = 0.0f;
        trk.worker = std::thread(flacPlaybackThread, index);
        LOGI("track_play[%d]: FLAC thread launched", index);
        return 0;
    }

    if (strcmp(extLower, ".mp3") == 0 || strcmp(extLower, ".aac") == 0
        || strcmp(extLower, ".ogg") == 0 || strcmp(extLower, ".m4a") == 0) {
        trk.ringBuf = new RingBuffer();
        trk.pcmRingBuf = new RingBuffer();
        trk.stopFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (trk.stopFd < 0) {
            delete trk.ringBuf; trk.ringBuf = nullptr;
            delete trk.pcmRingBuf; trk.pcmRingBuf = nullptr;
            return -8;
        }
        trk.format = AudioFormat::MEDIA;
        trk.volume = 1.0f;
        trk.pan = 0.0f;
        trk.worker = std::thread(mediaPlaybackThread, index);
        LOGI("track_play[%d]: Media thread launched", index);
        return 0;
    }

    LOGE("track_play[%d]: unsupported format %s", index, ext);
    return -1;
}

// ─── Legacy aliases (backward compat, use track 0) ──────────────────────────

int32_t start_audio(const char* path) {
    return track_play(0, path);
}

int32_t start_media_stream(const char* url) {
    return track_play(0, url);
}

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
