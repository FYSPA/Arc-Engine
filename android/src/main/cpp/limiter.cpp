#include "limiter.h"
#include <cmath>
#include <algorithm>

Limiter::Limiter()
    : enabled_(true)
    , thresholdDb_(-0.5f)
    , thresholdLin_(0.944f)
    , ceilingLin_(1.0f)
    , attackMs_(5.0f)
    , releaseMs_(50.0f)
    , attackCoeff_(0.0f)
    , releaseCoeff_(0.0f)
    , lookAheadMs_(2.9f)
    , lookAheadSamples_(128)
    , envelope_(0.0f)
    , laWritePos_(0)
    , laReadPos_(0)
    , laCount_(0)
    , laChannels_(2)
{
    memset(laBuffer_, 0, sizeof(laBuffer_));
    recalcThreshold();
    recalcCoefficients();
}

Limiter::~Limiter() {}

void Limiter::reset() {
    envelope_ = 0.0f;
    laWritePos_ = 0;
    laReadPos_ = 0;
    laCount_ = 0;
    memset(laBuffer_, 0, sizeof(laBuffer_));
}

void Limiter::setEnabled(bool enabled) {
    enabled_ = enabled;
    if (!enabled) reset();
}

void Limiter::setThresholdDb(float db) {
    thresholdDb_ = db < -60.0f ? -60.0f : (db > 0.0f ? 0.0f : db);
    recalcThreshold();
}

void Limiter::setAttackMs(float ms) {
    attackMs_ = ms < 0.1f ? 0.1f : (ms > 100.0f ? 100.0f : ms);
    recalcCoefficients();
}

void Limiter::setReleaseMs(float ms) {
    releaseMs_ = ms < 10.0f ? 10.0f : (ms > 1000.0f ? 1000.0f : ms);
    recalcCoefficients();
}

void Limiter::setLookAheadMs(float ms) {
    float clamped = ms < 0.0f ? 0.0f : (ms > 20.0f ? 20.0f : ms);
    lookAheadMs_ = clamped;
    lookAheadSamples_ = (int32_t)(clamped * 44100.0f / 1000.0f);
    if (lookAheadSamples_ > kMaxLookAhead) lookAheadSamples_ = kMaxLookAhead;
    reset();
}

void Limiter::recalcThreshold() {
    thresholdLin_ = powf(10.0f, thresholdDb_ / 20.0f);
    if (thresholdLin_ > ceilingLin_) thresholdLin_ = ceilingLin_;
}

void Limiter::recalcCoefficients() {
    const float sr = 44100.0f;
    attackCoeff_  = 1.0f - expf(-1.0f / (attackMs_ * sr / 1000.0f));
    releaseCoeff_ = 1.0f - expf(-1.0f / (releaseMs_ * sr / 1000.0f));
}

void Limiter::process(float *samples, int32_t numFrames, int32_t channels) {
    if (!enabled_ || numFrames <= 0) return;

    laChannels_ = channels;
    if (channels > 2) channels = 2;

    float thr = thresholdLin_;
    float env = envelope_;
    float aCoeff = attackCoeff_;
    float rCoeff = releaseCoeff_;
    int32_t laSamples = lookAheadSamples_;

    for (int32_t f = 0; f < numFrames; f++) {
        // Write current frame to LA buffer
        if (laSamples > 0) {
            for (int32_t c = 0; c < channels; c++) {
                laBuffer_[(laWritePos_ * channels) + c] = samples[f * channels + c];
            }
            laWritePos_ = (laWritePos_ + 1) % laSamples;
            if (laCount_ < laSamples) laCount_++;

            // Read the delayed frame (look-ahead)
            int32_t readIdx = laReadPos_ * channels;
            laReadPos_ = (laReadPos_ + 1) % laSamples;

            // Detect peak from delayed frame
            float peak = 0.0f;
            for (int32_t c = 0; c < channels; c++) {
                float a = fabsf(laBuffer_[readIdx + c]);
                if (a > peak) peak = a;
            }

            // Envelope follower
            if (peak > env) {
                env += aCoeff * (peak - env);
            } else {
                env += rCoeff * (peak - env);
            }

            // Compute gain reduction and apply to delayed frame
            float gr = (env > thr) ? (thr / env) : 1.0f;
            for (int32_t c = 0; c < channels; c++) {
                samples[f * channels + c] = laBuffer_[readIdx + c] * gr;
            }
        } else {
            // No look-ahead: simple peak detection + envelope
            float peak = 0.0f;
            for (int32_t c = 0; c < channels; c++) {
                float a = fabsf(samples[f * channels + c]);
                if (a > peak) peak = a;
            }

            if (peak > env) {
                env += aCoeff * (peak - env);
            } else {
                env += rCoeff * (peak - env);
            }

            float gr = (env > thr) ? (thr / env) : 1.0f;
            for (int32_t c = 0; c < channels; c++) {
                samples[f * channels + c] *= gr;
            }
        }
    }

    envelope_ = env;
}
