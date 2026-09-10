#pragma once

#include "../Transport/TransportService.h"
#include "ModuleBase.h"
#include <atomic>
#include <juce_audio_basics/juce_audio_basics.h>

/**
 * @brief "Channel Strip" — the end of one mixer channel (P9-2, docs/mixer.md §5).
 *
 * The node the mixer enumerates: a channel's chain terminates here, and the strip's gain, pan,
 * mute and solo are what a mixer column drives. Output is always stereo (Left on ch0, Right on
 * kRightBase); the INPUT is Mono or Stereo, decided once when the strip is created.
 *
 * CHANNEL SHAPE (docs/mixer.md §5.4). JUCE settles the bus layout in the ModuleBase constructor,
 * so the node always carries kNumChannels raw channels a side, and `shape_` says which input jacks
 * are visible:
 *
 *   - Mono   — raw ch0 is the one input jack; the strip feeds both output legs from it.
 *   - Stereo — raw ch0 (Left) and raw kRightBase (Right) are the two input jacks.
 *
 * The right leg sits on its own kRightBase block, never ch1 (Source/Modules/CLAUDE.md). ch1..3 are
 * reserved — nothing is mapped there today, and they are cleared every block so a future gain/pan
 * CV input can take one without repointing a saved patch's right-leg cable. kRightBase matches the
 * Macro In/Out port nodes' own, so a stereo Macro Out wires into a strip 0->0, 4->4.
 *
 * The shape is written ONCE — by setShape() from the channel-creation flow, or by a trusted
 * setExtraState() on load — and is locked from then on (and from the first prepareToPlay(), i.e.
 * once the node is live in a graph). A later write with a different shape is refused: changing a
 * strip's width means replacing the strip, one undo step, never widening a live node.
 *
 * The (5, 5) channel shape does not match hasStereoOutputPairShape, so no Dual I/O toggle is
 * inherited and none is wanted — the strip owns its own fixed jack map below.
 *
 * SOLO (docs/mixer.md §5.3). `soloed_` is NOT an AudioParameter: not host-visible, not
 * automatable, persisted only in the trusted extra state. Whether ANY strip is soloed is an
 * engine-owned count (AudioEngine::refreshSoloGate) carried to the audio thread on the playhead —
 * TransportService::isMixerSoloActiveForBlock(). While it is set, every non-soloed strip outputs
 * silence. Solo is never a setMuted() fan-out.
 *
 * BYPASS / MUTE. Two separate branches, per the root CLAUDE.md contract:
 *   - bypass disables the strip's OWN gain and pan (dry pass-through; a mono strip still feeds
 *     both output legs, because the output is stereo in every shape);
 *   - mute clears the buffer.
 * The solo gate is applied in BOTH the dry and the normal branch. That is not the forbidden
 * `isBypassed() || isMuted()` collapse: bypass is about this module's own processing, while the
 * solo gate is an engine-level mixer decision layered on top of whatever the strip outputs —
 * a bypassed non-soloed strip leaking into a soloed mix would break §5.3's "every non-soloed strip
 * outputs silence".
 *
 * INTERNAL-ONLY, the same three exclusions as Rec Tap and the macro port types: no library row, no
 * replace-menu entry, never authorable by a model (kNonAuthorableModuleTypes, docs/mixer.md §6).
 */
class ChannelStripModule : public ModuleBase {
public:
    enum class Shape { Mono, Stereo };

    /** Raw channel carrying the right leg, in and out. See the class comment. */
    static constexpr int kRightBase = 4;
    /** Raw channels a side. Fixed for the node's lifetime. */
    static constexpr int kNumChannels = kRightBase + 1;

    /** The fader's floor. At (or below) this the strip is silent, not merely -60 dB. */
    static constexpr float kMinGainDb = -60.0f;
    static constexpr float kMaxGainDb = 12.0f;

    /** Gain/pan ramp length — long enough to hide a zipper on a fader drag, short enough that a
     *  mixer move feels immediate. */
    static constexpr double kSmoothingSeconds = 0.02;

    ChannelStripModule()
        : ModuleBase("Channel Strip", kNumChannels, kNumChannels) {
        addParameter(gainParam_ = new juce::AudioParameterFloat(
                         "gain", "Gain", juce::NormalisableRange<float>(kMinGainDb, kMaxGainDb, 0.1f), 0.0f));
        addParameter(
            panParam_ = new juce::AudioParameterFloat("pan", "Pan", juce::NormalisableRange<float>(-1.0f, 1.0f), 0.0f));
        addMuteParameter();
    }

    ~ChannelStripModule() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        juce::ignoreUnused(samplesPerBlock);
        // Live in a graph from here on: the shape can no longer change (see the class comment).
        shapeLocked_.store(true, std::memory_order_relaxed);

        float targetL = 0.0f, targetR = 0.0f;
        computeTargetGains(targetL, targetR);
        smoothedGainL_.reset(sampleRate, kSmoothingSeconds);
        smoothedGainR_.reset(sampleRate, kSmoothingSeconds);
        smoothedGainL_.setCurrentAndTargetValue(targetL);
        smoothedGainR_.setCurrentAndTargetValue(targetR);
        meterPeakL_.store(0.0f, std::memory_order_relaxed);
        meterPeakR_.store(0.0f, std::memory_order_relaxed);
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        juce::ignoreUnused(midiMessages);
        const int numSamples = buffer.getNumSamples();
        if (buffer.getNumChannels() < kNumChannels) {
            buffer.clear();
            return;
        }

        const bool stereo = getShape() == Shape::Stereo;

        // Hidden-channel hygiene, unconditional: the reserved block, and the right input when the
        // strip is Mono (nothing should be wired there, but the graph leaves whatever it last had).
        for (int ch = 1; ch < kRightBase; ++ch)
            buffer.clear(ch, 0, numSamples);
        // Mono: the right leg is the left input, in every branch — the output is always stereo.
        if (!stereo)
            buffer.copyFrom(kRightBase, 0, buffer, 0, 0, numSamples);

        const bool soloGated = isSoloGatedThisBlock();

        if (isBypassed()) {
            // Dry: no gain, no pan. The solo gate still applies — see the class comment.
            if (soloGated)
                buffer.clear();
            storeMeter(buffer, numSamples);
            return;
        }

        if (isMuted()) {
            buffer.clear();
            storeMeter(buffer, numSamples);
            return;
        }

        float targetL = 0.0f, targetR = 0.0f;
        computeTargetGains(targetL, targetR);
        smoothedGainL_.setTargetValue(targetL);
        smoothedGainR_.setTargetValue(targetR);

        if (soloGated) {
            // Keep the ramps moving so un-soloing resumes from where the fader actually is.
            smoothedGainL_.skip(numSamples);
            smoothedGainR_.skip(numSamples);
            buffer.clear();
            storeMeter(buffer, numSamples);
            return;
        }

        auto* left = buffer.getWritePointer(0);
        auto* right = buffer.getWritePointer(kRightBase);
        for (int i = 0; i < numSamples; ++i) {
            left[i] *= smoothedGainL_.getNextValue();
            right[i] *= smoothedGainR_.getNextValue();
        }
        storeMeter(buffer, numSamples);
    }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }

    ModuleType getModuleType() const override { return ModuleType::ChannelStrip; }
    ModulationCategory getModulationCategory() const override { return ModulationCategory::Other; }

    int getVisibleInputPortCount() const override { return getShape() == Shape::Stereo ? 2 : 1; }
    int getVisibleOutputPortCount() const override { return 2; }

    juce::String getInputPortLabel(int visibleJack) const override {
        if (getShape() == Shape::Mono)
            return "In";
        return visibleJack == 1 ? "Right" : "Left";
    }
    juce::String getOutputPortLabel(int visibleJack) const override { return visibleJack == 1 ? "Right" : "Left"; }

    LogicalPort mapInputChannel(int rawChannel) const override {
        if (rawChannel == 0)
            return audioJack(0);
        if (rawChannel == kRightBase && getShape() == Shape::Stereo)
            return audioJack(1);
        return {}; // reserved / hidden: role Other, not a group head
    }
    LogicalPort mapOutputChannel(int rawChannel) const override {
        if (rawChannel == 0)
            return audioJack(0);
        if (rawChannel == kRightBase)
            return audioJack(1);
        return {};
    }

    int rightAudioLegChannel() const override { return kRightBase; }

    // ---- Shape (message thread) ----

    /** Sets the input shape. Called ONCE by the channel-creation flow, right after construction
     *  and before the node is added to a running graph. Returns false — and changes nothing — when
     *  the shape is already locked to a DIFFERENT value; re-asserting the current shape is a no-op
     *  that succeeds. */
    bool setShape(Shape shape) {
        if (shapeLocked_.load(std::memory_order_relaxed))
            return shape_.load(std::memory_order_relaxed) == shape;
        shape_.store(shape, std::memory_order_relaxed);
        shapeLocked_.store(true, std::memory_order_relaxed);
        return true;
    }
    Shape getShape() const { return shape_.load(std::memory_order_relaxed); }

    // ---- Solo (message thread writes, audio thread reads) ----

    /** This strip's own solo flag. Callers that change it must let the engine recount
     *  (AudioEngine::setChannelStripSoloed does both), or the "is anything soloed?" gate goes stale. */
    void setSoloed(bool soloed) noexcept { soloed_.store(soloed, std::memory_order_relaxed); }
    bool isSoloed() const noexcept { return soloed_.load(std::memory_order_relaxed); }

    // ---- Meter (audio thread writes, any thread reads) ----

    /** The last processed block's absolute peak for one output leg (0 = Left, 1 = Right), post
     *  gain/pan/mute/solo. A plain per-block store, not a consume-on-read: the mixer column and a
     *  track header's channel chip both read it, and each does its own ballistics. */
    float getMeterPeak(int leg) const noexcept {
        return (leg == 1 ? meterPeakR_ : meterPeakL_).load(std::memory_order_relaxed);
    }

    // ---- Non-parameter state. TRUSTED-PATH ONLY — AIStateMapper never calls setExtraState for
    // model output, and this type is additionally refused outright on the untrusted path
    // (kNonAuthorableModuleTypes). ----

    juce::var getExtraState() const override {
        auto* obj = new juce::DynamicObject();
        obj->setProperty("shape", getShape() == Shape::Mono ? "mono" : "stereo");
        obj->setProperty("solo", isSoloed());
        return juce::var(obj);
    }

    void setExtraState(const juce::var& state) override {
        if (auto* obj = state.getDynamicObject()) {
            if (obj->hasProperty("shape"))
                setShape(obj->getProperty("shape").toString() == "mono" ? Shape::Mono : Shape::Stereo);
            if (obj->hasProperty("solo"))
                setSoloed(static_cast<bool>(obj->getProperty("solo")));
        }
    }

private:
    static LogicalPort audioJack(int visibleJack) noexcept {
        LogicalPort p;
        p.visibleJackIndex = visibleJack;
        p.role = PortRole::Audio;
        p.isPolyGroupHead = true;
        p.polyVoiceSpan = 1;
        return p;
    }

    void computeTargetGains(float& targetL, float& targetR) const {
        const float gain = juce::Decibels::decibelsToGain(gainParam_->get(), kMinGainDb);
        float panL = 1.0f, panR = 1.0f;
        panGains(panParam_->get(), panL, panR);
        targetL = gain * panL;
        targetR = gain * panR;
    }

    bool isSoloGatedThisBlock() const {
        if (isSoloed())
            return false;
        auto* transport = dynamic_cast<synth::TransportService*>(getPlayHead());
        return transport != nullptr && transport->isMixerSoloActiveForBlock();
    }

    void storeMeter(const juce::AudioBuffer<float>& buffer, int numSamples) noexcept {
        meterPeakL_.store(buffer.getMagnitude(0, 0, numSamples), std::memory_order_relaxed);
        meterPeakR_.store(buffer.getMagnitude(kRightBase, 0, numSamples), std::memory_order_relaxed);
    }

    juce::AudioParameterFloat* gainParam_ = nullptr;
    juce::AudioParameterFloat* panParam_ = nullptr;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedGainL_{1.0f};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedGainR_{1.0f};

    // Written on the message thread (setShape / setExtraState, before the node is live), read every
    // block on the audio thread. Relaxed: nothing orders against it.
    std::atomic<Shape> shape_{Shape::Stereo};
    std::atomic<bool> shapeLocked_{false};
    std::atomic<bool> soloed_{false};

    std::atomic<float> meterPeakL_{0.0f};
    std::atomic<float> meterPeakR_{0.0f};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelStripModule)
};
