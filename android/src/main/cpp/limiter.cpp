#include "limiter.h"
#include <cmath>

Limiter::Limiter()
    : enabled_(true)
    , thresholdDb_(-0.5f)
    , thresholdLin_(0.944f)
    , ceilingLin_(1.0f)
{}

void Limiter::setEnabled(bool enabled) {
    enabled_ = enabled;
}

void Limiter::setThresholdDb(float db) {
    thresholdDb_ = db < -60.0f ? -60.0f : (db > 0.0f ? 0.0f : db);
    recalcThreshold();
}

void Limiter::recalcThreshold() {
    thresholdLin_ = powf(10.0f, thresholdDb_ / 20.0f);
    if (thresholdLin_ > ceilingLin_) thresholdLin_ = ceilingLin_;
}

void Limiter::process(float *samples, int32_t numFrames, int32_t channels) {
    if (!enabled_ || numFrames <= 0) return;

    float thr = thresholdLin_;
    int32_t total = numFrames * channels;

    for (int32_t i = 0; i < total; i++) {
        float s = samples[i];
        if (s > thr) samples[i] = thr;
        else if (s < -thr) samples[i] = -thr;
    }
}
