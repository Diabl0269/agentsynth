#pragma once

#include "../Transport/TransportService.h"
#include "FX/BrickwallCeiling.h"
#include "Mixer/PeakMeterLatch.h"
#include "ModuleBase.h"
#include <array>
#include <atomic>
#include <juce_audio_basics/juce_audio_basics.h>

/**
 * @brief "Master" — the mix bus every channel strip feeds (docs/mixer/mixer.md#node-types).
 *
 * Spliced in front of Rec Tap / Audio Output when the first channel is created
 * (synth::ensureMasterNode, Source/Mixer/MasterSplice.h), so the chain reads
 *
 *     ... channel strips ... -> Master -> Rec Tap -> Audio Output
 *
 * Inputs, four jacks on four raw channels:
 *   - Mix L / Mix R (ch0 / ch1)     — where channel strips land.
 *   - Direct L / Direct R (ch2/ch3) — whatever went straight to the output before the splice. It
 *     stays audible, but it is not a channel: while any strip is soloed
 *     (TransportService::isMixerSoloActiveForBlock) Direct is silenced along with every non-soloed
 *     strip (docs/mixer/mixer.md#solo-is-a-render-time-gate).
 * Outputs: Left / Right (ch0 / ch1).
 *
 * Direct is summed into Mix FIRST and the Master gain applied after, so Direct is post-fader like
 * any other input to the bus.
 *
 * SAFETY LIMITER: after the gain, an always-on brickwall at -1 dBFS (synth::BrickwallCeiling,
 * LimiterModule's Ceiling stage; instant attack, 80 ms release), so a hot mix can't clip the output.
 * The "safetyLimiter" param defaults ON, so a project saved without it loads limited. Out of the path
 * below the ceiling. Bypass and mute skip it (bypass keeps its unity sum).
 *
 * BYPASS / MUTE — two separate branches (root CLAUDE.md). Bypass is a unity sum of Mix + Direct:
 * this module's dry path is "the bus without its fader", and dropping Direct would silence every
 * cable that was never put on a channel. The solo gate on Direct applies in both branches, for the
 * same reason ChannelStripModule gates its dry branch. Mute clears.
 *
 * DUAL I/O (input side only, StereoAudio::Declared, ships SPLIT = the four jacks above, so a saved
 * project without the "dualIO" param opens unchanged). Collapsed, the card shows two jacks, "Mix"
 * (ch0+ch1) and "Direct" (ch2+ch3), each one a stereo pair — which is what a track's macro output
 * port feeds with a single cable. The raw channels never move. The OUTPUTS stay Left / Right in both
 * states: the toggle is inherited from the base, but the output collapse is not taken.
 *
 * INTERNAL-ONLY, same three exclusions as ChannelStripModule (docs/mixer/mixer.md#ai-authorability).
 */
class MasterModule : public ModuleBase {
public:
    static constexpr int kMixLeft = 0;
    static constexpr int kMixRight = 1;
    static constexpr int kDirectLeft = 2;
    static constexpr int kDirectRight = 3;
    static constexpr int kNumInputs = 4;
    static constexpr int kNumOutputs = 2;

    static constexpr float kMinGainDb = -60.0f;
    static constexpr float kMaxGainDb = 12.0f;
    static constexpr double kSmoothingSeconds = 0.02;
    static constexpr float kSafetyCeilingDb = -1.0f;
    static constexpr float kSafetyReleaseMs = 80.0f;
    static constexpr const char* kSafetyLimiterId = "safetyLimiter";
    static constexpr const char* kSafetyLimiterTooltip = "Keeps the master from clipping: limits peaks to -1 dB";

    MasterModule()
        : ModuleBase("Master", kNumInputs, kNumOutputs, StereoAudio::Declared) {
        addParameter(gainParam_ = new juce::AudioParameterFloat(
                         "gain", "Gain", juce::NormalisableRange<float>(kMinGainDb, kMaxGainDb, 0.1f), 0.0f));
        addParameter(safetyLimiterParam_ = new juce::AudioParameterBool(kSafetyLimiterId, "Safety limiter", true));
        addMuteParameter();
    }

    ~MasterModule() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        juce::ignoreUnused(samplesPerBlock);
        smoothedGain_.reset(sampleRate, kSmoothingSeconds);
        smoothedGain_.setCurrentAndTargetValue(targetGain());
        sampleRate_ = sampleRate;
        safetyLimiter_.reset();
        meterLatches_[0].reset();
        meterLatches_[1].reset();
    }

    void processModuleBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        juce::ignoreUnused(midiMessages);
        const int numSamples = buffer.getNumSamples();
        if (buffer.getNumChannels() < kNumInputs) {
            buffer.clear();
            return;
        }

        if (isBypassed()) {
            sumDirectIntoMix(buffer, numSamples);
            storeMeter(buffer, numSamples);
            return;
        }

        if (isMuted()) {
            buffer.clear();
            storeMeter(buffer, numSamples);
            return;
        }

        sumDirectIntoMix(buffer, numSamples);
        smoothedGain_.setTargetValue(targetGain());
        auto* left = buffer.getWritePointer(kMixLeft);
        auto* right = buffer.getWritePointer(kMixRight);
        for (int i = 0; i < numSamples; ++i) {
            const float g = smoothedGain_.getNextValue();
            left[i] *= g;
            right[i] *= g;
        }
        if (safetyLimiterParam_->get())
            safetyLimiter_.process(left, right, numSamples, kSafetyCeilingDb, kSafetyReleaseMs, sampleRate_);
        else
            safetyLimiter_.reset();
        storeMeter(buffer, numSamples);
    }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }

    ModuleType getModuleType() const override { return ModuleType::Master; }
    ModulationCategory getModulationCategory() const override { return ModulationCategory::Other; }

    int getVisibleInputPortCount() const override { return isDualIO() ? kNumInputs : 2; }
    int getVisibleOutputPortCount() const override { return kNumOutputs; }

    juce::String getInputPortLabel(int visibleJack) const override {
        if (!isDualIO())
            return visibleJack == 0 ? "Mix" : "Direct";
        switch (visibleJack) {
        case kMixLeft:
            return "Mix L";
        case kMixRight:
            return "Mix R";
        case kDirectLeft:
            return "Direct L";
        default:
            return "Direct R";
        }
    }
    juce::String getOutputPortLabel(int visibleJack) const override { return visibleJack == 1 ? "Right" : "Left"; }

    LogicalPort mapInputChannel(int rawChannel) const override {
        if (isDualIO())
            return audioJack(rawChannel, kNumInputs);
        // Collapsed: raw (0,1) is jack 0 and raw (2,3) is jack 1, each a contiguous stereo pair
        // (the same shape as ModuleBase::mapStereoPairInput's collapsed branch).
        LogicalPort p;
        p.visibleJackIndex = juce::jlimit(0, 1, rawChannel / 2);
        p.role = PortRole::Audio;
        p.isPolyGroupHead = rawChannel >= 0 && rawChannel < kNumInputs && rawChannel % 2 == 0;
        p.polyVoiceSpan = p.isPolyGroupHead ? 2 : 1;
        return p;
    }
    LogicalPort mapOutputChannel(int rawChannel) const override { return audioJack(rawChannel, kNumOutputs); }

    /** The peak latched since `reader`'s own last call, for one leg (0 = Left, 1 = Right).
     *  See ChannelStripModule::takeMeterPeak / Source/Mixer/PeakMeterLatch.h. */
    float takeMeterPeak(synth::MeterReader reader, int leg) noexcept {
        return meterLatches_[leg == 1 ? 1 : 0].takePeak(reader);
    }

private:
    static LogicalPort audioJack(int rawChannel, int numJacks) noexcept {
        LogicalPort p;
        p.visibleJackIndex = juce::jlimit(0, numJacks - 1, rawChannel);
        p.role = PortRole::Audio;
        p.isPolyGroupHead = rawChannel >= 0 && rawChannel < numJacks;
        p.polyVoiceSpan = 1;
        return p;
    }

    float targetGain() const { return juce::Decibels::decibelsToGain(gainParam_->get(), kMinGainDb); }

    bool isDirectGatedThisBlock() const {
        auto* transport = dynamic_cast<synth::TransportService*>(getPlayHead());
        return transport != nullptr && transport->isMixerSoloActiveForBlock();
    }

    // Direct into Mix (unless solo is gating it), then clear the Direct channels — they are not
    // outputs, but the bus is four wide and hygiene costs nothing.
    void sumDirectIntoMix(juce::AudioBuffer<float>& buffer, int numSamples) const {
        if (!isDirectGatedThisBlock()) {
            buffer.addFrom(kMixLeft, 0, buffer, kDirectLeft, 0, numSamples);
            buffer.addFrom(kMixRight, 0, buffer, kDirectRight, 0, numSamples);
        }
        buffer.clear(kDirectLeft, 0, numSamples);
        buffer.clear(kDirectRight, 0, numSamples);
    }

    void storeMeter(const juce::AudioBuffer<float>& buffer, int numSamples) noexcept {
        meterLatches_[0].storeBlockPeak(buffer.getMagnitude(kMixLeft, 0, numSamples));
        meterLatches_[1].storeBlockPeak(buffer.getMagnitude(kMixRight, 0, numSamples));
    }

    juce::AudioParameterFloat* gainParam_ = nullptr;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedGain_{1.0f};
    juce::AudioParameterBool* safetyLimiterParam_ = nullptr;
    synth::BrickwallCeiling safetyLimiter_;
    double sampleRate_ = 44100.0;
    std::array<synth::PeakMeterLatch, 2> meterLatches_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MasterModule)
};
