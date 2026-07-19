// ---------------------------------------------------------------------------
// File: ring_buffer.h
// Purpose: Lock-free SPSC ring buffer for interleaved float samples.
//          Fixed capacity of 65536 samples. One producer (decoder thread),
//          one consumer (AAudio callback thread).
// Importance: Core cross-thread audio transport. Used by every track.
// Missing: None
// Known issues: None
// ---------------------------------------------------------------------------

#pragma once

#include <stdint.h>
#include <atomic>
#include <cstring>

class RingBuffer {
public:
    static constexpr size_t kCapacity = 65536;
    static constexpr size_t kMask = kCapacity - 1;

    RingBuffer() : m_buffer(new float[kCapacity]), m_writeIndex(0), m_readIndex(0) {
        memset(m_buffer, 0, kCapacity * sizeof(float));
    }

    ~RingBuffer() {
        delete[] m_buffer;
    }

    // Push interleaved frames. Returns frames actually pushed (<= frames).
    int32_t push(const float *data, int32_t frames, int32_t channels) {
        size_t wi = m_writeIndex.load(std::memory_order_relaxed);
        size_t ri = m_readIndex.load(std::memory_order_acquire);

        size_t used = wi - ri;
        size_t totalSamples = (size_t)frames * channels;

        if (used + totalSamples > kCapacity) {
            if (used >= kCapacity) return 0;
            totalSamples = kCapacity - used;
            frames = (int32_t)(totalSamples / channels);
            if (frames == 0) return 0;
            totalSamples = (size_t)frames * channels;
        }

        for (size_t i = 0; i < totalSamples; i++) {
            m_buffer[(wi + i) & kMask] = data[i];
        }

        m_writeIndex.store(wi + totalSamples, std::memory_order_release);
        return frames;
    }

    // Pop interleaved frames into data. Returns frames actually popped.
    int32_t pop(float *data, int32_t frames, int32_t channels) {
        size_t wi = m_writeIndex.load(std::memory_order_acquire);
        size_t ri = m_readIndex.load(std::memory_order_relaxed);

        size_t used = wi - ri;
        int32_t availFrames = (int32_t)(used / channels);
        int32_t toRead = availFrames < frames ? availFrames : frames;

        size_t totalSamples = (size_t)toRead * channels;
        for (size_t i = 0; i < totalSamples; i++) {
            data[i] = m_buffer[(ri + i) & kMask];
        }

        m_readIndex.store(ri + totalSamples, std::memory_order_release);
        return toRead;
    }

    void reset() {
        m_writeIndex.store(0, std::memory_order_relaxed);
        m_readIndex.store(0, std::memory_order_release);
    }

    int32_t available(int32_t channels) const {
        size_t wi = m_writeIndex.load(std::memory_order_acquire);
        size_t ri = m_readIndex.load(std::memory_order_acquire);
        return (int32_t)((wi - ri) / channels);
    }

    int32_t capacity(int32_t channels) const {
        return (int32_t)(kCapacity / channels);
    }

    static int32_t pacingThreshold(int32_t channels) {
        return (int32_t)(kCapacity / channels * 3 / 4);
    }

private:
    float *m_buffer;
    std::atomic<size_t> m_writeIndex;
    std::atomic<size_t> m_readIndex;

    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;
};
