#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>

// WAV file writer for offline export.
// Writes 24-bit PCM WAV files with proper RIFF headers.

struct WavWriter {
    FILE *f{nullptr};
    int32_t channels{0};
    int32_t sampleRate{0};
    int32_t bitsPerSample{24};
    int64_t dataBytes{0};

    bool open(const char *path, int32_t ch, int32_t sr, int32_t bps = 24) {
        f = fopen(path, "wb");
        if (!f) return false;
        channels = ch;
        sampleRate = sr;
        bitsPerSample = bps;
        dataBytes = 0;

        // Write placeholder RIFF header (44 bytes)
        uint8_t header[44] = {};
        // ChunkID "RIFF"
        memcpy(header, "RIFF", 4);
        // ChunkSize = 36 + dataSize (will patch later)
        // Format "WAVE"
        memcpy(header + 8, "WAVE", 4);
        // Subchunk1ID "fmt "
        memcpy(header + 12, "fmt ", 4);
        // Subchunk1Size = 16 (PCM)
        header[16] = 16;
        // AudioFormat = 1 (PCM)
        header[20] = 1;
        // NumChannels
        header[22] = (uint8_t)(ch & 0xFF);
        header[23] = (uint8_t)((ch >> 8) & 0xFF);
        // SampleRate
        header[24] = (uint8_t)(sr & 0xFF);
        header[25] = (uint8_t)((sr >> 8) & 0xFF);
        header[26] = (uint8_t)((sr >> 16) & 0xFF);
        header[27] = (uint8_t)((sr >> 24) & 0xFF);
        // ByteRate = SampleRate * NumChannels * BitsPerSample/8
        int32_t byteRate = sr * ch * (bps / 8);
        header[28] = (uint8_t)(byteRate & 0xFF);
        header[29] = (uint8_t)((byteRate >> 8) & 0xFF);
        header[30] = (uint8_t)((byteRate >> 16) & 0xFF);
        header[31] = (uint8_t)((byteRate >> 24) & 0xFF);
        // BlockAlign = NumChannels * BitsPerSample/8
        int16_t blockAlign = (int16_t)(ch * (bps / 8));
        header[32] = (uint8_t)(blockAlign & 0xFF);
        header[33] = (uint8_t)((blockAlign >> 8) & 0xFF);
        // BitsPerSample
        header[34] = (uint8_t)(bps & 0xFF);
        header[35] = (uint8_t)((bps >> 8) & 0xFF);
        // Subchunk2ID "data"
        memcpy(header + 36, "data", 4);
        // Subchunk2Size = 0 (will patch)
        // header[40..43] already 0

        fwrite(header, 1, 44, f);
        return true;
    }

    // Write interleaved float samples as 24-bit PCM.
    // data: interleaved float samples [-1.0, 1.0]
    // frames: number of frames (samples = frames * channels)
    void writeBlock(const float *data, int32_t frames) {
        if (!f || frames <= 0) return;
        int32_t totalSamples = frames * channels;
        uint8_t buf[3];
        for (int32_t i = 0; i < totalSamples; i++) {
            float s = data[i];
            // Clamp to [-1, 1]
            if (s > 1.0f) s = 1.0f;
            else if (s < -1.0f) s = -1.0f;
            // Convert to 24-bit signed integer
            int32_t v = (int32_t)(s * 8388607.0f);
            if (v > 8388607) v = 8388607;
            else if (v < -8388608) v = -8388608;
            // Write 3 bytes little-endian
            buf[0] = (uint8_t)(v & 0xFF);
            buf[1] = (uint8_t)((v >> 8) & 0xFF);
            buf[2] = (uint8_t)((v >> 16) & 0xFF);
            fwrite(buf, 1, 3, f);
        }
        dataBytes += (int64_t)frames * channels * 3;
    }

    // Patch RIFF header with final sizes and close.
    void close() {
        if (!f) return;
        // Patch ChunkSize (offset 4): 36 + dataBytes
        int32_t chunkSize = (int32_t)(36 + dataBytes);
        fseek(f, 4, SEEK_SET);
        uint8_t buf[4];
        buf[0] = (uint8_t)(chunkSize & 0xFF);
        buf[1] = (uint8_t)((chunkSize >> 8) & 0xFF);
        buf[2] = (uint8_t)((chunkSize >> 16) & 0xFF);
        buf[3] = (uint8_t)((chunkSize >> 24) & 0xFF);
        fwrite(buf, 1, 4, f);
        // Patch Subchunk2Size (offset 40): dataBytes
        fseek(f, 40, SEEK_SET);
        buf[0] = (uint8_t)(dataBytes & 0xFF);
        buf[1] = (uint8_t)((dataBytes >> 8) & 0xFF);
        buf[2] = (uint8_t)((dataBytes >> 16) & 0xFF);
        buf[3] = (uint8_t)((dataBytes >> 24) & 0xFF);
        fwrite(buf, 1, 4, f);
        fclose(f);
        f = nullptr;
    }
};
