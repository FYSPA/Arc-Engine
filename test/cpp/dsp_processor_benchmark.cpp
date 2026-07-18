#include <cstdio>
#include <chrono>
#include <cmath>
#include "dsp_processor.h"

constexpr int kIterations = 100;

double now_us() {
    auto t = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::micro>(t.time_since_epoch()).count();
}

void benchmark_dsp(const char* name, int frames, int channels, int bands) {
    DspProcessor dsp;
    dsp.init(48000, channels);

    // Enable N bands
    double freqs[] = {31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
    for (int i = 0; i < bands && i < 10; i++) {
        dsp.setBand(i, FilterType::PEAKING, freqs[i], 3.0, 0.707);
    }

    int bufSize = frames * channels;
    float* samples = new float[bufSize];
    for (int i = 0; i < bufSize; i++) samples[i] = sinf(2.0f * M_PI * 440.0f * i / 48000.0f);

    // Warm-up
    for (int i = 0; i < 10; i++) dsp.process(samples, frames, channels);

    // Measure
    double start = now_us();
    for (int i = 0; i < kIterations; i++) {
        dsp.process(samples, frames, channels);
    }
    double elapsed = now_us() - start;

    double avgLatency = elapsed / kIterations;
    double totalSamples = (double)kIterations * frames * channels;
    double throughputMSps = totalSamples / (elapsed / 1e6) / 1e6;

    printf("  %-30s  %3dfr x %dch x %2d bands  |  avg %7.2f µs/call  |  %.2f MS/s\n",
           name, frames, channels, bands, avgLatency, throughputMSps);

    delete[] samples;
}

int main() {
    printf("DspProcessor Latency Benchmark\n");
    printf("==============================\n");
    printf("(%d iterations per test)\n\n", kIterations);
    printf("%-30s  %-27s  %s\n", "Test", "Latency", "Throughput");

    benchmark_dsp("bypassed (no op)", 192, 2, 0);
    benchmark_dsp("1 band", 192, 2, 1);
    benchmark_dsp("5 bands", 192, 2, 5);
    benchmark_dsp("10 bands", 192, 2, 10);
    benchmark_dsp("10 bands, large", 1024, 2, 10);
    benchmark_dsp("10 bands, mono", 192, 1, 10);

    printf("\nTypical AAudio callback: 192 frames @ 48 kHz (4 ms).\n");
    printf("Target: DSP processing should take < 1 ms.\n\n");
    return 0;
}
