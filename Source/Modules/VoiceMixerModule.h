#pragma once

#include "ModuleBase.h"

class VoiceMixerModule : public ModuleBase {
public:
    VoiceMixerModule()
        : ModuleBase("Voice Mixer", 8, 2) // 8 voice channels in, stereo out
    {
        addParameter(levelParam = new juce::AudioParameterFloat("level", "Level", 0.0f, 1.0f, 0.125f));
        addDualIOParameter();
    }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        juce::ignoreUnused(samplesPerBlock);
        smoothedLevel.reset(sampleRate, 0.01); // 10ms smoothing for anti-click
        smoothedLevel.setCurrentAndTargetValue(*levelParam);
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        juce::ignoreUnused(midiMessages);

        int numSamples = buffer.getNumSamples();
        int numChannels = buffer.getNumChannels();
        int voiceCount = std::min(8, numChannels);

        if (numSamples == 0 || voiceCount == 0)
            return;

        // Save all voice input data before overwriting (channels 0-7 are both input and output)
        if (inputCopy.getNumChannels() < voiceCount || inputCopy.getNumSamples() < numSamples)
            inputCopy.setSize(voiceCount, numSamples, false, false, true);
        for (int ch = 0; ch < voiceCount; ++ch)
            inputCopy.copyFrom(ch, 0, buffer, ch, 0, numSamples);

        // Sum all voices into channel 0
        buffer.clear(0, 0, numSamples);
        for (int ch = 0; ch < voiceCount; ++ch) {
            buffer.addFrom(0, 0, inputCopy, ch, 0, numSamples);
        }

        // Apply smoothed level and soft-clip to prevent distortion
        smoothedLevel.setTargetValue(*levelParam);
        float* outputData = buffer.getWritePointer(0);
        float peakIn = 0.0f;
        for (int s = 0; s < numSamples; ++s) {
            float raw = outputData[s];
            if (std::abs(raw) > peakIn)
                peakIn = std::abs(raw);
            float out = raw * smoothedLevel.getNextValue();
            outputData[s] = std::tanh(out);
        }

        // Copy to channel 1 for stereo output
        if (numChannels > 1)
            buffer.copyFrom(1, 0, buffer, 0, 0, numSamples);
    }

    juce::String getInputPortLabel(int i) const override { return "Voice " + juce::String(i); }
    juce::String getOutputPortLabel(int i) const override { return stereoOutputLabel(i); }
    int getVisibleOutputPortCount() const override { return stereoVisibleOutputCount(); }
    LogicalPort mapOutputChannel(int raw) const override { return mapStereoPairOutput(raw); }
    ModuleType getModuleType() const override { return ModuleType::VoiceMixer; }

private:
    juce::AudioParameterFloat* levelParam;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedLevel;
    juce::AudioBuffer<float> inputCopy; // Pre-allocated to avoid audio-thread allocation

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VoiceMixerModule)
};
