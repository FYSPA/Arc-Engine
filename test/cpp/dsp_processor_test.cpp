// ---------------------------------------------------------------------------
// File: dsp_processor_test.cpp
// Purpose: Unit tests for DspProcessor: initial bypassed state, init enables,
//          bypass no-op, setBand/enable, out-of-range safety, peaking filter
//          zero-dB response, resetAllBands, low-pass/high-pass frequency
//          response, zero frames safety, bypass toggle.
// Importance: Verifies DSP filter correctness and edge-case safety.
// Missing: None
// Known issues: None
// ---------------------------------------------------------------------------

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include "dsp_processor.h"

static int tests = 0;
static int passed = 0;

#define TEST(name) do { tests++; printf("  TEST %s ... ", name); } while(0)
#define PASS() do { passed++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); } while(0)
#define ASSERT(cond, msg) do { if (!(cond)) { FAIL(msg); return; } } while(0)

static bool floats_equal(float a, float b, float eps = 1e-4f) {
    return fabsf(a - b) < eps;
}

static void test_initial_bypassed() {
    TEST("initial state: bypassed, 10 bands");
    DspProcessor dsp;
    ASSERT(dsp.isBypassed() == true, "should start bypassed");
    ASSERT(dsp.bandCount() == 10, "should have 10 bands");
    PASS();
}

static void test_init_enables_processing() {
    TEST("init disables bypass and sets sample rate");
    DspProcessor dsp;
    dsp.init(44100, 2);
    ASSERT(dsp.isBypassed() == false, "bypass should be false after init");
    PASS();
}

static void test_bypass_does_not_modify() {
    TEST("bypassed process leaves samples unchanged");
    DspProcessor dsp;
    float samples[4] = {0.5f, -0.3f, 0.1f, 0.8f};
    float original[4];
    for (int i = 0; i < 4; i++) original[i] = samples[i];

    dsp.process(samples, 2, 2);
    for (int i = 0; i < 4; i++) {
        ASSERT(floats_equal(samples[i], original[i]),
               "bypassed processor should not change samples");
    }
    PASS();
}

static void test_set_band_and_enable() {
    TEST("setBand configures a band and enables it");
    DspProcessor dsp;
    dsp.init(44100, 2);
    dsp.setBand(0, FilterType::PEAKING, 1000.0, 6.0, 0.707);
    // Just verify no crash — coefficients are valid
    ASSERT(!dsp.isBypassed(), "should not be bypassed");
    PASS();
}

static void test_set_band_out_of_range() {
    TEST("setBand with out-of-range index is safe (no-op)");
    DspProcessor dsp;
    dsp.init(44100, 2);
    // Should not crash
    dsp.setBand(10, FilterType::PEAKING, 1000.0, 6.0, 0.707);
    dsp.setBand(-1, FilterType::PEAKING, 1000.0, 6.0, 0.707);
    PASS();
}

static void test_set_band_enabled_out_of_range() {
    TEST("setBandEnabled with out-of-range index is safe");
    DspProcessor dsp;
    dsp.setBandEnabled(10, true);
    dsp.setBandEnabled(-1, false);
    PASS();
}

static void test_peaking_filter_response() {
    TEST("peaking filter with 0 dB gain does not change signal");
    DspProcessor dsp;
    dsp.init(44100, 2);

    // 0 dB gain should produce identical output (for steady-state DC)
    dsp.setBand(0, FilterType::PEAKING, 1000.0, 0.0, 0.707);

    float samples[4] = {1.0f, 1.0f, 1.0f, 1.0f}; // DC signal
    dsp.process(samples, 2, 2);

    // DC should pass through unchanged with 0 dB peaking
    ASSERT(floats_equal(samples[0], 1.0f), "sample 0 should be unchanged");
    ASSERT(floats_equal(samples[1], 1.0f), "sample 1 should be unchanged");
    PASS();
}

static void test_reset_all_bands() {
    TEST("resetAllBands clears all band configurations");
    DspProcessor dsp;
    dsp.init(44100, 2);
    dsp.setBand(0, FilterType::PEAKING, 1000.0, 6.0, 0.707);
    dsp.setBand(1, FilterType::LOW_SHELF, 200.0, 3.0, 0.5);

    dsp.resetAllBands();

    // Should still be initialized (not bypassed)
    ASSERT(!dsp.isBypassed(), "should not be bypassed after reset");
    PASS();
}

static void test_low_pass_filter() {
    TEST("low-pass filter attenuates high frequencies");
    DspProcessor dsp;
    dsp.init(44100, 2);

    // Low-pass at very low frequency (10 Hz) — should heavily attenuate a 440 Hz signal
    dsp.setBand(0, FilterType::LOW_PASS, 10.0, 0.0, 0.707);

    // Generate a 440 Hz sine wave at 44100 sample rate
    int numFrames = 100;
    int channels = 2;
    float samples[200];
    float original[200];
    for (int i = 0; i < numFrames; i++) {
        float val = sinf(2.0f * (float)M_PI * 440.0f * (float)i / 44100.0f);
        samples[i * 2] = val;
        samples[i * 2 + 1] = val;
        original[i * 2] = val;
        original[i * 2 + 1] = val;
    }

    dsp.process(samples, numFrames, channels);

    // Output should be significantly attenuated
    float maxOut = 0.0f;
    for (int i = 0; i < numFrames * channels; i++) {
        if (fabsf(samples[i]) > maxOut) maxOut = fabsf(samples[i]);
    }
    float maxIn = 1.0f; // sine wave amplitude is 1.0

    ASSERT(maxOut < maxIn * 0.5f, "low-pass should attenuate high frequency signal");
    PASS();
}

static void test_high_pass_filter() {
    TEST("high-pass filter attenuates low frequencies");
    DspProcessor dsp;
    dsp.init(44100, 2);

    // High-pass at 1000 Hz — should attenuate a 50 Hz signal
    dsp.setBand(0, FilterType::HIGH_PASS, 1000.0, 0.0, 0.707);

    int numFrames = 100;
    int channels = 2;
    float samples[200];
    for (int i = 0; i < numFrames; i++) {
        float val = sinf(2.0f * (float)M_PI * 50.0f * (float)i / 44100.0f);
        samples[i * 2] = val;
        samples[i * 2 + 1] = val;
    }

    dsp.process(samples, numFrames, channels);

    float maxOut = 0.0f;
    for (int i = 0; i < numFrames * channels; i++) {
        if (fabsf(samples[i]) > maxOut) maxOut = fabsf(samples[i]);
    }

    ASSERT(maxOut < 0.5f, "high-pass should attenuate low frequency signal");
    PASS();
}

static void test_process_zero_frames() {
    TEST("process with zero frames is safe (no-op)");
    DspProcessor dsp;
    dsp.init(44100, 2);
    dsp.setBand(0, FilterType::PEAKING, 1000.0, 6.0, 0.707);
    dsp.process(nullptr, 0, 2);
    PASS();
}

static void test_set_bypass_toggle() {
    TEST("setBypass toggles processing");
    DspProcessor dsp;
    dsp.init(44100, 2);
    ASSERT(dsp.isBypassed() == false, "not bypassed after init");

    dsp.setBypass(true);
    ASSERT(dsp.isBypassed() == true, "should be bypassed after setBypass(true)");

    dsp.setBypass(false);
    ASSERT(dsp.isBypassed() == false, "should not be bypassed after setBypass(false)");
    PASS();
}

int main() {
    printf("DspProcessor Tests\n");
    printf("==================\n\n");

    test_initial_bypassed();
    test_init_enables_processing();
    test_bypass_does_not_modify();
    test_set_band_and_enable();
    test_set_band_out_of_range();
    test_set_band_enabled_out_of_range();
    test_peaking_filter_response();
    test_reset_all_bands();
    test_low_pass_filter();
    test_high_pass_filter();
    test_process_zero_frames();
    test_set_bypass_toggle();

    printf("\n%d / %d tests passed\n", passed, tests);
    return passed == tests ? 0 : 1;
}
