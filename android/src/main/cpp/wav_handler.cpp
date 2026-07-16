#include "wav_handler.h"
#include "aaudio_utils.h"
#include "common.h"

#include <cstdio>
#include <cstring>
#include <aaudio/AAudio.h>

int32_t play_wav(const char* path) {
    FILE *f = fopen(path, "rb");
    if (!f) { LOGE("WAV: cannot open"); return -1; }

    uint8_t riff[12];
    if (fread(riff, 1, 12, f) != 12 || memcmp(riff, "RIFF", 4) != 0 || memcmp(riff + 8, "WAVE", 4) != 0) {
        fclose(f); return -2;
    }

    int32_t sampleRate = 0, channels = 0, bitsPerSample = 0;
    uint32_t dataSize = 0;
    uint8_t *pcmData = NULL;
    bool fmtFound = false;

    uint8_t chunk[8];
    while (fread(chunk, 1, 8, f) == 8) {
        uint32_t chunkSize = readInt32LE(chunk + 4);

        if (memcmp(chunk, "fmt ", 4) == 0) {
            uint8_t fmt[16];
            if (chunkSize < 16) { fclose(f); return -3; }
            if (fread(fmt, 1, 16, f) != 16) { fclose(f); return -3; }

            int32_t audioFormat = readInt16LE(fmt);
            channels = readInt16LE(fmt + 2);
            sampleRate = readInt32LE(fmt + 4);
            bitsPerSample = readInt16LE(fmt + 14);
            fmtFound = true;

            if (audioFormat != 1) { LOGE("WAV: only PCM supported, got format %d", audioFormat); fclose(f); return -4; }
            if (chunkSize > 16) fseek(f, chunkSize - 16, SEEK_CUR);

        } else if (memcmp(chunk, "data", 4) == 0) {
            if (!fmtFound) { fclose(f); return -5; }
            dataSize = chunkSize;
            pcmData = new uint8_t[dataSize];
            if (fread(pcmData, 1, dataSize, f) != dataSize) {
                delete[] pcmData; fclose(f); return -6;
            }
            break;

        } else {
            fseek(f, chunkSize, SEEK_CUR);
        }
    }
    fclose(f);

    if (!pcmData) { LOGE("WAV: no data chunk"); return -7; }

    LOGI("WAV: %dHz %dch %dbit %u bytes", sampleRate, channels, bitsPerSample, dataSize);

    AAudioStream *stream = createAAudioStream(sampleRate, channels);
    if (!stream) { delete[] pcmData; return -8; }

    int32_t totalFrames = dataSize / (channels * (bitsPerSample / 8));
    int32_t blockSize = 4096;
    int32_t written = 0;

    while (written < totalFrames) {
        int32_t rem = (totalFrames - written) < blockSize ? (totalFrames - written) : blockSize;
        float floatBuf[rem * channels];

        for (int32_t i = 0; i < rem; i++) {
            for (int32_t ch = 0; ch < channels; ch++) {
                int32_t srcIdx = (written + i) * channels * (bitsPerSample / 8);
                float sample = 0.0f;

                switch (bitsPerSample) {
                    case 8:
                        sample = (pcmData[srcIdx + ch] - 128) / 128.0f;
                        break;
                    case 16: {
                        int16_t s = pcmData[srcIdx + ch * 2] | (pcmData[srcIdx + ch * 2 + 1] << 8);
                        sample = s / 32768.0f;
                        break;
                    }
                    case 24: {
                        int32_t s = pcmData[srcIdx + ch * 3]
                                  | (pcmData[srcIdx + ch * 3 + 1] << 8)
                                  | (pcmData[srcIdx + ch * 3 + 2] << 16);
                        if (s & 0x800000) s |= ~0xFFFFFF;
                        sample = s / 8388608.0f;
                        break;
                    }
                    case 32: {
                        int32_t s = readInt32LE(pcmData + srcIdx + ch * 4);
                        sample = s / 2147483648.0f;
                        break;
                    }
                }
                floatBuf[i * channels + ch] = sample;
            }
        }

        if (writeFrames(stream, floatBuf, rem, channels) != 0) break;
        written += rem;
    }

    closeAAudioStream(stream);
    delete[] pcmData;
    LOGI("WAV playback finished: %d frames", written);
    return 0;
}
