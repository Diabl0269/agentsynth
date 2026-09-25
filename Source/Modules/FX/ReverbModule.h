#pragma once

#include "../ModuleBase.h"
#include <juce_audio_basics/juce_audio_basics.h>

// Wet/Dry can exceed unity gain: juce::Reverb::updateParameters() (JUCE's code, not this
// module's) applies its own internal wetScaleFactor = 3.0 and dryScaleFactor = 2.0 to the 0-1
// Wet/Dry knobs before mixing, so neither is a plain 0-1 balance control at the signal level -
// maxing Wet feeds the algorithmic tail at up to 3x, maxing Dry passes the input at up to 2x.
// ModuleGainAudit (Tests/Engine/GainStaging/ModuleGainAuditTests.cpp) measures a worst case of
// +8.91 dB (Wet at max) and +7.77 dB (Dry at max) against a continuous, hot test tone;
// allow-listed there. See docs/modules/fx-modules.md#reverb-module.
class ReverbModule : public ModuleBase {
public:
    ReverbModule()
        : ModuleBase("Reverb", 7, 2) { // 2 audio + 5 CV (Size, Damping, Wet, Dry, Width)
        addParameter(roomSizeParam = new juce::AudioParameterFloat("roomSize", "Room Size", 0.0f, 1.0f, 0.5f));
        addParameter(dampingParam = new juce::AudioParameterFloat("damping", "Damping", 0.0f, 1.0f, 0.5f));
        addParameter(wetParam = new juce::AudioParameterFloat("wet", "Wet", 0.0f, 1.0f, 0.33f));
        addParameter(dryParam = new juce::AudioParameterFloat("dry", "Dry", 0.0f, 1.0f, 0.4f));
        addParameter(widthParam = new juce::AudioParameterFloat("width", "Width", 0.0f, 1.0f, 1.0f));
        addOutputLevelParameter();
        addMuteParameter();
    }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        juce::ignoreUnused(samplesPerBlock);
        reverb.setSampleRate(sampleRate);
        prepareOutputLevel(sampleRate);
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        if (buffer.getNumSamples() == 0 || buffer.getNumChannels() == 0)
            return;
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

        // CV (ch2-6) follows the normalised convention (docs/modules/modulation.md#cv-in-normalised-units),
        // read once per block — juce::Reverb only takes its parameters per block anyway (and
        // ramps wet/dry/width internally). These jacks were declared as targets long before the
        // module had the channels; the ModulationTargetBinding sweep is what caught it.
        juce::Reverb::Parameters params;
        params.roomSize = modulateNormalised(*roomSizeParam, *roomSizeParam, blockCV(buffer, 2));
        params.damping = modulateNormalised(*dampingParam, *dampingParam, blockCV(buffer, 3));
        params.wetLevel = modulateNormalised(*wetParam, *wetParam, blockCV(buffer, 4));
        params.dryLevel = modulateNormalised(*dryParam, *dryParam, blockCV(buffer, 5));
        params.width = modulateNormalised(*widthParam, *widthParam, blockCV(buffer, 6));
        reverb.setParameters(params);

        if (buffer.getNumChannels() >= 2) {
            reverb.processStereo(buffer.getWritePointer(0), buffer.getWritePointer(1), buffer.getNumSamples());
        } else {
            reverb.processMono(buffer.getWritePointer(0), buffer.getNumSamples());
        }

        // Scales wet+dry together — Wet/Dry set the balance, Level sets how loud the
        // whole thing lands downstream.
        applyOutputLevel(buffer, 2);

        // Clear CV channels to prevent leaking to downstream modules
        for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
            buffer.clear(ch, 0, buffer.getNumSamples());
    }

    juce::String getInputPortLabel(int i) const override {
        const juce::String cv[] = {"Size", "Damping", "Wet", "Dry", "Width"};
        return stereoInputLabel(i, 5, cv);
    }
    juce::String getOutputPortLabel(int i) const override { return stereoOutputLabel(i); }
    int getVisibleInputPortCount() const override { return stereoVisibleInputCount(5); }
    int getVisibleOutputPortCount() const override { return stereoVisibleOutputCount(); }
    LogicalPort mapInputChannel(int raw) const override { return mapStereoPairInput(raw, 5); }
    LogicalPort mapOutputChannel(int raw) const override { return mapStereoPairOutput(raw); }

    // Every continuous parameter has a CV jack; paramId binds "Size" to the "Room Size" knob.
    std::vector<ModulationTarget> getModulationTargets() const override {
        return {{"Size", 2, "roomSize"},
                {"Damping", 3, "damping"},
                {"Wet", 4, "wet"},
                {"Dry", 5, "dry"},
                {"Width", 6, "width"}};
    }
    // Pure audio FX — processBlock never touches the MIDI buffer.
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }

    ModulationCategory getModulationCategory() const override { return ModulationCategory::FX; }
    ModuleType getModuleType() const override { return ModuleType::Reverb; }

private:
    juce::Reverb reverb;
    juce::AudioParameterFloat* roomSizeParam;
    juce::AudioParameterFloat* dampingParam;
    juce::AudioParameterFloat* wetParam;
    juce::AudioParameterFloat* dryParam;
    juce::AudioParameterFloat* widthParam;
};
