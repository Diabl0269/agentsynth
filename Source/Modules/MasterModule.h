#pragma once

#include "../Transport/TransportService.h"
#include "ModuleBase.h"
#include <atomic>
#include <juce_audio_basics/juce_audio_basics.h>

/**
 * @brief "Master" — the mix bus every channel strip feeds (P9-2, docs/mixer.md §5.1).
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
 *     strip (docs/mixer.md §5.3).
 * Outputs: Left / Right (ch0 / ch1).
 *
 * Direct is summed into Mix FIRST and the Master gain applied after, so Direct is post-fader like
 * any other input to the bus.
 *
 * BYPASS / MUTE — two separate branches (root CLAUDE.md). Bypass is a unity sum of Mix + Direct:
 * this module's dry path is "the bus without its fader", and dropping Direct would silence every
 * cable that was never put on a channel. The solo gate on Direct applies in both branches, for the
 * same reason ChannelStripModule gates its dry branch. Mute clears.
 *
 * The (4, 2) shape matches hasStereoOutputPairShape, but Master is not an FX-shaped stereo pair —
 * its inputs are two stereo BLOCKS, not a pair plus CV — so it opts out of the inherited Dual I/O
 * toggle (StereoAudio::None; recorded in the StereoDeclaration sweep's kDualIOOptOuts).
 *
 * INTERNAL-ONLY, same three exclusions as ChannelStripModule (docs/mixer.md §6).
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

    MasterModule()
        : ModuleBase("Master", kNumInputs, kNumOutputs, StereoAudio::None) {
        addParameter(gainParam_ = new juce::AudioParameterFloat(
                         "gain", "Gain", juce::NormalisableRange<float>(kMinGainDb, kMaxGainDb, 0.1f), 0.0f));
        addMuteParameter();
    }

    ~MasterModule() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        juce::ignoreUnused(samplesPerBlock);
        smoothedGain_.reset(sampleRate, kSmoothingSeconds);
        smoothedGain_.setCurrentAndTargetValue(targetGain());
        meterPeakL_.store(0.0f, std::memory_order_relaxed);
        meterPeakR_.store(0.0f, std::memory_order_relaxed);
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
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
        storeMeter(buffer, numSamples);
    }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }

    ModuleType getModuleType() const override { return ModuleType::Master; }
    ModulationCategory getModulationCategory() const override { return ModulationCategory::Other; }

    int getVisibleInputPortCount() const override { return kNumInputs; }
    int getVisibleOutputPortCount() const override { return kNumOutputs; }

    juce::String getInputPortLabel(int visibleJack) const override {
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

    LogicalPort mapInputChannel(int rawChannel) const override { return audioJack(rawChannel, kNumInputs); }
    LogicalPort mapOutputChannel(int rawChannel) const override { return audioJack(rawChannel, kNumOutputs); }

    /** The last processed block's absolute output peak for one leg (0 = Left, 1 = Right). Plain
     *  per-block store; see ChannelStripModule::getMeterPeak. */
    float getMeterPeak(int leg) const noexcept {
        return (leg == 1 ? meterPeakR_ : meterPeakL_).load(std::memory_order_relaxed);
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
        meterPeakL_.store(buffer.getMagnitude(kMixLeft, 0, numSamples), std::memory_order_relaxed);
        meterPeakR_.store(buffer.getMagnitude(kMixRight, 0, numSamples), std::memory_order_relaxed);
    }

    juce::AudioParameterFloat* gainParam_ = nullptr;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedGain_{1.0f};
    std::atomic<float> meterPeakL_{0.0f};
    std::atomic<float> meterPeakR_{0.0f};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MasterModule)
};
