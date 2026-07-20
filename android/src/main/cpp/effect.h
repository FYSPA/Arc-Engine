#pragma once

#include <stdint.h>
#include <cstring>
#include <vector>
#include <mutex>
#include <string>
#include <functional>
#include <unordered_map>

class AudioEffect {
public:
    virtual ~AudioEffect() = default;

    virtual void process(float *samples, int32_t numFrames, int32_t channels) = 0;
    virtual void reset() {}
    virtual void init(float sampleRate) { sampleRate_ = sampleRate; }

    virtual bool active() const { return enabled_; }
    virtual void setEnabled(bool v) { enabled_ = v; }
    virtual const char* name() const = 0;

protected:
    bool enabled_ = true;
    float sampleRate_ = 44100.0f;
};

class EffectChain {
public:
    ~EffectChain() {
        for (auto *fx : effects_) delete fx;
        effects_.clear();
    }

    void add(AudioEffect *fx) {
        std::lock_guard<std::mutex> lock(mutex_);
        effects_.push_back(fx);
    }

    bool remove(const char *name) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = effects_.begin(); it != effects_.end(); ++it) {
            if (strcmp((*it)->name(), name) == 0) {
                delete *it;
                effects_.erase(it);
                return true;
            }
        }
        return false;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto *fx : effects_) delete fx;
        effects_.clear();
    }

    void process(float *samples, int32_t numFrames, int32_t channels) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto *fx : effects_)
            if (fx->active())
                fx->process(samples, numFrames, channels);
    }

    int count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return (int)effects_.size();
    }

    AudioEffect* at(int index) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (index < 0 || index >= (int)effects_.size()) return nullptr;
        return effects_[index];
    }

    AudioEffect* find(const char *name) const {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto *fx : effects_)
            if (strcmp(fx->name(), name) == 0) return fx;
        return nullptr;
    }

    void initAll(float sr) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto *fx : effects_) fx->init(sr);
    }

private:
    mutable std::mutex mutex_;
    std::vector<AudioEffect*> effects_;
};

// Effect factory registry — maps name → creator lambda.
// Register: fxRegistry["compressor"] = []{ return new Compressor(); };
using FxFactoryMap = std::unordered_map<std::string, std::function<AudioEffect*()>>;

inline FxFactoryMap& fxRegistry() {
    static FxFactoryMap reg;
    return reg;
}
