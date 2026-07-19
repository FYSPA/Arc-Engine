#pragma once

#include <stdint.h>

class Limiter {
public:
    Limiter();

    void process(float *samples, int32_t numFrames, int32_t channels);

    void setThresholdDb(float db);
    float thresholdDb() const { return thresholdDb_; }
    float thresholdLin() const { return thresholdLin_; }

    void setEnabled(bool enabled);
    bool enabled() const { return enabled_; }

private:
    bool enabled_;
    float thresholdDb_;
    float thresholdLin_;
    float ceilingLin_;

    void recalcThreshold();
};
