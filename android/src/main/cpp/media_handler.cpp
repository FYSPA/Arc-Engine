// ---------------------------------------------------------------------------
// File: media_handler.cpp
// Purpose: Legacy playback of compressed audio (MP3, AAC, OGG, M4A) via
//          AMediaCodec + AMediaExtractor with blocking AAudio output.
// Importance: Provides backward-compatible compressed audio playback.
// Missing: None
// Known issues: None
// ---------------------------------------------------------------------------

#include "media_handler.h"
#include "aaudio_utils.h"
#include "common.h"

#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>

int32_t play_media(const char* path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) { LOGE("Media: cannot open %s errno=%d", path, errno); return -1; }

    AMediaExtractor *extractor = AMediaExtractor_new();
    off64_t fileLen = lseek64(fd, 0, SEEK_END);
    lseek64(fd, 0, SEEK_SET);
    media_status_t ms = AMediaExtractor_setDataSourceFd(extractor, fd, 0, fileLen);
    if (ms != AMEDIA_OK) {
        LOGE("Media: setDataSourceFd failed: %d", ms);
        AMediaExtractor_delete(extractor);
        close(fd);
        return -2;
    }

    int32_t audioTrack = -1;
    int32_t sampleRate = 0, channels = 0;

    for (int32_t i = 0; i < AMediaExtractor_getTrackCount(extractor); i++) {
        AMediaFormat *fmt = AMediaExtractor_getTrackFormat(extractor, i);
        const char *m = NULL;
        AMediaFormat_getString(fmt, AMEDIAFORMAT_KEY_MIME, &m);
        if (m && strncmp(m, "audio/", 6) == 0) {
            audioTrack = i;
            AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_SAMPLE_RATE, &sampleRate);
            AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &channels);
            LOGI("Media: track %d %s %dHz %dch", i, m, sampleRate, channels);
            AMediaFormat_delete(fmt);
            break;
        }
        AMediaFormat_delete(fmt);
    }

    if (audioTrack < 0) {
        LOGE("Media: no audio track found");
        AMediaExtractor_delete(extractor);
        close(fd);
        return -3;
    }

    AMediaFormat *trackFmt = AMediaExtractor_getTrackFormat(extractor, audioTrack);
    const char *mime = NULL;
    AMediaFormat_getString(trackFmt, AMEDIAFORMAT_KEY_MIME, &mime);

    AMediaExtractor_selectTrack(extractor, audioTrack);

    AMediaCodec *codec = AMediaCodec_createDecoderByType(mime);
    if (!codec) {
        LOGE("Media: createDecoderByType failed for %s", mime);
        AMediaFormat_delete(trackFmt);
        AMediaExtractor_delete(extractor);
        close(fd);
        return -4;
    }

    ms = AMediaCodec_configure(codec, trackFmt, NULL, NULL, 0);
    AMediaFormat_delete(trackFmt);
    if (ms != AMEDIA_OK) {
        LOGE("Media: configure failed: %d", ms);
        AMediaCodec_delete(codec);
        AMediaExtractor_delete(extractor);
        close(fd);
        return -5;
    }

    ms = AMediaCodec_start(codec);
    if (ms != AMEDIA_OK) {
        LOGE("Media: start failed: %d", ms);
        AMediaCodec_delete(codec);
        AMediaExtractor_delete(extractor);
        close(fd);
        return -6;
    }

    AAudioStream *stream = NULL;
    int32_t outChannels = channels, outSampleRate = sampleRate;
    bool inputDone = false, outputDone = false;
    int32_t ret = 0;

    while (!outputDone) {
        if (!inputDone) {
            ssize_t inIdx = AMediaCodec_dequeueInputBuffer(codec, 10000);
            if (inIdx >= 0) {
                size_t inSize;
                uint8_t *inBuf = AMediaCodec_getInputBuffer(codec, inIdx, &inSize);
                if (inBuf) {
                    ssize_t sampleSize = AMediaExtractor_readSampleData(extractor, inBuf, inSize);
                    if (sampleSize < 0) {
                        AMediaCodec_queueInputBuffer(codec, inIdx, 0, 0, 0, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                        inputDone = true;
                    } else {
                        AMediaCodec_queueInputBuffer(codec, inIdx, 0, sampleSize, AMediaExtractor_getSampleTime(extractor), 0);
                        AMediaExtractor_advance(extractor);
                    }
                }
            }
        }

        AMediaCodecBufferInfo info;
        ssize_t outIdx = AMediaCodec_dequeueOutputBuffer(codec, &info, 10000);

        if (outIdx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            AMediaFormat *newFmt = AMediaCodec_getOutputFormat(codec);
            if (!stream) {
                AMediaFormat_getInt32(newFmt, AMEDIAFORMAT_KEY_SAMPLE_RATE, &outSampleRate);
                AMediaFormat_getInt32(newFmt, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &outChannels);
                stream = createAAudioStream(outSampleRate, outChannels);
                if (!stream) { ret = -7; break; }
                LOGI("Media: output %dHz %dch", outSampleRate, outChannels);
            }
            AMediaFormat_delete(newFmt);
            continue;
        }

        if (outIdx >= 0) {
            if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) outputDone = true;

            if (info.size > 0 && stream) {
                size_t outSize;
                uint8_t *outBuf = AMediaCodec_getOutputBuffer(codec, outIdx, &outSize);
                if (outBuf) {
                    outBuf += info.offset;
                    int32_t totalSamples = info.size / 2;
                    int32_t frames = totalSamples / outChannels;

                    float *floatBuf = new float[totalSamples];
                    for (int32_t i = 0; i < totalSamples; i++) {
                        int16_t s = outBuf[i * 2] | (outBuf[i * 2 + 1] << 8);
                        floatBuf[i] = s / 32768.0f;
                    }

                    ret = writeFrames(stream, floatBuf, frames, outChannels);
                    delete[] floatBuf;
                    if (ret != 0) { ret = -8; outputDone = true; }
                }
            }

            AMediaCodec_releaseOutputBuffer(codec, outIdx, false);
        } else if (outIdx != AMEDIACODEC_INFO_TRY_AGAIN_LATER &&
                   outIdx != AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
            LOGE("Media: dequeueOutputBuffer error: %zd", outIdx);
            ret = -9;
            break;
        }
    }

    if (stream) closeAAudioStream(stream);
    AMediaCodec_stop(codec);
    AMediaCodec_delete(codec);
    AMediaExtractor_delete(extractor);
    close(fd);

    LOGI("Media playback finished: %d", ret);
    return ret;
}
