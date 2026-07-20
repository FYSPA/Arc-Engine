#include "reverb.h"
#include <algorithm>
#include <cstring>

// ─── DelayLine ────────────────────────────────────────────────────────────────

void Reverb::DelayLine::resize(int n) {
    data.resize(n, 0.0f);
    size = n;
    writePos = 0;
}

float Reverb::DelayLine::read(int delay) const {
    int idx = writePos - delay;
    if (idx < 0) idx += size;
    return data[idx];
}

void Reverb::DelayLine::write(float v) {
    data[writePos] = v;
    if (++writePos >= size) writePos = 0;
}

void Reverb::DelayLine::zap() {
    std::fill(data.begin(), data.end(), 0.0f);
    writePos = 0;
}

// ─── CombFilter ───────────────────────────────────────────────────────────────

float Reverb::CombFilter::process(float input, float dampCoeff) {
    float read = delay.read(delaySamples);
    dampZ = read * (1.0f - dampCoeff) + dampZ * dampCoeff;
    delay.write(input + gain * dampZ);
    return read;
}

// ─── AllPassFilter ────────────────────────────────────────────────────────────

float Reverb::AllPassFilter::process(float input) {
    float read = delay.read(delaySamples);
    delay.write(input + gain * read);
    return read - gain * input;
}

// ─── PreDelay ─────────────────────────────────────────────────────────────────

float Reverb::PreDelay::process(float input) {
    delay.write(input);
    if (samples > 0) return delay.read(samples);
    return input;
}

// ─── Reverb ───────────────────────────────────────────────────────────────────

Reverb::Reverb() {
    baseCombDelays_[0] = 1493;
    baseCombDelays_[1] = 1790;
    baseCombDelays_[2] = 1973;
    baseCombDelays_[3] = 2098;

    baseAllPassDelays_[0] = 240;
    baseAllPassDelays_[1] = 82;

    recalc();
}

void Reverb::init(float sampleRate) {
    sampleRate_ = sampleRate;
    recalc();
}

void Reverb::reset() {
    for (int i = 0; i < kNumCombs; i++) {
        combs_[i].delay.zap();
        combs_[i].dampZ = 0.0f;
    }
    for (int i = 0; i < kNumAllPasses; i++) {
        allPasses_[i].delay.zap();
    }
    preDelay_.delay.zap();
}

void Reverb::setMix(float v) {
    mix_ = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

void Reverb::setDecay(float v) {
    decay_ = v < 0.1f ? 0.1f : (v > 10.0f ? 10.0f : v);
    recalc();
}

void Reverb::setRoomSize(float v) {
    roomSize_ = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    recalc();
}

void Reverb::setDamping(float v) {
    damping_ = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    recalc();
}

void Reverb::setPreDelayMs(float v) {
    preDelayMs_ = v < 0.0f ? 0.0f : (v > 200.0f ? 200.0f : v);
    if (sampleRate_ > 0) {
        preDelay_.samples = (int)(preDelayMs_ * sampleRate_ / 1000.0f);
        preDelay_.delay.resize(preDelay_.samples + 1);
    }
}

void Reverb::recalc() {
    if (sampleRate_ <= 0) return;

    float scale = 0.25f + roomSize_ * 0.75f;
    float invSampleRate = 1.0f / sampleRate_;

    for (int i = 0; i < kNumCombs; i++) {
        int ds = (int)(baseCombDelays_[i] * scale);
        if (ds < 2) ds = 2;
        combs_[i].delaySamples = ds;
        combs_[i].delay.resize(ds + 1);
        combs_[i].gain = powf(0.001f, (float)ds * invSampleRate / decay_);
    }

    for (int i = 0; i < kNumAllPasses; i++) {
        int ds = (int)(baseAllPassDelays_[i] * scale);
        if (ds < 2) ds = 2;
        allPasses_[i].delaySamples = ds;
        allPasses_[i].delay.resize(ds + 1);
        allPasses_[i].gain = 0.7f;
    }

    // One-pole LP damping coefficient: maps 0..1 → no damping..max damping
    dampCoeff_ = damping_;

    preDelay_.samples = (int)(preDelayMs_ * sampleRate_ / 1000.0f);
    preDelay_.delay.resize(preDelay_.samples + 1);
}

void Reverb::process(float *samples, int32_t numFrames, int32_t channels) {
    if (!enabled_ || numFrames <= 0 || channels <= 0) return;

    // 1. Sum to mono
    float *mono = new float[numFrames];
    for (int32_t f = 0; f < numFrames; f++) {
        float sum = 0.0f;
        int base = f * channels;
        for (int32_t c = 0; c < channels; c++) {
            sum += samples[base + c];
        }
        mono[f] = sum / (float)channels;
    }

    // 2. Process mono through reverb
    for (int32_t f = 0; f < numFrames; f++) {
        float wet = preDelay_.process(mono[f]);

        float sum = 0.0f;
        for (int i = 0; i < kNumCombs; i++) {
            sum += combs_[i].process(wet, dampCoeff_);
        }
        sum /= (float)kNumCombs;

        for (int i = 0; i < kNumAllPasses; i++) {
            sum = allPasses_[i].process(sum);
        }

        mono[f] = sum;
    }

    // 3. Wet/dry mix
    float dryGain = 1.0f - mix_;
    float wetGain = mix_;
    for (int32_t f = 0; f < numFrames; f++) {
        float wet = mono[f];
        int base = f * channels;
        for (int32_t c = 0; c < channels; c++) {
            samples[base + c] = samples[base + c] * dryGain + wet * wetGain;
        }
    }

    delete[] mono;
}
