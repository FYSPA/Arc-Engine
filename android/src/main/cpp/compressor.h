#pragma once

#include "effect.h"
#include <cmath>

class Compressor : public AudioEffect {
public:
    Compressor();

    void process(float *samples, int32_t numFrames, int32_t channels) override;
    const char* name() const override { return "compressor"; }

    void setThresholdDb(float db);
    void setRatio(float r);
    void setAttackMs(float ms);
    void setReleaseMs(float ms);
    void setKneeDb(float db);
    void setMakeupDb(float db);

    float thresholdDb() const { return thresholdDb_; }
    float ratio() const { return ratio_; }
    float attackMs() const { return attackMs_; }
    float releaseMs() const { return releaseMs_; }
    float kneeDb() const { return kneeDb_; }
    float makeupDb() const { return makeupDb_; }

private:
    float thresholdDb_ = -12.0f;
    float ratio_ = 4.0f;
    float attackMs_ = 5.0f;
    float releaseMs_ = 100.0f;
    float kneeDb_ = 3.0f;
    float makeupDb_ = 0.0f;

    float envelope_ = 0.0f;
    float attackCoeff_ = 0.0f;
    float releaseCoeff_ = 0.0f;

    void recalc();
};
