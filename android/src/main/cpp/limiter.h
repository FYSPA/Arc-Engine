#pragma once

#include <stdint.h>
#include <cstring>

class Limiter {
public:
    static constexpr int32_t kMaxLookAhead = 882; // ~20ms @ 44100

    Limiter();
    ~Limiter();

    void process(float *samples, int32_t numFrames, int32_t channels);

    void setThresholdDb(float db);
    float thresholdDb() const { return thresholdDb_; }
    float thresholdLin() const { return thresholdLin_; }

    void setAttackMs(float ms);
    float attackMs() const { return attackMs_; }

    void setReleaseMs(float ms);
    float releaseMs() const { return releaseMs_; }

    void setLookAheadMs(float ms);
    float lookAheadMs() const { return lookAheadMs_; }

    void setEnabled(bool enabled);
    bool enabled() const { return enabled_; }

    void reset();

private:
    bool enabled_;
    float thresholdDb_;
    float thresholdLin_;
    float ceilingLin_;
    float attackMs_;
    float releaseMs_;
    float attackCoeff_;
    float releaseCoeff_;
    float lookAheadMs_;
    int32_t lookAheadSamples_;
    float envelope_;

    // Look-ahead circular buffer (stereo: interleaved)
    float laBuffer_[kMaxLookAhead * 2];
    int32_t laWritePos_;
    int32_t laReadPos_;
    int32_t laCount_;
    int32_t laChannels_;

    void recalcThreshold();
    void recalcCoefficients();
};
