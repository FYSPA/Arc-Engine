#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include "common.h"
#include "engine_state.h"
#include "flac_handler.h"

// Result of offline decoding a single track to float PCM.
struct DecodedAudio {
    float *data{nullptr};        // interleaved float samples [-1,1]
    int32_t frames{0};
    int32_t channels{0};
    int32_t sampleRate{0};

    void free() { delete[] data; data = nullptr; frames = 0; }
};

// ─── FLAC offline decode ──────────────────────────────────────────────

struct FlacExportCtx {
    float *buf;
    int32_t maxFrames;
    int32_t channels;
    int32_t totalFrames;
    int32_t sampleRate;
    int32_t bitsPerSample;
};

static FLAC__StreamDecoderWriteStatus exportFlacWriteCb(
    const FLAC__StreamDecoder *, const FLAC__Frame *frame,
    const FLAC__int32 *const buffer[], void *clientData)
{
    auto *ctx = (FlacExportCtx *)clientData;
    int32_t frames = frame->header.blocksize;
    int32_t ch = frame->header.channels;
    int32_t avail = ctx->maxFrames - ctx->totalFrames;
    if (avail <= 0) return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
    if (frames > avail) frames = avail;
    ctx->channels = ch;
    int32_t bps = frame->header.bits_per_sample;
    float scale = 1.0f / (float)(1LL << (bps - 1));
    for (int32_t i = 0; i < frames; i++) {
        for (int32_t c = 0; c < ch && c < 2; c++) {
            float s = buffer[c][i] * scale;
            if (s > 1.0f) s = 1.0f; else if (s < -1.0f) s = -1.0f;
            ctx->buf[(ctx->totalFrames + i) * 2 + c] = s;
        }
        if (ch == 1)
            ctx->buf[(ctx->totalFrames + i) * 2 + 1] = ctx->buf[(ctx->totalFrames + i) * 2];
    }
    ctx->totalFrames += frames;
    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

static void exportFlacMetadataCb(
    const FLAC__StreamDecoder *, const FLAC__StreamMetadata *metadata, void *clientData)
{
    auto *ctx = (FlacExportCtx *)clientData;
    if (metadata->type == FLAC__METADATA_TYPE_STREAMINFO) {
        ctx->sampleRate = metadata->data.stream_info.sample_rate;
        int64_t total = metadata->data.stream_info.total_samples;
        if (total > 0 && total < 100 * 1000 * 1000)  // cap at ~100M frames (~38 min stereo)
            ctx->maxFrames = (int32_t)total;
    }
}

static inline int32_t decodeFlacFull(const char *path, DecodedAudio &out) {
    FLAC__StreamDecoder *decoder = FLAC__stream_decoder_new();
    if (!decoder) {
        LOGI("decodeFlacFull: FLAC__stream_decoder_new() returned null");
        return -1;
    }

    FlacExportCtx ctx;
    ctx.buf = nullptr;
    ctx.maxFrames = 200 * 1000 * 1000;  // initial cap, updated from metadata
    ctx.channels = 0;
    ctx.totalFrames = 0;
    ctx.sampleRate = 0;
    ctx.bitsPerSample = 0;

    // First pass: get metadata to know total frames
    FLAC__StreamDecoderInitStatus st = FLAC__stream_decoder_init_file(
        decoder, path, exportFlacWriteCb, exportFlacMetadataCb, errorCallback, &ctx);
    if (st != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
        LOGI("decodeFlacFull: init_file FAILED (status=%d) path=%s", (int)st, path);
        FLAC__stream_decoder_delete(decoder);
        return -2;
    }
    FLAC__stream_decoder_process_until_end_of_metadata(decoder);
    FLAC__stream_decoder_finish(decoder);
    FLAC__stream_decoder_delete(decoder);

    if (ctx.totalFrames <= 0 || ctx.sampleRate <= 0) {
        LOGI("decodeFlacFull: metadata parse FAILED (frames=%d sr=%d) path=%s",
             ctx.totalFrames, ctx.sampleRate, path);
        return -3;
    }

    LOGI("decodeFlacFull: metadata OK (frames=%d sr=%d) path=%s",
         ctx.totalFrames, ctx.sampleRate, path);

    // Allocate buffer and decode fully
    ctx.buf = new float[ctx.maxFrames * 2];
    ctx.totalFrames = 0;

    decoder = FLAC__stream_decoder_new();
    st = FLAC__stream_decoder_init_file(
        decoder, path, exportFlacWriteCb, exportFlacMetadataCb, errorCallback, &ctx);
    if (st != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
        LOGI("decodeFlacFull: second init_file FAILED (status=%d) path=%s", (int)st, path);
        delete[] ctx.buf;
        FLAC__stream_decoder_delete(decoder);
        return -4;
    }
    FLAC__stream_decoder_process_until_end_of_metadata(decoder);
    while (ctx.totalFrames < ctx.maxFrames) {
        FLAC__StreamDecoderState ds = FLAC__stream_decoder_get_state(decoder);
        if (ds == FLAC__STREAM_DECODER_END_OF_STREAM) break;
        if (ds == FLAC__STREAM_DECODER_ABORTED) break;
        if (!FLAC__stream_decoder_process_single(decoder)) break;
    }
    FLAC__stream_decoder_finish(decoder);
    FLAC__stream_decoder_delete(decoder);

    out.data = ctx.buf;
    out.frames = ctx.totalFrames;
    out.channels = 2;  // always stereo (mono duped)
    out.sampleRate = ctx.sampleRate;
    return 0;
}

// ─── WAV offline decode ───────────────────────────────────────────────

static inline int32_t decodeWavFull(const char *path, DecodedAudio &out) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    uint8_t riff[12];
    if (fread(riff, 1, 12, f) != 12 ||
        memcmp(riff, "RIFF", 4) != 0 || memcmp(riff + 8, "WAVE", 4) != 0) {
        fclose(f); return -2;
    }

    int32_t channels = 0, sampleRate = 0, bps = 0;
    uint32_t dataSize = 0;
    uint8_t *pcmData = nullptr;
    bool fmtFound = false;

    uint8_t chunk[8];
    while (fread(chunk, 1, 8, f) == 8) {
        uint32_t cs = chunk[4] | (chunk[5] << 8) | (chunk[6] << 16) | (chunk[7] << 24);
        if (memcmp(chunk, "fmt ", 4) == 0) {
            uint8_t fmt[16];
            if (cs < 16 || fread(fmt, 1, 16, f) != 16) { fclose(f); return -3; }
            if ((fmt[0] | (fmt[1] << 8)) != 1) { fclose(f); return -4; }  // PCM only
            channels = fmt[2] | (fmt[3] << 8);
            sampleRate = fmt[4] | (fmt[5] << 8) | (fmt[6] << 16) | (fmt[7] << 24);
            bps = fmt[14] | (fmt[15] << 8);
            fmtFound = true;
            if (cs > 16) fseek(f, cs - 16, SEEK_CUR);
        } else if (memcmp(chunk, "data", 4) == 0) {
            if (!fmtFound) { fclose(f); return -5; }
            dataSize = cs;
            pcmData = new uint8_t[dataSize];
            if (fread(pcmData, 1, dataSize, f) != dataSize) {
                delete[] pcmData; fclose(f); return -6;
            }
            break;
        } else {
            fseek(f, cs, SEEK_CUR);
        }
    }
    fclose(f);
    if (!pcmData) return -7;

    int32_t frameSize = channels * (bps / 8);
    int32_t totalFrames = dataSize / frameSize;
    if (totalFrames <= 0) { delete[] pcmData; return -8; }

    // Convert to interleaved stereo float
    float *buf = new float[totalFrames * 2];
    for (int32_t i = 0; i < totalFrames; i++) {
        for (int32_t c = 0; c < 2; c++) {
            float s = 0.0f;
            if (c < channels) {
                int32_t off = i * frameSize;
                switch (bps) {
                    case 8:
                        s = (pcmData[off + c] - 128) / 128.0f;
                        break;
                    case 16: {
                        int16_t v = pcmData[off + c * 2] | (pcmData[off + c * 2 + 1] << 8);
                        s = v / 32768.0f;
                        break;
                    }
                    case 24: {
                        int32_t v = pcmData[off + c * 3]
                                  | (pcmData[off + c * 3 + 1] << 8)
                                  | (pcmData[off + c * 3 + 2] << 16);
                        if (v & 0x800000) v |= ~0xFFFFFF;
                        s = v / 8388608.0f;
                        break;
                    }
                    case 32: {
                        int32_t v = pcmData[off + c * 4]
                                  | (pcmData[off + c * 4 + 1] << 8)
                                  | (pcmData[off + c * 4 + 2] << 16)
                                  | (pcmData[off + c * 4 + 3] << 24);
                        s = v / 2147483648.0f;
                        break;
                    }
                }
            }
            buf[i * 2 + c] = s;
        }
    }
    delete[] pcmData;

    out.data = buf;
    out.frames = totalFrames;
    out.channels = 2;
    out.sampleRate = sampleRate;
    return 0;
}

// ─── Media (MP3/AAC/OGG/M4A) offline decode ──────────────────────────
// Uses AMediaExtractor + AMediaCodec, same pattern as mediaPlaybackThread.

#if __ANDROID__
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaCodec.h>
#include <fcntl.h>
#include <unistd.h>

static inline int32_t decodeMediaFull(const char *path, DecodedAudio &out) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;

    off64_t fileLen = lseek64(fd, 0, SEEK_END);
    lseek64(fd, 0, SEEK_SET);
    if (fileLen <= 0) { close(fd); return -2; }

    AMediaExtractor *extractor = AMediaExtractor_new();
    AMediaExtractor_setDataSourceFd(extractor, fd, 0, fileLen);

    int32_t audioTrack = -1, sr = 0, ch = 0;
    for (int32_t i = 0; i < AMediaExtractor_getTrackCount(extractor); i++) {
        AMediaFormat *fmt = AMediaExtractor_getTrackFormat(extractor, i);
        const char *m = nullptr;
        AMediaFormat_getString(fmt, AMEDIAFORMAT_KEY_MIME, &m);
        if (m && strncmp(m, "audio/", 6) == 0) {
            audioTrack = i;
            AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_SAMPLE_RATE, &sr);
            AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &ch);
            AMediaFormat_delete(fmt);
            break;
        }
        AMediaFormat_delete(fmt);
    }
    if (audioTrack < 0 || sr <= 0) {
        AMediaExtractor_delete(extractor);
        close(fd);
        return -3;
    }

    AMediaFormat *trackFmt = AMediaExtractor_getTrackFormat(extractor, audioTrack);
    const char *mime = nullptr;
    AMediaFormat_getString(trackFmt, AMEDIAFORMAT_KEY_MIME, &mime);
    AMediaExtractor_selectTrack(extractor, audioTrack);

    AMediaCodec *codec = AMediaCodec_createDecoderByType(mime);
    AMediaCodec_configure(codec, trackFmt, nullptr, nullptr, 0);
    AMediaCodec_start(codec);
    AMediaFormat_delete(trackFmt);

    // Decode entire file to float buffer
    std::vector<float> pcmFloat;
    pcmFloat.reserve(sr * ch * 10);  // pre-allocate ~10 seconds
    bool inputDone = false, outputDone = false;

    while (!outputDone) {
        // Feed input
        if (!inputDone) {
            ssize_t inIdx = AMediaCodec_dequeueInputBuffer(codec, 10000);
            if (inIdx >= 0) {
                size_t inSize;
                uint8_t *inBuf = AMediaCodec_getInputBuffer(codec, inIdx, &inSize);
                ssize_t sampleSize = AMediaExtractor_readSampleData(extractor, inBuf, inSize);
                if (sampleSize < 0) {
                    AMediaCodec_queueInputBuffer(codec, inIdx, 0, 0, 0,
                        AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                    inputDone = true;
                } else {
                    AMediaCodec_queueInputBuffer(codec, inIdx, 0, sampleSize,
                        AMediaExtractor_getSampleTime(extractor), 0);
                    AMediaExtractor_advance(extractor);
                }
            }
        }

        // Drain output
        AMediaCodecBufferInfo info;
        ssize_t outIdx = AMediaCodec_dequeueOutputBuffer(codec, &info, 10000);
        if (outIdx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            AMediaFormat *newFmt = AMediaCodec_getOutputFormat(codec);
            AMediaFormat_getInt32(newFmt, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &ch);
            AMediaFormat_delete(newFmt);
            continue;
        }
        if (outIdx >= 0) {
            if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM)
                outputDone = true;
            if (info.size > 0) {
                size_t outSize;
                uint8_t *outBuf = AMediaCodec_getOutputBuffer(codec, outIdx, &outSize);
                outBuf += info.offset;
                // AMediaCodec outputs 16-bit interleaved PCM
                int32_t totalSamples = info.size / 2;
                for (int32_t i = 0; i < totalSamples; i++) {
                    int16_t vs = outBuf[i * 2] | (outBuf[i * 2 + 1] << 8);
                    pcmFloat.push_back(vs / 32768.0f);
                }
            }
            AMediaCodec_releaseOutputBuffer(codec, outIdx, false);
        }
    }

    AMediaCodec_stop(codec);
    AMediaCodec_delete(codec);
    AMediaExtractor_delete(extractor);
    close(fd);

    if (pcmFloat.empty()) return -4;

    // Convert to stereo float (mono duped)
    int32_t totalSamples = (int32_t)pcmFloat.size();
    int32_t srcCh = ch;
    int32_t totalFrames = totalSamples / srcCh;

    float *buf = new float[totalFrames * 2];
    for (int32_t i = 0; i < totalFrames; i++) {
        for (int32_t c = 0; c < 2; c++) {
            int32_t srcIdx = i * srcCh + (c < srcCh ? c : 0);
            buf[i * 2 + c] = pcmFloat[srcIdx];
        }
    }

    out.data = buf;
    out.frames = totalFrames;
    out.channels = 2;
    out.sampleRate = sr;
    return 0;
}

#else
// Non-Android stub
static inline int32_t decodeMediaFull(const char *, DecodedAudio &) { return -99; }
#endif
