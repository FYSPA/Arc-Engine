// ---------------------------------------------------------------------------
// File: ring_buffer_benchmark.cpp
// Purpose: Latency benchmark for RingBuffer. Measures push+pop latency and
//          throughput at various frame/channel sizes (including AAudio
//          callback size: 192 stereo frames).
// Importance: Validates RingBuffer performance budget (~14 µs for 192fr).
// Missing: None
// Known issues: None
// ---------------------------------------------------------------------------

#include <cstdio>
#include <chrono>
#include "ring_buffer.h"

constexpr int kIterations = 1000;

double now_us() {
    auto t = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::micro>(t.time_since_epoch()).count();
}

void benchmark_push_pop(const char* name, int frames, int channels) {
    RingBuffer rb;
    int bufSize = frames * channels;
    float* data = new float[bufSize];
    float* out = new float[bufSize];
    for (int i = 0; i < bufSize; i++) data[i] = 0.5f;

    // Warm-up
    for (int i = 0; i < 100; i++) {
        rb.push(data, frames, channels);
        rb.pop(out, frames, channels);
    }
    rb.reset();

    // Measure
    double start = now_us();
    for (int i = 0; i < kIterations; i++) {
        rb.push(data, frames, channels);
        rb.pop(out, frames, channels);
    }
    double elapsed = now_us() - start;

    double avgLatency = elapsed / (kIterations * 2); // push + pop
    double totalBytes = (double)kIterations * frames * channels * sizeof(float);
    double throughputMBs = totalBytes / (elapsed / 1e6) / (1024.0 * 1024.0);

    printf("  %-30s  %5d frames x %d ch  |  avg %.2f µs/op  |  %.1f MB/s\n",
           name, frames, channels, avgLatency, throughputMBs);

    delete[] data;
    delete[] out;
}

int main() {
    printf("RingBuffer Latency Benchmark\n");
    printf("============================\n");
    printf("(push + pop pair, %d iterations each)\n\n", kIterations);
    printf("%-30s  %-24s  %s\n", "Test", "Latency", "Throughput");

    benchmark_push_pop("small mono", 64, 1);
    benchmark_push_pop("small stereo", 64, 2);
    benchmark_push_pop("medium mono", 256, 1);
    benchmark_push_pop("medium stereo", 256, 2);
    benchmark_push_pop("AAudio callback (192fr)", 192, 2);
    benchmark_push_pop("large mono", 1024, 1);
    benchmark_push_pop("large stereo", 1024, 2);

    printf("\nNote: RingBuffer wraps at 65536 samples (32768 stereo frames).\n");
    printf("AAudio callback typically requests 192 frames at 48 kHz.\n\n");
    return 0;
}
