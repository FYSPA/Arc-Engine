#pragma once

#include <stdint.h>
#include <cmath>
#include <cstring>
#include "common.h"

struct BiquadState {
    double x1, x2, y1, y2;
    void reset() { x1 = x2 = y1 = y2 = 0.0; }
};

enum class FilterType : int32_t {
    PEAKING = 0,
    LOW_SHELF = 1,
    HIGH_SHELF = 2,
    LOW_PASS = 3,
    HIGH_PASS = 4
};

struct EqBandConfig {
    bool enabled;
    FilterType type;
    double frequency;
    double gain;
    double q;
};

#define MAX_EQ_BANDS 10

class DspProcessor {
public:
    DspProcessor() {
        bypass_ = true;
        sr_ = 0.0;
        channels_ = 0;
        for (int i = 0; i < MAX_EQ_BANDS; i++) {
            bands_[i].enabled = false;
            bands_[i].type = FilterType::PEAKING;
            bands_[i].frequency = 1000.0;
            bands_[i].gain = 0.0;
            bands_[i].q = 0.707;
            b0_[i] = b1_[i] = b2_[i] = a1_[i] = a2_[i] = 0.0;
            for (int c = 0; c < 2; c++)
                states_[i][c].reset();
        }
    }

    void init(int sampleRate, int numChannels) {
        sr_ = (double)sampleRate;
        channels_ = numChannels;
        bypass_ = false;
        for (int i = 0; i < MAX_EQ_BANDS; i++) {
            for (int c = 0; c < 2; c++)
                states_[i][c].reset();
            if (bands_[i].enabled)
                recalcCoeffs(i);
        }
    }

    void setBand(int index, FilterType type, double freq, double gain, double q) {
        if (index < 0 || index >= MAX_EQ_BANDS) return;
        bands_[index].type = type;
        bands_[index].frequency = freq;
        bands_[index].gain = gain;
        bands_[index].q = q;
        bands_[index].enabled = true;
        if (sr_ > 0.0) recalcCoeffs(index);
        for (int c = 0; c < 2; c++)
            states_[index][c].reset();
    }

    void setBandEnabled(int index, bool enabled) {
        if (index < 0 || index >= MAX_EQ_BANDS) return;
        bands_[index].enabled = enabled;
    }

    void setBypass(bool bypass) { bypass_ = bypass; }
    bool isBypassed() const { return bypass_; }

    void resetAllBands() {
        for (int i = 0; i < MAX_EQ_BANDS; i++) {
            bands_[i].enabled = false;
            bands_[i].frequency = 1000.0;
            bands_[i].gain = 0.0;
            bands_[i].q = 0.707;
            b0_[i] = b1_[i] = b2_[i] = a1_[i] = a2_[i] = 0.0;
            for (int c = 0; c < 2; c++)
                states_[i][c].reset();
        }
    }

    int bandCount() const { return MAX_EQ_BANDS; }

    void process(float *samples, int32_t numFrames, int32_t channels) {
        if (bypass_ || numFrames <= 0) return;

        int32_t ch = channels < 2 ? channels : 2;

        for (int b = 0; b < MAX_EQ_BANDS; b++) {
            if (!bands_[b].enabled) continue;

            double b0 = b0_[b], b1 = b1_[b], b2 = b2_[b];
            double a1 = a1_[b], a2 = a2_[b];

            for (int32_t f = 0; f < numFrames; f++) {
                for (int32_t c = 0; c < ch; c++) {
                    int32_t idx = f * channels + c;
                    BiquadState &st = states_[b][c];
                    double x0 = (double)samples[idx];
                    double y0 = b0 * x0 + b1 * st.x1 + b2 * st.x2
                                - a1 * st.y1 - a2 * st.y2;
                    st.x2 = st.x1;
                    st.x1 = x0;
                    st.y2 = st.y1;
                    st.y1 = y0;
                    samples[idx] = (float)y0;
                }
            }
        }
    }

private:
    void recalcCoeffs(int index) {
        if (sr_ <= 0.0) return;
        double w0 = 2.0 * M_PI * bands_[index].frequency / sr_;
        double cosW = cos(w0);
        double sinW = sin(w0);
        double Q = bands_[index].q;
        double A = pow(10.0, bands_[index].gain / 40.0);
        double alpha = sinW / (2.0 * Q);

        double &b0 = b0_[index], &b1 = b1_[index], &b2 = b2_[index];
        double &a0 = a0_[index], &a1 = a1_[index], &a2 = a2_[index];

        switch (bands_[index].type) {
            case FilterType::PEAKING:
                b0 = 1.0 + alpha * A;
                b1 = -2.0 * cosW;
                b2 = 1.0 - alpha * A;
                a0 = 1.0 + alpha / A;
                a1 = -2.0 * cosW;
                a2 = 1.0 - alpha / A;
                break;
            case FilterType::LOW_SHELF:
                b0 = A * ((A + 1.0) - (A - 1.0) * cosW + 2.0 * sqrt(A) * alpha);
                b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cosW);
                b2 = A * ((A + 1.0) - (A - 1.0) * cosW - 2.0 * sqrt(A) * alpha);
                a0 = (A + 1.0) + (A - 1.0) * cosW + 2.0 * sqrt(A) * alpha;
                a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cosW);
                a2 = (A + 1.0) + (A - 1.0) * cosW - 2.0 * sqrt(A) * alpha;
                break;
            case FilterType::HIGH_SHELF:
                b0 = A * ((A + 1.0) + (A - 1.0) * cosW + 2.0 * sqrt(A) * alpha);
                b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosW);
                b2 = A * ((A + 1.0) + (A - 1.0) * cosW - 2.0 * sqrt(A) * alpha);
                a0 = (A + 1.0) - (A - 1.0) * cosW + 2.0 * sqrt(A) * alpha;
                a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cosW);
                a2 = (A + 1.0) - (A - 1.0) * cosW - 2.0 * sqrt(A) * alpha;
                break;
            case FilterType::LOW_PASS:
                b0 = (1.0 - cosW) / 2.0;
                b1 = 1.0 - cosW;
                b2 = (1.0 - cosW) / 2.0;
                a0 = 1.0 + alpha;
                a1 = -2.0 * cosW;
                a2 = 1.0 - alpha;
                break;
            case FilterType::HIGH_PASS:
                b0 = (1.0 + cosW) / 2.0;
                b1 = -(1.0 + cosW);
                b2 = (1.0 + cosW) / 2.0;
                a0 = 1.0 + alpha;
                a1 = -2.0 * cosW;
                a2 = 1.0 - alpha;
                break;
        }

        b0 /= a0; b1 /= a0; b2 /= a0;
        a1 /= a0; a2 /= a0;
    }

    double sr_;
    int channels_;
    bool bypass_;
    EqBandConfig bands_[MAX_EQ_BANDS];
    double b0_[MAX_EQ_BANDS], b1_[MAX_EQ_BANDS], b2_[MAX_EQ_BANDS];
    double a0_[MAX_EQ_BANDS], a1_[MAX_EQ_BANDS], a2_[MAX_EQ_BANDS];
    BiquadState states_[MAX_EQ_BANDS][2];
};
