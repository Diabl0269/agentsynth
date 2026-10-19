// Concern: the shared output stage of every ModuleBase -- the click-free bypass/mute transition and
// the opt-in Mix and Pan. Rationale and the order of operations: ModuleOutputStage.cpp.
#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <thread>

namespace synth {

/** The audio legs of one module's output for one block: raw channel, the input channel bypass leaves
    on it (-1 for none: a dry path), and whether it is the right leg. */
struct OutputLegs {
    static constexpr int kMax = 16;
    int channel[kMax] = {};
    int dryChannel[kMax] = {};
    bool hasDry[kMax] = {};
    bool isRight[kMax] = {};
    int count = 0;
    bool anyDry = false;
    bool hasRight = false;

    void add(int raw, int dryRaw, bool right) noexcept;
};

class ModuleOutputStage {
public:
    /** Which crossfade is running. The module renders its active (not bypassed, not muted) branch
        throughout; MuteOut and BypassToDry only switch to the inactive branch once the ramp ends. */
    enum class Fade { None, MuteOut, MuteIn, BypassToDry, BypassToWet };

    /** Message thread (constructor): reserve the dry-capture storage a module needs without a prepare call. */
    void reserveDefault(int legs);
    /** Message thread (prepareToPlay): size the dry-capture storage and the smoothers. */
    void prepare(double sampleRate, int maxBlock, int legs);

    /** Message thread (constructor): register the shared parameters on `owner`. */
    void addMix(juce::AudioProcessor& owner);
    void addPan(juce::AudioProcessor& owner);

    /** True when nothing needs doing this block: the effective state equals the parameters, no fade is
        running and Mix / Pan rest at 1 / 0. Audio thread; a handful of loads. */
    bool isIdle(bool paramBypass, bool paramMute) const noexcept {
        return synced_ && fade_ == Fade::None && effBypass_ == paramBypass && effMute_ == paramMute && !mixPanWanted();
    }

    /** The state processModuleBlock sees: the lagging effective one on the render thread while a block runs
        through the slow path, the parameter everywhere else. A call from the render thread also records that
        the module honours the flag (a module that never asks is not faded). */
    bool bypassedForRender(bool param) const noexcept {
        if (!onRenderThread())
            return param;
        bypassQueried_ = true;
        return effBypass_;
    }
    bool mutedForRender(bool param) const noexcept {
        if (!onRenderThread())
            return param;
        muteQueried_ = true;
        return effMute_;
    }

    // Audio thread, slow path only (ModuleBase::renderThroughOutputStage).
    void setRate(double sampleRate) noexcept;
    bool canCapture(const OutputLegs& legs, int numSamples) const noexcept;
    void startBlock(bool paramBypass, bool paramMute, const OutputLegs& legs, bool canCaptureDry,
                    bool bypassIsInPlace) noexcept;
    bool needsDry(const OutputLegs& legs) const noexcept;
    void captureDry(const juce::AudioBuffer<float>& buffer, const OutputLegs& legs) noexcept;
    void beginRender() noexcept;
    void endRender(bool paramBypass, bool paramMute) noexcept;
    bool bypassedNow() const noexcept { return effBypass_; }
    bool mutedNow() const noexcept { return effMute_; }
    void applyMixPan(juce::AudioBuffer<float>& buffer, const OutputLegs& legs, bool panApplies) noexcept;
    void skipMixPan(int numSamples) noexcept;
    void applyFade(juce::AudioBuffer<float>& buffer, const OutputLegs& legs) noexcept;
    void finishBlock(int numSamples) noexcept;

private:
    bool mixPanWanted() const noexcept {
        return (mix_ != nullptr && (mixSm_.isSmoothing() || mix_->get() != 1.0f || mixSm_.getCurrentValue() != 1.0f)) ||
               (pan_ != nullptr && (panSm_.isSmoothing() || pan_->get() != 0.0f || panSm_.getCurrentValue() != 0.0f));
    }
    static std::size_t threadKey() noexcept {
        return std::hash<std::thread::id>{}(std::this_thread::get_id()) | std::size_t{1};
    }
    bool onRenderThread() const noexcept {
        const auto owner = renderThread_.load(std::memory_order_relaxed);
        return owner != 0 && owner == threadKey();
    }
    void syncTargets() noexcept;

    juce::AudioParameterFloat* mix_ = nullptr;
    juce::AudioParameterFloat* pan_ = nullptr;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSm_{1.0f};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> panSm_{0.0f};
    juce::AudioBuffer<float> dry_;

    bool synced_ = false;
    bool effBypass_ = false;
    bool effMute_ = false;
    bool dryValid_ = false;
    Fade fade_ = Fade::None;
    int rampPos_ = 0;
    int rampLen_ = 64;
    double rate_ = 0.0;
    std::atomic<std::size_t> renderThread_{0};
    mutable bool bypassQueried_ = false; // written by the render thread only, inside processModuleBlock
    mutable bool muteQueried_ = false;
};

} // namespace synth
