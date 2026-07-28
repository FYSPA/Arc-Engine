#pragma once

#include <atomic>
#include <thread>
#include <cstring>
#include <cstdio>
#include "export_mix.h"
#include "export_decoder.h"
#include "wav_writer.h"

// Async export state — polled from Dart via FFI.
// Only one export/conversion can run at a time.
struct AsyncExport {
    enum Mode { NONE, MIX, FILE_CONVERT };

    std::atomic<int32_t> status{0};   // 0=idle, 1=running, 2=done
    std::atomic<int32_t> resultCode{0};
    std::atomic<float> progress{0.0f};
    std::atomic<Mode> mode{NONE};
    std::thread worker;
    char outputPath[1024]{};
    char inputPath[1024]{};
    int32_t sampleRate{44100};
    int32_t bitDepth{24};

    // Track snapshots captured at start time (for mix export)
    TrackSnapshot snapshots[MAX_TRACKS]{};

    void joinWorker() {
        if (worker.joinable()) worker.join();
    }
};

static AsyncExport gAsync;

// ─── Mix export worker (runs in pthread) ──────────────────────────────

static void mixExportThread() {
    ExportConfig config;
    config.outputPath = gAsync.outputPath;
    config.outputSampleRate = gAsync.sampleRate;
    config.outputBitDepth = gAsync.bitDepth;
    config.outputChannels = 2;
    config.snapshots = gAsync.snapshots;

    auto onProgress = [](float p) {
        gAsync.progress.store(p, std::memory_order_relaxed);
    };

    int32_t rc = exportMixToWav(config, onProgress);
    gAsync.resultCode.store(rc, std::memory_order_relaxed);
    gAsync.progress.store(1.0f, std::memory_order_relaxed);
    gAsync.status.store(2, std::memory_order_relaxed);  // done
}

// ─── File convert worker (runs in pthread) ────────────────────────────

static void fileConvertThread() {
    const char *inputPath = gAsync.inputPath;
    const char *outputPath = gAsync.outputPath;

    LOGI("fileConvertThread: input=%s output=%s", inputPath, outputPath);

    const char *ext = strrchr(inputPath, '.');
    if (!ext) { ext = ""; } else { ext++; }

    DecodedAudio decoded;
    int32_t rc = -5;

    gAsync.progress.store(0.1f, std::memory_order_relaxed);

    if (strcasecmp(ext, "flac") == 0) {
        rc = decodeFlacFull(inputPath, decoded);
    } else if (strcasecmp(ext, "wav") == 0) {
        rc = decodeWavFull(inputPath, decoded);
    } else if (strcasecmp(ext, "mp3") == 0 || strcasecmp(ext, "aac") == 0 ||
               strcasecmp(ext, "m4a") == 0 || strcasecmp(ext, "ogg") == 0 ||
               strcasecmp(ext, "opus") == 0) {
        rc = decodeMediaFull(inputPath, decoded);
    }

    if (rc != 0 || decoded.frames <= 0) {
        LOGI("fileConvertThread: decode FAILED (rc=%d frames=%d ext=%s)", rc, decoded.frames, ext);
        decoded.free();
        gAsync.resultCode.store(-6, std::memory_order_relaxed);
        gAsync.status.store(2, std::memory_order_relaxed);
        return;
    }

    LOGI("fileConvertThread: decode OK (frames=%d ch=%d sr=%d)", decoded.frames, decoded.channels, decoded.sampleRate);

    gAsync.progress.store(0.4f, std::memory_order_relaxed);

    const int32_t outSR = gAsync.sampleRate;
    const int32_t ch = decoded.channels;

    // Resample if needed
    if (decoded.sampleRate != outSR && decoded.sampleRate > 0) {
        double ratio = (double)outSR / decoded.sampleRate;
        int32_t outFrames = (int32_t)(decoded.frames * ratio);
        float *resampled = new float[outFrames * ch];
        resampleSinc(resampled, outFrames, decoded.data, decoded.frames, ch, ratio);
        decoded.free();
        decoded.data = resampled;
        decoded.frames = outFrames;
    }

    gAsync.progress.store(0.6f, std::memory_order_relaxed);

    // Write WAV
    WavWriter writer;
    if (!writer.open(outputPath, ch, outSR, gAsync.bitDepth)) {
        decoded.free();
        gAsync.resultCode.store(-7, std::memory_order_relaxed);
        gAsync.status.store(2, std::memory_order_relaxed);
        return;
    }

    const int32_t BLOCK = 4096;
    int32_t written = 0;
    while (written < decoded.frames) {
        int32_t blockFrames = std::min(BLOCK, decoded.frames - written);
        writer.writeBlock(decoded.data + written * ch, blockFrames);
        written += blockFrames;
        float p = 0.6f + 0.4f * ((float)written / decoded.frames);
        gAsync.progress.store(p, std::memory_order_relaxed);
    }
    writer.close();
    decoded.free();

    gAsync.progress.store(1.0f, std::memory_order_relaxed);
    gAsync.resultCode.store(0, std::memory_order_relaxed);
    gAsync.status.store(2, std::memory_order_relaxed);
}

// ─── FFI exports ──────────────────────────────────────────────────────

extern "C" {

// Start async mix export. Returns 0 on success, -1 if already running.
EXPORT int32_t export_mix_start(const char *outputPath, int32_t sampleRate, int32_t bitDepth) {
    if (gAsync.status.load() == 1) return -1;  // already running
    if (!outputPath || !outputPath[0]) return -2;

    gAsync.joinWorker();
    gAsync.status.store(1, std::memory_order_relaxed);
    gAsync.resultCode.store(0, std::memory_order_relaxed);
    gAsync.progress.store(0.0f, std::memory_order_relaxed);
    gAsync.mode.store(AsyncExport::MIX, std::memory_order_relaxed);
    strncpy(gAsync.outputPath, outputPath, sizeof(gAsync.outputPath) - 1);
    gAsync.sampleRate = (sampleRate > 0) ? sampleRate : 44100;
    gAsync.bitDepth = (bitDepth == 16 || bitDepth == 24) ? bitDepth : 24;

    // Snapshot all track state NOW (before pthread can race with stopTrack)
    for (int t = 0; t < MAX_TRACKS; t++) {
        TrackState &trk = gCtl.tracks[t];
        TrackSnapshot &sn = gAsync.snapshots[t];
        memcpy(sn.path, trk.path, sizeof(sn.path));
        sn.format = trk.format;
        sn.mute = trk.mute.load();
        sn.solo = trk.solo.load();
        sn.volume = trk.volume;
        sn.pan = trk.pan;
    }

    gAsync.worker = std::thread(mixExportThread);
    return 0;
}

// Start async file conversion. Returns 0 on success, -1 if already running.
EXPORT int32_t convert_file_start(const char *inputPath, const char *outputPath,
                                   int32_t sampleRate, int32_t bitDepth) {
    if (gAsync.status.load() == 1) return -1;
    if (!inputPath || !inputPath[0]) return -2;
    if (!outputPath || !outputPath[0]) return -3;

    gAsync.joinWorker();
    gAsync.status.store(1, std::memory_order_relaxed);
    gAsync.resultCode.store(0, std::memory_order_relaxed);
    gAsync.progress.store(0.0f, std::memory_order_relaxed);
    gAsync.mode.store(AsyncExport::FILE_CONVERT, std::memory_order_relaxed);
    strncpy(gAsync.inputPath, inputPath, sizeof(gAsync.inputPath) - 1);
    strncpy(gAsync.outputPath, outputPath, sizeof(gAsync.outputPath) - 1);
    gAsync.sampleRate = (sampleRate > 0) ? sampleRate : 44100;
    gAsync.bitDepth = (bitDepth == 16 || bitDepth == 24) ? bitDepth : 24;

    gAsync.worker = std::thread(fileConvertThread);
    return 0;
}

// Poll status: 0=idle, 1=running, 2=done
EXPORT int32_t export_get_status() {
    return gAsync.status.load(std::memory_order_relaxed);
}

// Poll progress: 0.0 to 1.0
EXPORT float export_get_progress() {
    return gAsync.progress.load(std::memory_order_relaxed);
}

// Get result code (valid when status==2). Returns 0 on success.
EXPORT int32_t export_get_result() {
    return gAsync.resultCode.load(std::memory_order_relaxed);
}

// Cleanup: join worker thread if done. Call after status==2.
EXPORT void export_cleanup() {
    gAsync.joinWorker();
    gAsync.status.store(0, std::memory_order_relaxed);
    gAsync.mode.store(AsyncExport::NONE, std::memory_order_relaxed);
}

}  // extern "C"
