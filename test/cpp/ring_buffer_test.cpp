// ---------------------------------------------------------------------------
// File: ring_buffer_test.cpp
// Purpose: Unit tests for RingBuffer: initial state, mono/stereo push/pop,
//          wrap-around at boundary, buffer full/empty/partial push, reset,
//          pacing threshold, interleaved stereo integrity.
// Importance: Verifies lock-free SPSC ring buffer correctness.
// Missing: None
// Known issues: None
// ---------------------------------------------------------------------------

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include "ring_buffer.h"

static int tests = 0;
static int passed = 0;

#define TEST(name) do { tests++; printf("  TEST %s ... ", name); } while(0)
#define PASS() do { passed++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); } while(0)
#define ASSERT(cond, msg) do { if (!(cond)) { FAIL(msg); return; } } while(0)

static void test_initial_state() {
    TEST("initial state: empty, zero available");
    RingBuffer rb;
    ASSERT(rb.available(1) == 0, "should be empty for mono");
    ASSERT(rb.available(2) == 0, "should be empty for stereo");
    ASSERT(rb.capacity(1) == 65536, "mono capacity should be 65536");
    ASSERT(rb.capacity(2) == 32768, "stereo capacity should be 32768");
    PASS();
}

static void test_push_pop_mono() {
    TEST("push and pop 100 mono frames");
    RingBuffer rb;
    float in[100];
    float out[100] = {0};
    for (int i = 0; i < 100; i++) in[i] = (float)i * 0.5f;

    int pushed = rb.push(in, 100, 1);
    ASSERT(pushed == 100, "should push all 100 frames");

    int avail = rb.available(1);
    ASSERT(avail == 100, "available should be 100 after push");

    int popped = rb.pop(out, 100, 1);
    ASSERT(popped == 100, "should pop all 100 frames");

    for (int i = 0; i < 100; i++) {
        ASSERT(fabsf(out[i] - in[i]) < 1e-6f, "data should match after pop");
    }

    ASSERT(rb.available(1) == 0, "should be empty after pop");
    PASS();
}

static void test_push_pop_stereo() {
    TEST("push and pop 50 stereo frames");
    RingBuffer rb;
    float in[100]; // 50 frames * 2 channels
    float out[100] = {0};
    for (int i = 0; i < 100; i++) in[i] = (float)i;

    int pushed = rb.push(in, 50, 2);
    ASSERT(pushed == 50, "should push all 50 stereo frames");

    int popped = rb.pop(out, 50, 2);
    ASSERT(popped == 50, "should pop all 50 stereo frames");

    for (int i = 0; i < 100; i++) {
        ASSERT(fabsf(out[i] - in[i]) < 1e-6f, "stereo data should match");
    }
    PASS();
}

static void test_wrap_around() {
    TEST("wrap-around at buffer boundary");
    RingBuffer rb;
    float in[32768];
    float out[32768] = {0};
    for (int i = 0; i < 32768; i++) in[i] = 1.0f;

    // Fill buffer almost full (mono)
    int pushed = rb.push(in, 32768, 1);
    ASSERT(pushed == 32768, "should push 32768 frames");

    // Pop half
    int popped = rb.pop(out, 16384, 1);
    ASSERT(popped == 16384, "should pop 16384 frames");

    // Push more — this should wrap around ring buffer
    for (int i = 0; i < 16384; i++) in[i] = 2.0f;
    pushed = rb.push(in, 16384, 1);
    ASSERT(pushed == 16384, "should push 16384 more frames (wrap)");

    // Pop remaining
    popped = rb.pop(out, 32768, 1);
    ASSERT(popped == 32768, "should pop all remaining 32768 frames");

    // First 16384 should be 1.0, next 16384 should be 2.0
    for (int i = 0; i < 16384; i++) {
        ASSERT(fabsf(out[i] - 1.0f) < 1e-6f, "first half should be 1.0");
    }
    for (int i = 16384; i < 32768; i++) {
        ASSERT(fabsf(out[i] - 2.0f) < 1e-6f, "second half should be 2.0");
    }
    PASS();
}

static void test_buffer_full() {
    TEST("push returns 0 when buffer is full");
    RingBuffer rb;
    float in[32768];
    for (int i = 0; i < 32768; i++) in[i] = 1.0f;

    // Fill completely (mono capacity = 65536 frames)
    int pushed = rb.push(in, 32768, 1);
    ASSERT(pushed == 32768, "first push should succeed");

    pushed = rb.push(in, 32768, 1);
    ASSERT(pushed == 32768, "second push should succeed");

    // Buffer should be full now
    pushed = rb.push(in, 1, 1);
    ASSERT(pushed == 0, "push to full buffer should return 0");
    PASS();
}

static void test_partial_push() {
    TEST("partial push when buffer is nearly full (truncates)");
    RingBuffer rb;
    float in[32768];
    for (int i = 0; i < 32768; i++) in[i] = 1.0f;

    // Fill with 65535 samples (65536 - 1)
    int pushed = rb.push(in, 32768, 1);
    pushed = rb.push(in, 32767, 1);
    // 65535 samples used, 1 remaining
    ASSERT(pushed == 32767, "second push should succeed");

    // Try to push 100 more, should only fit 1
    float small[100];
    pushed = rb.push(small, 100, 1);
    ASSERT(pushed == 1, "should only push 1 frame (remaining space)");
    PASS();
}

static void test_pop_empty() {
    TEST("pop returns 0 when buffer is empty");
    RingBuffer rb;
    float out[10];
    int popped = rb.pop(out, 10, 1);
    ASSERT(popped == 0, "pop from empty buffer should return 0");
    PASS();
}

static void test_reset() {
    TEST("reset clears all data");
    RingBuffer rb;
    float in[100];
    for (int i = 0; i < 100; i++) in[i] = 1.0f;

    rb.push(in, 100, 1);
    ASSERT(rb.available(1) == 100, "available after push");

    rb.reset();
    ASSERT(rb.available(1) == 0, "available after reset should be 0");

    // Should be usable after reset
    int pushed = rb.push(in, 50, 1);
    ASSERT(pushed == 50, "should push after reset");
    PASS();
}

static void test_pacing_threshold() {
    TEST("pacingThreshold is 75% of capacity");
    ASSERT(RingBuffer::pacingThreshold(1) == 49152, "mono threshold should be 49152");
    ASSERT(RingBuffer::pacingThreshold(2) == 24576, "stereo threshold should be 24576");
    PASS();
}

static void test_interleaved_stereo() {
    TEST("interleaved stereo data integrity");
    RingBuffer rb;
    float in[100];
    float out[100] = {0};

    // Create interleaved stereo: L0,R0, L1,R1, ...
    for (int i = 0; i < 50; i++) {
        in[i * 2]     = (float)i;       // left
        in[i * 2 + 1] = (float)(-i);    // right
    }

    int pushed = rb.push(in, 50, 2);
    ASSERT(pushed == 50, "should push 50 stereo frames");

    int popped = rb.pop(out, 50, 2);
    ASSERT(popped == 50, "should pop 50 stereo frames");

    for (int i = 0; i < 50; i++) {
        ASSERT(fabsf(out[i * 2] - (float)i) < 1e-6f, "left channel should match");
        ASSERT(fabsf(out[i * 2 + 1] - (float)(-i)) < 1e-6f, "right channel should match");
    }
    PASS();
}

int main() {
    printf("RingBuffer Tests\n");
    printf("================\n\n");

    test_initial_state();
    test_push_pop_mono();
    test_push_pop_stereo();
    test_wrap_around();
    test_buffer_full();
    test_partial_push();
    test_pop_empty();
    test_reset();
    test_pacing_threshold();
    test_interleaved_stereo();

    printf("\n%d / %d tests passed\n", passed, tests);
    return passed == tests ? 0 : 1;
}
