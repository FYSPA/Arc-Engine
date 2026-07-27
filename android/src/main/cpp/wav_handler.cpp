// ---------------------------------------------------------------------------
// File: wav_handler.cpp
// Purpose: Legacy WAV playback via blocking AAudio stream. Parses RIFF
//          header, converts PCM data to float, writes to AAudio.
// Importance: Provides backward-compatible WAV playback.
// Missing: None
// Known issues: None
// ---------------------------------------------------------------------------

#include "wav_handler.h"
#include "aaudio_utils.h"
#include "common.h"

#include <cstdio>
#include <vector>
#include <cstring>
#include <aaudio/AAudio.h>

int32_t getWavFormat(const char *path, int32_t &outSampleRate, int32_t &outChannels) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    uint8_t riff[12];
    if (fread(riff, 1, 12, f) != 12 || memcmp(riff, "RIFF", 4) != 0 || memcmp(riff + 8, "WAVE", 4) != 0) {
        fclose(f); return -2;
    }

    uint8_t chunk[8];
    while (fread(chunk, 1, 8, f) == 8) {
        uint32_t cs = readInt32LE(chunk + 4);
        if (memcmp(chunk, "fmt ", 4) == 0) {
            uint8_t fmt[16];
            if (cs < 16 || fread(fmt, 1, 16, f) != 16) { fclose(f); return -3; }
            if ((fmt[0] | (fmt[1] << 8)) != 1) { fclose(f); return -4; }
            outChannels = fmt[2] | (fmt[3] << 8);
            outSampleRate = readInt32LE(fmt + 4);
            fclose(f);
            return 0;
        }
        fseek(f, cs, SEEK_CUR);
    }
    fclose(f);
    return -5;
}

int32_t loadWavIntoState(TrackState &trk, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    uint8_t riff[12];
    if (fread(riff, 1, 12, f) != 12 || memcmp(riff, "RIFF", 4) != 0 || memcmp(riff + 8, "WAVE", 4) != 0) {
        fclose(f); return -2;
    }

    int32_t bps = 0;
    bool fmtFound = false;
    uint32_t dataSize = 0;
    uint8_t *pcmData = nullptr;

    uint8_t chunk[8];
    while (fread(chunk, 1, 8, f) == 8) {
        uint32_t cs = readInt32LE(chunk + 4);
        if (memcmp(chunk, "fmt ", 4) == 0) {
            uint8_t fmt[16];
            if (cs < 16 || fread(fmt, 1, 16, f) != 16) { fclose(f); return -3; }
            if ((fmt[0] | (fmt[1] << 8)) != 1) { fclose(f); return -4; }
            trk.channels = fmt[2] | (fmt[3] << 8);
            trk.sampleRate = readInt32LE(fmt + 4);
            bps = readInt16LE(fmt + 14);
            trk.bitsPerSample = bps;
            fmtFound = true;
            if (cs > 16) fseek(f, cs - 16, SEEK_CUR);
        } else if (memcmp(chunk, "data", 4) == 0) {
            if (!fmtFound) { fclose(f); return -5; }
            dataSize = cs;
            pcmData = new uint8_t[cs];
            if (fread(pcmData, 1, cs, f) != cs) {
                delete[] pcmData; fclose(f); return -6;
            }
            break;
        } else {
            fseek(f, cs, SEEK_CUR);
        }
    }
    fclose(f);

    if (!pcmData) return -7;

    // Replace old WAV data
    if (trk.wavData) delete[] trk.wavData;
    trk.wavData = pcmData;
    trk.wavDataSize = dataSize;
    trk.wavFrameSize = trk.channels * (bps / 8);
    trk.totalFrames = dataSize / trk.wavFrameSize;
    trk.writtenFrames = 0;
    return 0;
}

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
        std::vector<float> floatBuf(rem * channels);

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

        if (writeFrames(stream, floatBuf.data(), rem, channels) != 0) break;
        written += rem;
    }

    closeAAudioStream(stream);
    delete[] pcmData;
    LOGI("WAV playback finished: %d frames", written);
    return 0;
}

// ─── analyzeWavWaveform ──────────────────────────────────────────────────

int32_t analyzeWavWaveform(const char *path, int32_t numBars, float *outPeaks) {
    if (numBars <= 0 || numBars > 512 || !outPeaks) return -1;
    memset(outPeaks, 0, numBars * sizeof(float));

    FILE *f = fopen(path, "rb");
    if (!f) return -2;

    uint8_t riff[12];
    if (fread(riff, 1, 12, f) != 12 || memcmp(riff, "RIFF", 4) != 0 || memcmp(riff + 8, "WAVE", 4) != 0) {
        fclose(f); return -3;
    }

    int32_t channels = 0, bitsPerSample = 0;
    uint32_t dataSize = 0;
    uint8_t *pcmData = nullptr;
    bool fmtFound = false;

    uint8_t chunk[8];
    while (fread(chunk, 1, 8, f) == 8) {
        uint32_t cs = readInt32LE(chunk + 4);
        if (memcmp(chunk, "fmt ", 4) == 0) {
            uint8_t fmt[16];
            if (cs < 16 || fread(fmt, 1, 16, f) != 16) { fclose(f); return -4; }
            if ((fmt[0] | (fmt[1] << 8)) != 1) { fclose(f); return -5; }
            channels = fmt[2] | (fmt[3] << 8);
            bitsPerSample = readInt16LE(fmt + 14);
            fmtFound = true;
            if (cs > 16) fseek(f, cs - 16, SEEK_CUR);
        } else if (memcmp(chunk, "data", 4) == 0) {
            if (!fmtFound) { fclose(f); return -6; }
            dataSize = cs;
            pcmData = new uint8_t[cs];
            if (fread(pcmData, 1, cs, f) != cs) { delete[] pcmData; fclose(f); return -7; }
            break;
        } else {
            fseek(f, cs, SEEK_CUR);
        }
    }
    fclose(f);

    if (!pcmData || dataSize == 0 || channels == 0) return -8;

    int32_t bytesPerSample = bitsPerSample / 8;
    int32_t frameSize = channels * bytesPerSample;
    int64_t totalFrames = dataSize / frameSize;
    int64_t framesPerBar = totalFrames / numBars;
    if (framesPerBar == 0) { delete[] pcmData; return -9; }

    float scale = 1.0f / (float)(1LL << (bitsPerSample - 1));

    for (int32_t bar = 0; bar < numBars; bar++) {
        int64_t startFrame = (int64_t)bar * framesPerBar;
        int64_t endFrame = startFrame + framesPerBar;
        if (endFrame > totalFrames) endFrame = totalFrames;

        float maxPeak = 0.0f;
        for (int64_t frame = startFrame; frame < endFrame; frame++) {
            uint32_t offset = (uint32_t)(frame * frameSize);
            for (int c = 0; c < channels; c++) {
                float sample = 0.0f;
                switch (bitsPerSample) {
                    case 8:
                        sample = (pcmData[offset + c] - 128) / 128.0f;
                        break;
                    case 16: {
                        int16_t s = pcmData[offset + c * 2] | (pcmData[offset + c * 2 + 1] << 8);
                        sample = s / 32768.0f;
                        break;
                    }
                    case 24: {
                        int32_t s = pcmData[offset + c * 3]
                                  | (pcmData[offset + c * 3 + 1] << 8)
                                  | (pcmData[offset + c * 3 + 2] << 16);
                        if (s & 0x800000) s |= ~0xFFFFFF;
                        sample = s / 8388608.0f;
                        break;
                    }
                    case 32: {
                        int32_t s = readInt32LE(pcmData + offset + c * 4);
                        sample = s / 2147483648.0f;
                        break;
                    }
                }
                float absSample = fabsf(sample);
                if (absSample > maxPeak) maxPeak = absSample;
            }
        }
        outPeaks[bar] = maxPeak;
    }

    delete[] pcmData;

    // Normalize: find global max and scale to 1.0
    float globalMax = 0.0f;
    for (int32_t i = 0; i < numBars; i++) {
        if (outPeaks[i] > globalMax) globalMax = outPeaks[i];
    }
    if (globalMax > 0.0f && globalMax < 1.0f) {
        float invMax = 1.0f / globalMax;
        for (int32_t i = 0; i < numBars; i++)
            outPeaks[i] *= invMax;
    }

    for (int32_t i = 0; i < numBars; i++) {
        if (outPeaks[i] < 0.02f) outPeaks[i] = 0.02f;
    }

    return 0;
}
