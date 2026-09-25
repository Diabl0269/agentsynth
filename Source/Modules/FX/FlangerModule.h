#pragma once

#include "../ModuleBase.h"
#include <juce_dsp/juce_dsp.h>

class FlangerModule : public ModuleBase {
public:
    FlangerModule()
        : ModuleBase("Flanger", 7, 2) { // 2 audio + 5 CV (Rate, Depth, Centre Delay, Feedback, Mix)
        addParameter(rateParam = new juce::AudioParameterFloat("rate", "Rate (Hz)", 0.05f, 5.0f, 0.3f));
        addParameter(depthParam = new juce::AudioParameterFloat("depth", "Depth", 0.0f, 1.0f, 0.7f));
        addParameter(centreDelayParam =
                         new juce::AudioParameterFloat("centreDelay", "Centre Delay (ms)", 1.0f, 5.0f, 2.0f));
        addParameter(feedbackParam = new juce::AudioParameterFloat("feedback", "Feedback", -1.0f, 1.0f, 0.5f));
        addParameter(mixParam = new juce::AudioParameterFloat("mix", "Mix", 0.0f, 1.0f, 0.5f));
        addOutputLevelParameter();
        addMuteParameter();
    }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = (juce::uint32)samplesPerBlock;
        spec.numChannels = 2;
        flanger.prepare(spec);
        flanger.reset();

        smoothedRate.reset(sampleRate, 0.05);
        smoothedDepth.reset(sampleRate, 0.005);
        // Centre Delay is added straight onto the modulated delay-line read position inside
        // juce::dsp::Chorus, so a per-block step jumps the read head by (delta_ms * fs / 1000)
        // samples — an audible discontinuity. 50 ms, matching Rate.
        smoothedCentreDelay.reset(sampleRate, 0.05);
        smoothedRate.setCurrentAndTargetValue(*rateParam);
        smoothedDepth.setCurrentAndTargetValue(*depthParam);
        smoothedCentreDelay.setCurrentAndTargetValue(*centreDelayParam);
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
        smoothedCentreDelay.setTargetValue(*centreDelayParam);

        float rate = juce::jlimit(0.05f, 5.0f, smoothedRate.getCurrentValue() + cvRateVal * 2.5f);
        float depth = juce::jlimit(0.0f, 1.0f, smoothedDepth.getCurrentValue() + cvDepthVal);

        flanger.setRate(rate);
        flanger.setDepth(depth);
        // Centre Delay / Feedback / Mix CV (ch4-6) follows the normalised convention
        // (docs/modules/modulation.md#cv-in-normalised-units): +1.0 sweeps the knob to its maximum.
        // Read once per block like Rate/Depth above — all three land in juce::dsp::Chorus setters.
        flanger.setCentreDelay(std::max(
            1.0f, modulateNormalised(*centreDelayParam, smoothedCentreDelay.getCurrentValue(), blockCV(buffer, 4))));
        // Feedback and Mix are already ramped inside juce::dsp::Chorus (a per-channel
        // SmoothedValue and a DryWetMixer), so smoothing them again here would only add lag.
        flanger.setFeedback(modulateNormalised(*feedbackParam, *feedbackParam, blockCV(buffer, 5)));
        flanger.setMix(modulateNormalised(*mixParam, *mixParam, blockCV(buffer, 6)));

        juce::dsp::AudioBlock<float> fullBlock(buffer);
        juce::dsp::AudioBlock<float> audioBlock = fullBlock.getSubsetChannelBlock(0, 2);
        juce::dsp::ProcessContextReplacing<float> context(audioBlock);
        flanger.process(context);

        applyOutputLevel(buffer, 2);

        // Clear CV channels to prevent leaking to downstream modules
        for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
            buffer.clear(ch, 0, numSamples);

        smoothedRate.skip(numSamples);
        smoothedDepth.skip(numSamples);
        smoothedCentreDelay.skip(numSamples);
    }

    juce::String getInputPortLabel(int i) const override {
        const juce::String cv[] = {"Rate", "Depth", "Centre Delay", "Feedback", "Mix"};
        return stereoInputLabel(i, 5, cv);
    }
    juce::String getOutputPortLabel(int i) const override { return stereoOutputLabel(i); }
    int getVisibleInputPortCount() const override { return stereoVisibleInputCount(5); }
    int getVisibleOutputPortCount() const override { return stereoVisibleOutputCount(); }
    LogicalPort mapInputChannel(int raw) const override { return mapStereoPairInput(raw, 5); }
    LogicalPort mapOutputChannel(int raw) const override { return mapStereoPairOutput(raw); }

    // Every continuous parameter has a CV jack; paramId binds each jack to its knob ("Rate" is the
    // jack label, "Rate (Hz)" the knob) so the card rings it and accepts a cable dropped on it.
    std::vector<ModulationTarget> getModulationTargets() const override {
        return {{"Rate", 2, "rate"},
                {"Depth", 3, "depth"},
                {"Centre Delay", 4, "centreDelay"},
                {"Feedback", 5, "feedback"},
                {"Mix", 6, "mix"}};
    }
    // Pure audio FX — processBlock never touches the MIDI buffer.
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }

    ModulationCategory getModulationCategory() const override { return ModulationCategory::FX; }
    ModuleType getModuleType() const override { return ModuleType::Flanger; }

private:
    juce::dsp::Chorus<float> flanger;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedRate;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedDepth;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedCentreDelay;

    juce::AudioParameterFloat* rateParam;
    juce::AudioParameterFloat* depthParam;
    juce::AudioParameterFloat* centreDelayParam;
    juce::AudioParameterFloat* feedbackParam;
    juce::AudioParameterFloat* mixParam;
};
