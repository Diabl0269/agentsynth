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
        addOutputLevelParameter();
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
        // Centre Freq is the allpass bank's cutoff: stepping it swaps every stage's coefficients
        // at once, which lands as a step in the notched output. Multiplicative so the ramp is
        // even in pitch, 50 ms to match Rate.
        smoothedCentreFreq.reset(sampleRate, 0.05);
        smoothedRate.setCurrentAndTargetValue(*rateParam);
        smoothedDepth.setCurrentAndTargetValue(*depthParam);
        smoothedCentreFreq.setCurrentAndTargetValue(*centreFreqParam);
        prepareOutputLevel(sampleRate);
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
        smoothedCentreFreq.setTargetValue(*centreFreqParam);

        float rate = juce::jlimit(0.1f, 20.0f, smoothedRate.getCurrentValue() + cvRateVal * 10.0f);
        float depth = juce::jlimit(0.0f, 1.0f, smoothedDepth.getCurrentValue() + cvDepthVal);

        phaser.setRate(rate);
        phaser.setDepth(depth);
        phaser.setCentreFrequency(smoothedCentreFreq.getCurrentValue());
        // Feedback and Mix are already ramped inside juce::dsp::Phaser (a per-channel
        // SmoothedValue and a DryWetMixer), so smoothing them again here would only add lag.
        phaser.setFeedback(*feedbackParam);
        phaser.setMix(*mixParam);

        juce::dsp::AudioBlock<float> fullBlock(buffer);
        juce::dsp::AudioBlock<float> audioBlock = fullBlock.getSubsetChannelBlock(0, 2);
        juce::dsp::ProcessContextReplacing<float> context(audioBlock);
        phaser.process(context);

        applyOutputLevel(buffer, 2);

        // Clear CV channels to prevent leaking to downstream modules
        for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
            buffer.clear(ch, 0, numSamples);

        smoothedRate.skip(numSamples);
        smoothedDepth.skip(numSamples);
        smoothedCentreFreq.skip(numSamples);
    }

    juce::String getInputPortLabel(int i) const override {
        const juce::String cv[] = {"Rate", "Depth"};
        return stereoInputLabel(i, 2, cv);
    }
    juce::String getOutputPortLabel(int i) const override { return stereoOutputLabel(i); }
    int getVisibleInputPortCount() const override { return stereoVisibleInputCount(2); }
    int getVisibleOutputPortCount() const override { return stereoVisibleOutputCount(); }
    LogicalPort mapInputChannel(int raw) const override { return mapStereoPairInput(raw, 2); }
    LogicalPort mapOutputChannel(int raw) const override { return mapStereoPairOutput(raw); }

    std::vector<ModulationTarget> getModulationTargets() const override { return {{"Rate", 2}, {"Depth", 3}}; }
    // Pure audio FX — processBlock never touches the MIDI buffer.
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }

    ModulationCategory getModulationCategory() const override { return ModulationCategory::FX; }
    ModuleType getModuleType() const override { return ModuleType::Phaser; }

private:
    juce::dsp::Phaser<float> phaser;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedRate;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedDepth;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> smoothedCentreFreq{1300.0f};

    juce::AudioParameterFloat* rateParam;
    juce::AudioParameterFloat* depthParam;
    juce::AudioParameterFloat* centreFreqParam;
    juce::AudioParameterFloat* feedbackParam;
    juce::AudioParameterFloat* mixParam;
};
