#pragma once

#include "../ModuleBase.h"
#include <juce_dsp/juce_dsp.h>

class LimiterModule : public ModuleBase {
public:
    LimiterModule()
        : ModuleBase("Limiter", 2, 2) {
        addParameter(thresholdParam =
                         new juce::AudioParameterFloat("threshold", "Threshold (dB)", -20.0f, 0.0f, -1.0f));
        addParameter(releaseParam = new juce::AudioParameterFloat("release", "Release (ms)", 1.0f, 500.0f, 100.0f));
        addParameter(inputGainParam =
                         new juce::AudioParameterFloat("inputGain", "Input Gain (dB)", -20.0f, 20.0f, 0.0f));
        addMuteParameter();
    }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = (juce::uint32)samplesPerBlock;
        spec.numChannels = 2;
        limiter.prepare(spec);
        limiter.reset();

        smoothedInputGain.reset(sampleRate, 0.005);
        smoothedInputGain.setCurrentAndTargetValue(*inputGainParam);
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        if (isBypassed()) {
            // Pure stereo module — no CV channels to clear; pass dry audio through unchanged
            return;
        }
        if (isMuted()) {
            buffer.clear();
            return;
        }

        juce::ignoreUnused(midiMessages);

        const int numSamples = buffer.getNumSamples();
        if (numSamples == 0 || buffer.getNumChannels() < 2)
            return;

        smoothedInputGain.setTargetValue(*inputGainParam);
        limiter.setThreshold(*thresholdParam);
        limiter.setRelease(*releaseParam);

        // Apply input gain per-sample (smoothed)
        for (int i = 0; i < numSamples; ++i) {
            float gain = juce::Decibels::decibelsToGain(smoothedInputGain.getNextValue());
            buffer.getWritePointer(0)[i] *= gain;
            buffer.getWritePointer(1)[i] *= gain;
        }

        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        limiter.process(context);
    }

    juce::String getInputPortLabel(int i) const override { return i == 0 ? "Left" : "Right"; }
    juce::String getOutputPortLabel(int i) const override { return i == 0 ? "Left" : "Right"; }

    std::vector<ModulationTarget> getModulationTargets() const override { return {}; }
    ModulationCategory getModulationCategory() const override { return ModulationCategory::FX; }
    ModuleType getModuleType() const override { return ModuleType::Limiter; }

private:
    juce::dsp::Limiter<float> limiter;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedInputGain;

    juce::AudioParameterFloat* thresholdParam;
    juce::AudioParameterFloat* releaseParam;
    juce::AudioParameterFloat* inputGainParam;
};
