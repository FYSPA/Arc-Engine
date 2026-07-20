#include "compressor.h"

Compressor::Compressor() { recalc(); }

void Compressor::setThresholdDb(float db) {
    thresholdDb_ = db < -60.0f ? -60.0f : (db > 0.0f ? 0.0f : db);
    recalc();
}

void Compressor::setRatio(float r) {
    ratio_ = r < 1.0f ? 1.0f : (r > 20.0f ? 20.0f : r);
    recalc();
}

void Compressor::setAttackMs(float ms) {
    attackMs_ = ms < 0.1f ? 0.1f : (ms > 100.0f ? 100.0f : ms);
    recalc();
}

void Compressor::setReleaseMs(float ms) {
    releaseMs_ = ms < 10.0f ? 10.0f : (ms > 1000.0f ? 1000.0f : ms);
    recalc();
}

void Compressor::setKneeDb(float db) {
    kneeDb_ = db < 0.0f ? 0.0f : (db > 12.0f ? 12.0f : db);
    recalc();
}

void Compressor::setMakeupDb(float db) {
    makeupDb_ = db < 0.0f ? 0.0f : (db > 24.0f ? 24.0f : db);
}

void Compressor::recalc() {
    float attackSec = attackMs_ / 1000.0f;
    float releaseSec = releaseMs_ / 1000.0f;
    attackCoeff_ = 1.0f - expf(-1.0f / (attackSec * sampleRate_));
    releaseCoeff_ = 1.0f - expf(-1.0f / (releaseSec * sampleRate_));
}

void Compressor::process(float *samples, int32_t numFrames, int32_t channels) {
    if (!enabled_ || numFrames <= 0) return;

    float slope = 1.0f / ratio_ - 1.0f;  // negative for ratio > 1
    float halfKnee = kneeDb_ * 0.5f;
    float thresholdLo = thresholdDb_ - halfKnee;
    float invTwoKnee = kneeDb_ > 0.0f ? 1.0f / (2.0f * kneeDb_) : 0.0f;

    for (int32_t f = 0; f < numFrames; f++) {
        int32_t base = f * channels;

        // RMS level across all channels for this frame
        float sumSq = 0.0f;
        for (int32_t c = 0; c < channels; c++) {
            float s = samples[base + c];
            sumSq += s * s;
        }
        float rms = sqrtf(sumSq / (float)channels);
        float levelDb = 20.0f * log10f(fmaxf(rms, 1e-10f));

        // Gain computer with soft knee
        float grDb = 0.0f;
        if (levelDb > thresholdLo) {
            if (levelDb < thresholdDb_ + halfKnee && kneeDb_ > 0.0f) {
                // Soft knee zone
                float x = levelDb - thresholdLo;
                grDb = slope * x * x * invTwoKnee;
            } else {
                // Full compression
                grDb = slope * (levelDb - thresholdDb_);
            }
        }

        // Envelope follower (attack / release)
        float coeff = (grDb < envelope_) ? attackCoeff_ : releaseCoeff_;
        envelope_ += coeff * (grDb - envelope_);

        // Convert gain reduction + makeup to linear
        float gainLin = powf(10.0f, (envelope_ + makeupDb_) / 20.0f);

        // Apply
        for (int32_t c = 0; c < channels; c++) {
            samples[base + c] *= gainLin;
        }
    }
}
