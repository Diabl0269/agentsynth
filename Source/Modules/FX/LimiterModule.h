#pragma once

#include "../ModuleBase.h"
#include <juce_dsp/juce_dsp.h>

// Threshold bakes in automatic makeup gain: juce::dsp::Limiter's own update() (JUCE's code, not
// this module's) computes outputVolume = 10^(10*(1 - 1/ratio)/40) * dB2gain(-threshold) with a
// fixed internal ratio of 4, so a LOWER threshold (more limiting) also raises JUCE's own
// automatic makeup gain - a loudness-maximizing limiter, not a passive ceiling. ModuleGainAudit
// (Tests/Engine/GainStaging/ModuleGainAuditTests.cpp) measures a net worst case of +7.61 dB
// output/input RMS at threshold's minimum (-20 dB); allow-listed there, not a LimiterModule bug.
// See docs/modules/fx-modules.md#limiter-module.
class LimiterModule : public ModuleBase {
public:
    LimiterModule()
        : ModuleBase("Limiter", 5, 2) { // 2 audio + 3 CV (Threshold, Release, Input Gain)
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
        // Threshold is where the gain computer starts pulling the signal down, so stepping it
        // steps the applied gain reduction. Advanced a block at a time (juce::dsp::Limiter only
        // takes it through setThreshold), snapped at prepare so a static render is unchanged.
        smoothedThreshold.reset(sampleRate, 0.01);
        smoothedThreshold.setCurrentAndTargetValue(*thresholdParam);
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

        smoothedInputGain.setTargetValue(*inputGainParam);
        smoothedThreshold.setTargetValue(*thresholdParam);
        // CV (ch2-4) follows the normalised convention (docs/modules/modulation.md#cv-in-normalised-units),
        // read once per block: Threshold and Release land in juce::dsp::Limiter setters that are
        // only consulted per block, and Input Gain is a per-block dB shift on the smoothed ramp.
        limiter.setThreshold(
            modulateNormalised(*thresholdParam, smoothedThreshold.getCurrentValue(), blockCV(buffer, 2)));
        // Release is a detector time constant: stepping it changes how fast gain recovers, never
        // the current gain. Deliberately not smoothed.
        limiter.setRelease(modulateNormalised(*releaseParam, *releaseParam, blockCV(buffer, 3)));
        smoothedThreshold.skip(numSamples);

        // Apply input gain per-sample (smoothed)
        const float inputGainCV = blockCV(buffer, 4);
        for (int i = 0; i < numSamples; ++i) {
            const float gainDb = modulateNormalised(*inputGainParam, smoothedInputGain.getNextValue(), inputGainCV);
            const float gain = juce::Decibels::decibelsToGain(gainDb);
            buffer.getWritePointer(0)[i] *= gain;
            buffer.getWritePointer(1)[i] *= gain;
        }

        // Only the audio pair goes through the limiter: it was prepared for 2 channels, and the
        // CV block behind it is not audio.
        juce::dsp::AudioBlock<float> fullBlock(buffer);
        juce::dsp::AudioBlock<float> audioBlock = fullBlock.getSubsetChannelBlock(0, 2);
        juce::dsp::ProcessContextReplacing<float> context(audioBlock);
        limiter.process(context);

        // Clear CV channels to prevent leaking to downstream modules
        for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
            buffer.clear(ch, 0, numSamples);
    }

    juce::String getInputPortLabel(int i) const override {
        const juce::String cv[] = {"Threshold", "Release", "Input Gain"};
        return stereoInputLabel(i, 3, cv);
    }
    juce::String getOutputPortLabel(int i) const override { return stereoOutputLabel(i); }
    int getVisibleInputPortCount() const override { return stereoVisibleInputCount(3); }
    int getVisibleOutputPortCount() const override { return stereoVisibleOutputCount(); }
    LogicalPort mapInputChannel(int raw) const override { return mapStereoPairInput(raw, 3); }
    LogicalPort mapOutputChannel(int raw) const override { return mapStereoPairOutput(raw); }

    // Every continuous parameter has a CV jack; paramId binds each jack to its knob ("Threshold"
    // is the jack label, "Threshold (dB)" the knob) so the card rings it and accepts a drop on it.
    std::vector<ModulationTarget> getModulationTargets() const override {
        return {{"Threshold", 2, "threshold"}, {"Release", 3, "release"}, {"Input Gain", 4, "inputGain"}};
    }
    // Pure audio FX — processBlock never touches the MIDI buffer.
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }

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
