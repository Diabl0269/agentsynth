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
        addDualIOParameter();
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
        // Threshold is where the gain computer starts pulling the signal down, so stepping it
        // steps the applied gain reduction. Advanced a block at a time (juce::dsp::Limiter only
        // takes it through setThreshold), snapped at prepare so a static render is unchanged.
        smoothedThreshold.reset(sampleRate, 0.01);
        smoothedThreshold.setCurrentAndTargetValue(*thresholdParam);
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
        smoothedThreshold.setTargetValue(*thresholdParam);
        limiter.setThreshold(smoothedThreshold.getCurrentValue());
        // Release is a detector time constant: stepping it changes how fast gain recovers, never
        // the current gain. Deliberately not smoothed.
        limiter.setRelease(*releaseParam);
        smoothedThreshold.skip(numSamples);

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

    juce::String getInputPortLabel(int i) const override { return stereoInputLabel(i, 0, nullptr); }
    juce::String getOutputPortLabel(int i) const override { return stereoOutputLabel(i); }
    int getVisibleInputPortCount() const override { return stereoVisibleInputCount(0); }
    int getVisibleOutputPortCount() const override { return stereoVisibleOutputCount(); }
    LogicalPort mapInputChannel(int raw) const override { return mapStereoPairInput(raw, 0); }
    LogicalPort mapOutputChannel(int raw) const override { return mapStereoPairOutput(raw); }

    std::vector<ModulationTarget> getModulationTargets() const override { return {}; }
    ModulationCategory getModulationCategory() const override { return ModulationCategory::FX; }
    ModuleType getModuleType() const override { return ModuleType::Limiter; }

private:
    juce::dsp::Limiter<float> limiter;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedInputGain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedThreshold;

    juce::AudioParameterFloat* thresholdParam;
    juce::AudioParameterFloat* releaseParam;
    juce::AudioParameterFloat* inputGainParam;
};
