#pragma once

#include "ModuleBase.h"
#include "SchmittTrigger.h"
#include "ThresholdMeterSource.h"
#include <algorithm>
#include <atomic>
#include <cmath>

/**
    Comparator — emits a gate when the Signal input is above Threshold, and the inverted
    gate on a second output. The classic modular "slice this into a pulse" utility: turn
    an LFO into a pulse wave, a kick into a gate, an envelope into a window.

    This is a *switch*, not a detector and not an envelope generator. Envelope Follower
    tracks amplitude continuously; ADSR shapes a gate into Attack/Decay/Sustain/Release.
    The Comparator only answers "is it over the line?".

    Channel layout (mono):
      inputs   0 = Signal, 1 = Threshold CV
      outputs  0 = Gate,   1 = Inverse

    Bypass/mute both clear — there is no dry audio path. Passing the Signal through would
    push audio-rate samples into a Gate destination.
*/
class ComparatorModule
    : public ModuleBase
    , public ThresholdMeterSource {
public:
    ComparatorModule()
        : ModuleBase("Comparator", 2, 2) {
        // `trigThreshold`, not `threshold`: Compressor and Limiter both own a `threshold`
        // float meaning dB. Same id as Sample & Hold because both mean a bipolar CV slice.
        addParameter(thresholdParam = new juce::AudioParameterFloat("trigThreshold", "Threshold", -1.0f, 1.0f, 0.5f));
        addMuteParameter();
        enableVisualBuffer(true);
    }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        juce::ignoreUnused(sampleRate, samplesPerBlock);
        trigger.reset();
        resetMeters();
        effectiveThreshold.store(thresholdParam->get(), std::memory_order_relaxed);
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        juce::ignoreUnused(midiMessages);

        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();
        if (numSamples == 0 || numChannels == 0)
            return;

        // Audio-in / CV-out tap: no dry path. Two separate branches so the intent is explicit.
        if (isBypassed()) {
            buffer.clear();
            resetMeters();
            return;
        }
        if (isMuted()) {
            buffer.clear();
            resetMeters();
            return;
        }

        const float baseThreshold = thresholdParam->get();
        const float* signalIn = buffer.getReadPointer(0);
        const float* thresholdCV = numChannels > 1 ? buffer.getReadPointer(1) : nullptr;

        float* gateOut = buffer.getWritePointer(0);
        float* inverseOut = numChannels > 1 ? buffer.getWritePointer(1) : nullptr;

        float meterPeak = 0.0f;
        float lastThreshold = baseThreshold;
        int firedThisBlock = 0;

        for (int s = 0; s < numSamples; ++s) {
            const float sample = signalIn[s];
            if (std::abs(sample) > std::abs(meterPeak))
                meterPeak = sample;

            float threshold = baseThreshold;
            if (thresholdCV != nullptr)
                threshold = juce::jlimit(-1.0f, 1.0f, threshold + thresholdCV[s]);
            lastThreshold = threshold;

            // Read signal before overwriting ch0 (the Gate output aliases the Signal input).
            const auto edge = trigger.process(sample, threshold);
            if (edge == SchmittTrigger::Edge::Rising)
                ++firedThisBlock;

            const float gate = trigger.high ? 1.0f : 0.0f;
            gateOut[s] = gate;
            if (inverseOut != nullptr)
                inverseOut[s] = 1.0f - gate;
        }

        for (int ch = 2; ch < numChannels; ++ch)
            buffer.clear(ch, 0, numSamples);

        meterLevel.store(meterPeak, std::memory_order_relaxed);
        effectiveThreshold.store(lastThreshold, std::memory_order_relaxed);
        overThreshold.store(trigger.high, std::memory_order_relaxed);
        if (firedThisBlock > 0)
            triggerCount.fetch_add(firedThisBlock, std::memory_order_relaxed);

        if (auto* vb = getVisualBuffer())
            for (int s = 0; s < numSamples; ++s)
                vb->pushSample(gateOut[s]);
    }

    std::vector<ModulationTarget> getModulationTargets() const override { return {{"Threshold", 1}}; }

    juce::String getInputPortLabel(int i) const override {
        return i == 0 ? "Signal" : i == 1 ? "Threshold" : ModuleBase::getInputPortLabel(i);
    }

    juce::String getOutputPortLabel(int i) const override {
        return i == 0 ? "Gate" : i == 1 ? "Inverse" : ModuleBase::getOutputPortLabel(i);
    }

    int getVisibleInputPortCount() const override { return 2; }
    int getVisibleOutputPortCount() const override { return 2; }
    // Pure audio/CV utility — processBlock never touches the MIDI buffer.
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }

    ModulationCategory getModulationCategory() const override { return ModulationCategory::Other; }
    ModuleType getModuleType() const override { return ModuleType::Comparator; }

    LogicalPort mapInputChannel(int raw) const override {
        if (raw == 0 || raw == 1) {
            LogicalPort p;
            p.visibleJackIndex = raw;
            p.role = (raw == 0) ? PortRole::Audio : PortRole::ModCV;
            p.isPolyGroupHead = true;
            p.polyVoiceSpan = 1;
            return p;
        }
        return ModuleBase::mapInputChannel(raw);
    }

    LogicalPort mapOutputChannel(int raw) const override {
        if (raw == 0 || raw == 1) {
            LogicalPort p;
            p.visibleJackIndex = raw;
            p.role = PortRole::Gate;
            p.isPolyGroupHead = true;
            p.polyVoiceSpan = 1;
            return p;
        }
        return ModuleBase::mapOutputChannel(raw);
    }

    float getMeterLevel() const override { return meterLevel.load(std::memory_order_relaxed); }
    float getEffectiveThreshold() const override { return effectiveThreshold.load(std::memory_order_relaxed); }
    bool isOverThreshold() const override { return overThreshold.load(std::memory_order_relaxed); }
    int getTriggerCount() const override { return triggerCount.load(std::memory_order_relaxed); }
    ThresholdScale getThresholdScale() const override { return ThresholdScale::Bipolar; }
    juce::String getThresholdParamID() const override { return "trigThreshold"; }
    juce::String getMeterIdleLabel() const override { return "no signal"; }

    static constexpr float getTriggerHysteresis() { return SchmittTrigger::kHysteresis; }

private:
    void resetMeters() {
        meterLevel.store(0.0f, std::memory_order_relaxed);
        overThreshold.store(false, std::memory_order_relaxed);
        trigger.reset();
    }

    juce::AudioParameterFloat* thresholdParam = nullptr;
    SchmittTrigger trigger;
    std::atomic<float> meterLevel{0.0f};
    std::atomic<float> effectiveThreshold{0.5f};
    std::atomic<bool> overThreshold{false};
    std::atomic<int> triggerCount{0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ComparatorModule)
};
