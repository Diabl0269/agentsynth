#pragma once

#include "../ModuleBase.h"
#include <juce_dsp/juce_dsp.h>

class CompressorModule : public ModuleBase {
public:
    CompressorModule()
        : ModuleBase("Compressor", 7, 2) { // 2 audio + 5 CV (Threshold, Ratio, Attack, Release, Makeup)
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
        // Ratio sets the slope of the gain computer, so stepping it steps the applied gain
        // reduction exactly like Threshold does. Same 10 ms block-rate treatment.
        smoothedRatio.reset(sampleRate, 0.01);
        smoothedThreshold.setCurrentAndTargetValue(*thresholdParam);
        smoothedMakeupGain.setCurrentAndTargetValue(*makeupGainParam);
        smoothedRatio.setCurrentAndTargetValue(*ratioParam);
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

        smoothedThreshold.setTargetValue(*thresholdParam);
        smoothedMakeupGain.setTargetValue(*makeupGainParam);
        smoothedRatio.setTargetValue(*ratioParam);

        // CV (ch2-6) follows the normalised convention (docs/modules/modulation.md#cv-in-normalised-units)
        // and is read once per block — every target lands in a juce::dsp::Compressor setter that
        // is itself only consulted per block. Threshold/Ratio CV rides on top of the smoothed
        // base, so a knob move still ramps while the CV steps.
        compressor.setThreshold(
            modulateNormalised(*thresholdParam, smoothedThreshold.getCurrentValue(), blockCV(buffer, 2)));
        compressor.setRatio(modulateNormalised(*ratioParam, smoothedRatio.getCurrentValue(), blockCV(buffer, 3)));
        // Attack/Release are detector time constants, not levels: stepping one changes how fast
        // the envelope follower tracks, never the current gain. Deliberately not smoothed.
        compressor.setAttack(modulateNormalised(*attackParam, *attackParam, blockCV(buffer, 4)));
        compressor.setRelease(modulateNormalised(*releaseParam, *releaseParam, blockCV(buffer, 5)));

        // Only the audio pair goes through the compressor: it was prepared for 2 channels, and the
        // CV block behind it is not audio.
        juce::dsp::AudioBlock<float> fullBlock(buffer);
        juce::dsp::AudioBlock<float> audioBlock = fullBlock.getSubsetChannelBlock(0, 2);
        juce::dsp::ProcessContextReplacing<float> context(audioBlock);
        compressor.process(context);

        // Apply makeup gain per-sample (smoothed); the CV offset is a per-block dB shift on top.
        const float makeupCV = blockCV(buffer, 6);
        for (int i = 0; i < numSamples; ++i) {
            const float makeupDb = modulateNormalised(*makeupGainParam, smoothedMakeupGain.getNextValue(), makeupCV);
            const float linearGain = juce::Decibels::decibelsToGain(makeupDb);
            buffer.getWritePointer(0)[i] *= linearGain;
            buffer.getWritePointer(1)[i] *= linearGain;
        }

        smoothedThreshold.skip(numSamples);
        smoothedRatio.skip(numSamples);

        // Clear CV channels to prevent leaking to downstream modules
        for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
            buffer.clear(ch, 0, numSamples);
    }

    juce::String getInputPortLabel(int i) const override {
        const juce::String cv[] = {"Threshold", "Ratio", "Attack", "Release", "Makeup"};
        return stereoInputLabel(i, 5, cv);
    }
    juce::String getOutputPortLabel(int i) const override { return stereoOutputLabel(i); }
    int getVisibleInputPortCount() const override { return stereoVisibleInputCount(5); }
    int getVisibleOutputPortCount() const override { return stereoVisibleOutputCount(); }
    LogicalPort mapInputChannel(int raw) const override { return mapStereoPairInput(raw, 5); }
    LogicalPort mapOutputChannel(int raw) const override { return mapStereoPairOutput(raw); }

    // Every continuous parameter has a CV jack; paramId binds each jack to its knob ("Threshold"
    // is the jack label, "Threshold (dB)" the knob) so the card rings it and accepts a drop on it.
    std::vector<ModulationTarget> getModulationTargets() const override {
        return {{"Threshold", 2, "threshold"},
                {"Ratio", 3, "ratio"},
                {"Attack", 4, "attack"},
                {"Release", 5, "release"},
                {"Makeup", 6, "makeupGain"}};
    }
    // Pure audio FX — processBlock never touches the MIDI buffer.
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }

    ModulationCategory getModulationCategory() const override { return ModulationCategory::FX; }
    ModuleType getModuleType() const override { return ModuleType::Compressor; }

private:
    juce::dsp::Compressor<float> compressor;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedThreshold;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedMakeupGain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedRatio;

    juce::AudioParameterFloat* thresholdParam;
    juce::AudioParameterFloat* ratioParam;
    juce::AudioParameterFloat* attackParam;
    juce::AudioParameterFloat* releaseParam;
    juce::AudioParameterFloat* makeupGainParam;
};
