#pragma once

#include "../ModuleBase.h"
#include <juce_dsp/juce_dsp.h>

class CompressorModule : public ModuleBase {
public:
    CompressorModule()
        : ModuleBase("Compressor", 2, 2) {
        addParameter(thresholdParam =
                         new juce::AudioParameterFloat("threshold", "Threshold (dB)", -60.0f, 0.0f, -12.0f));
        addParameter(ratioParam = new juce::AudioParameterFloat("ratio", "Ratio", 1.0f, 20.0f, 4.0f));
        addParameter(attackParam = new juce::AudioParameterFloat("attack", "Attack (ms)", 0.1f, 200.0f, 10.0f));
        addParameter(releaseParam = new juce::AudioParameterFloat("release", "Release (ms)", 10.0f, 1000.0f, 100.0f));
        addParameter(makeupGainParam =
                         new juce::AudioParameterFloat("makeupGain", "Makeup Gain (dB)", -20.0f, 40.0f, 0.0f));
        addMuteParameter();
    }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = (juce::uint32)samplesPerBlock;
        spec.numChannels = 2;
        compressor.prepare(spec);
        compressor.reset();

        smoothedThreshold.reset(sampleRate, 0.01);
        smoothedMakeupGain.reset(sampleRate, 0.005);
        smoothedThreshold.setCurrentAndTargetValue(*thresholdParam);
        smoothedMakeupGain.setCurrentAndTargetValue(*makeupGainParam);
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        if (isBypassed() || isMuted()) {
            buffer.clear();
            return;
        }

        juce::ignoreUnused(midiMessages);

        const int numSamples = buffer.getNumSamples();
        if (numSamples == 0 || buffer.getNumChannels() < 2)
            return;

        smoothedThreshold.setTargetValue(*thresholdParam);
        smoothedMakeupGain.setTargetValue(*makeupGainParam);

        compressor.setThreshold(smoothedThreshold.getCurrentValue());
        compressor.setRatio(*ratioParam);
        compressor.setAttack(*attackParam);
        compressor.setRelease(*releaseParam);

        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        compressor.process(context);

        // Apply makeup gain per-sample (smoothed)
        for (int i = 0; i < numSamples; ++i) {
            float linearGain = juce::Decibels::decibelsToGain(smoothedMakeupGain.getNextValue());
            buffer.getWritePointer(0)[i] *= linearGain;
            buffer.getWritePointer(1)[i] *= linearGain;
        }

        smoothedThreshold.skip(numSamples);
    }

    juce::String getInputPortLabel(int i) const override { return i == 0 ? "Left" : "Right"; }
    juce::String getOutputPortLabel(int i) const override { return i == 0 ? "Left" : "Right"; }

    std::vector<ModulationTarget> getModulationTargets() const override { return {}; }
    ModulationCategory getModulationCategory() const override { return ModulationCategory::FX; }
    ModuleType getModuleType() const override { return ModuleType::Compressor; }

private:
    juce::dsp::Compressor<float> compressor;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedThreshold;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedMakeupGain;

    juce::AudioParameterFloat* thresholdParam;
    juce::AudioParameterFloat* ratioParam;
    juce::AudioParameterFloat* attackParam;
    juce::AudioParameterFloat* releaseParam;
    juce::AudioParameterFloat* makeupGainParam;
};
