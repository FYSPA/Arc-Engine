#pragma once

#include <stdint.h>
#include <cstring>
#include <vector>
#include <mutex>
#include <string>
#include <functional>
#include <unordered_map>
#include <atomic>

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
        // Free authoritative list
        for (auto *fx : effects_) delete fx;
        effects_.clear();
        // Release snapshot reference
        Snapshot *snap = snapshot_.load(std::memory_order_relaxed);
        if (snap && snap->release() == 0) delete snap;
    }

    // ─── Lock-free hot path (called from AAudio callback every ~4ms) ───
    void process(float *samples, int32_t numFrames, int32_t channels) {
        Snapshot *snap = snapshot_.load(std::memory_order_acquire);
        if (!snap) return;
        snap->acquire();  // prevent deletion while we use it
        for (auto *fx : snap->effects)
            if (fx->active())
                fx->process(samples, numFrames, channels);
        if (snap->release() == 0) delete snap;
    }

    // ─── Mutation paths (called from FFI/Dart threads, rare) ───
    void add(AudioEffect *fx) {
        std::lock_guard<std::mutex> lock(mutex_);
        effects_.push_back(fx);
        rebuildSnapshot();
    }

    bool remove(const char *name) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = effects_.begin(); it != effects_.end(); ++it) {
            if (strcmp((*it)->name(), name) == 0) {
                delete *it;
                effects_.erase(it);
                rebuildSnapshot();
                return true;
            }
        }
        return false;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto *fx : effects_) delete fx;
        effects_.clear();
        rebuildSnapshot();
    }

    // ─── Cold-path queries (rarely called, mutex is fine) ───
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
    struct Snapshot {
        std::atomic<int32_t> refCount{1};
        std::vector<AudioEffect*> effects;

        void acquire() { refCount.fetch_add(1, std::memory_order_relaxed); }
        int32_t release() { return refCount.fetch_sub(1, std::memory_order_acq_rel) - 1; }
    };

    void rebuildSnapshot() {
        Snapshot *old = snapshot_.load(std::memory_order_relaxed);
        Snapshot *snap = new Snapshot();
        snap->effects = effects_;  // shallow copy of pointers
        snapshot_.store(snap, std::memory_order_release);
        if (old && old->release() == 0) delete old;
    }

    mutable std::mutex mutex_;
    std::vector<AudioEffect*> effects_;
    std::atomic<Snapshot*> snapshot_{nullptr};
};

// Effect factory registry — maps name → creator lambda.
// Register: fxRegistry["compressor"] = []{ return new Compressor(); };
using FxFactoryMap = std::unordered_map<std::string, std::function<AudioEffect*()>>;

inline FxFactoryMap& fxRegistry() {
    static FxFactoryMap reg;
    return reg;
}
