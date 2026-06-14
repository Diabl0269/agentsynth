#pragma once

#include "../ModuleBase.h"
#include <juce_dsp/juce_dsp.h>

class PhaserModule : public ModuleBase {
public:
    PhaserModule()
        : ModuleBase("Phaser", 4, 2) {
        addParameter(rateParam = new juce::AudioParameterFloat("rate", "Rate (Hz)", 0.1f, 20.0f, 1.0f));
        addParameter(depthParam = new juce::AudioParameterFloat("depth", "Depth", 0.0f, 1.0f, 0.5f));
        addParameter(centreFreqParam =
                         new juce::AudioParameterFloat("centreFreq", "Centre Freq (Hz)", 200.0f, 10000.0f, 1300.0f));
        addParameter(feedbackParam = new juce::AudioParameterFloat("feedback", "Feedback", -1.0f, 1.0f, 0.0f));
        addParameter(mixParam = new juce::AudioParameterFloat("mix", "Mix", 0.0f, 1.0f, 0.5f));
        addMuteParameter();
    }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = (juce::uint32)samplesPerBlock;
        spec.numChannels = 2;
        phaser.prepare(spec);
        phaser.reset();

        smoothedRate.reset(sampleRate, 0.05);
        smoothedDepth.reset(sampleRate, 0.005);
        smoothedRate.setCurrentAndTargetValue(*rateParam);
        smoothedDepth.setCurrentAndTargetValue(*depthParam);
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        if (isBypassed()) {
            // Pass dry audio through; clear CV channels so mod signals don't leak downstream
            for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
                buffer.clear(ch, 0, buffer.getNumSamples());
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

        const float* cvRate = (buffer.getNumChannels() > 2) ? buffer.getReadPointer(2) : nullptr;
        const float* cvDepth = (buffer.getNumChannels() > 3) ? buffer.getReadPointer(3) : nullptr;

        float cvRateVal = 0.0f;
        float cvDepthVal = 0.0f;

        if (cvRate) {
            float rms = 0.0f;
            for (int i = 0; i < numSamples; ++i)
                rms += cvRate[i] * cvRate[i];
            if ((rms / numSamples) > 1e-4f)
                cvRateVal = cvRate[0];
        }
        if (cvDepth) {
            float rms = 0.0f;
            for (int i = 0; i < numSamples; ++i)
                rms += cvDepth[i] * cvDepth[i];
            if ((rms / numSamples) > 1e-4f)
                cvDepthVal = cvDepth[0];
        }

        smoothedRate.setTargetValue(*rateParam);
        smoothedDepth.setTargetValue(*depthParam);

        float rate = juce::jlimit(0.1f, 20.0f, smoothedRate.getCurrentValue() + cvRateVal * 10.0f);
        float depth = juce::jlimit(0.0f, 1.0f, smoothedDepth.getCurrentValue() + cvDepthVal);

        phaser.setRate(rate);
        phaser.setDepth(depth);
        phaser.setCentreFrequency(*centreFreqParam);
        phaser.setFeedback(*feedbackParam);
        phaser.setMix(*mixParam);

        juce::dsp::AudioBlock<float> fullBlock(buffer);
        juce::dsp::AudioBlock<float> audioBlock = fullBlock.getSubsetChannelBlock(0, 2);
        juce::dsp::ProcessContextReplacing<float> context(audioBlock);
        phaser.process(context);

        // Clear CV channels to prevent leaking to downstream modules
        for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
            buffer.clear(ch, 0, numSamples);

        smoothedRate.skip(numSamples);
        smoothedDepth.skip(numSamples);
    }

    juce::String getInputPortLabel(int i) const override {
        const juce::String labels[] = {"Left", "Right", "Rate", "Depth"};
        return (i >= 0 && i < 4) ? labels[i] : ModuleBase::getInputPortLabel(i);
    }
    juce::String getOutputPortLabel(int i) const override { return i == 0 ? "Left" : "Right"; }

    std::vector<ModulationTarget> getModulationTargets() const override { return {{"Rate", 2}, {"Depth", 3}}; }
    ModulationCategory getModulationCategory() const override { return ModulationCategory::FX; }
    ModuleType getModuleType() const override { return ModuleType::Phaser; }

private:
    juce::dsp::Phaser<float> phaser;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedRate;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedDepth;

    juce::AudioParameterFloat* rateParam;
    juce::AudioParameterFloat* depthParam;
    juce::AudioParameterFloat* centreFreqParam;
    juce::AudioParameterFloat* feedbackParam;
    juce::AudioParameterFloat* mixParam;
};
