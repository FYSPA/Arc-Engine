#pragma once

#include "effect.h"
#include <vector>
#include <cmath>

class Reverb : public AudioEffect {
public:
    Reverb();

    void process(float *samples, int32_t numFrames, int32_t channels) override;
    void init(float sampleRate) override;
    void reset() override;
    const char* name() const override { return "reverb"; }

    void setMix(float v);
    void setDecay(float v);
    void setRoomSize(float v);
    void setDamping(float v);
    void setPreDelayMs(float v);

    float mix() const { return mix_; }
    float decay() const { return decay_; }
    float roomSize() const { return roomSize_; }
    float damping() const { return damping_; }
    float preDelayMs() const { return preDelayMs_; }

private:
    struct DelayLine {
        std::vector<float> data;
        int writePos = 0;
        int size = 0;

        void resize(int n);
        float read(int delay) const;
        void write(float v);
        void zap();
    };

    struct CombFilter {
        DelayLine delay;
        float gain = 0.0f;
        float dampZ = 0.0f;
        int delaySamples = 0;

        float process(float input, float dampCoeff);
    };

    struct AllPassFilter {
        DelayLine delay;
        float gain = 0.0f;
        int delaySamples = 0;

        float process(float input);
    };

    struct PreDelay {
        DelayLine delay;
        int samples = 0;

        float process(float input);
    };

    float mix_ = 0.3f;
    float decay_ = 2.0f;
    float roomSize_ = 0.5f;
    float damping_ = 0.5f;
    float preDelayMs_ = 20.0f;

    static const int kNumCombs = 4;
    static const int kNumAllPasses = 2;

    CombFilter combs_[kNumCombs];
    AllPassFilter allPasses_[kNumAllPasses];
    PreDelay preDelay_;

    int baseCombDelays_[kNumCombs] = {};
    int baseAllPassDelays_[kNumAllPasses] = {};

    float dampCoeff_ = 0.0f;

    void recalc();
};
