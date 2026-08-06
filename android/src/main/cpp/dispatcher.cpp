// ---------------------------------------------------------------------------
// File: dispatcher.cpp
// Purpose: FFI-exported C API implementation: track lifecycle (play/stop/
//          pause/resume/seek), legacy single-track aliases, volume/pan
//          controls, and EQ configuration.
// Importance: The primary FFI surface consumed by Dart via dart:ffi.
// Missing: None
// Known issues: None
// ---------------------------------------------------------------------------

#include "dispatcher.h"
#include "engine_state.h"
#include "engine_threads.h"
#include "ring_buffer.h"
#include "wav_handler.h"
#include "flac_handler.h"
#include "media_handler.h"
#include "effect.h"
#include "compressor.h"
#include "reverb.h"
#include "common.h"

#include <cstdio>
#include <cstring>
#include <thread>
#include <unistd.h>
#include <fcntl.h>
#include <sys/eventfd.h>

// Static registration of built-in effects
namespace {
    bool _registered = false;
    void ensureFxRegistry() {
        if (!_registered) {
            fxRegistry()["compressor"] = []{ return new Compressor(); };
            fxRegistry()["reverb"] = []{ return new Reverb(); };
            _registered = true;
        }
    }
}

// Creates the shared effect chain if not already created.
void ensureFxChain(float sampleRate, int channels) {
    ensureFxRegistry();
    if (!gCtl.fxChain) {
        gCtl.fxChain = new EffectChain();
        gCtl.fxChain->initAll(sampleRate);
        LOGI("Effect chain created (%d Hz, %d ch)", (int)sampleRate, channels);
    }
}

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

    // Clean up deferred resources from previous playback (stopTrack
    // doesn't delete ringBuf/pcmRingBuf to avoid use-after-free in
    // the AAudio callback — we clean them up here before creating new ones)
    if (trk.ringBuf) { delete trk.ringBuf; trk.ringBuf = nullptr; }
    if (trk.pcmRingBuf) { delete trk.pcmRingBuf; trk.pcmRingBuf = nullptr; }

    const char *ext = strrchr(path, '.');
    if (!ext) { LOGE("track_play[%d]: no extension", index); return -1; }

    char extLower[8] = {0};
    for (int i = 0; i < 7 && ext[i]; i++) extLower[i] = ext[i] | 0x20;

    strncpy(trk.path, path, sizeof(trk.path) - 1);
    LOGI("track_play[%d]: path=%s ext=%s", index, path, extLower);

    if (strcmp(extLower, ".wav") == 0) {
        int32_t rc = loadWavIntoState(trk, path);
        if (rc != 0) return rc;
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
        trk.mute = 0;
        trk.solo = 0;
        trk.worker = std::thread(wavPlaybackThread, index);
        LOGI("track_play[%d]: WAV thread launched", index);
        return 0;
    }

    if (strcmp(extLower, ".flac") == 0) {
        // Probe real format — handle mislabeled files (e.g. MP3 saved as .flac)
        ProbedFormat realFmt = probeAudioFormat(path);
        if (realFmt != ProbedFormat::FLAC && realFmt != ProbedFormat::UNKNOWN) {
            LOGI("track_play[%d]: .flac extension but real format is %d — routing to MediaCodec",
                 index, (int)realFmt);
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
            trk.mute = 0;
            trk.solo = 0;
            trk.worker = std::thread(mediaPlaybackThread, index);
            LOGI("track_play[%d]: Media thread launched (probed from .flac)", index);
            return 0;
        }
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
        trk.mute = 0;
        trk.solo = 0;
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
        trk.mute = 0;
        trk.solo = 0;
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
