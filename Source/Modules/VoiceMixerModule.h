#pragma once

#include "ModuleBase.h"
#include "Modules/ParameterText.h"
#include "Modules/SoftLimit.h"

class VoiceMixerModule : public ModuleBase {
public:
    static constexpr int kNumVoices = 8;
    static constexpr int kLevelCvChannel =
        kNumVoices; // above the voices; reached through the Level fader, no gutter jack

    VoiceMixerModule()
        : ModuleBase("Voice Mixer", kLevelCvChannel + 1, 2) // 8 voice channels and the Level CV in, stereo out
    {
        addParameter(levelParam =
                         new juce::AudioParameterFloat("level", "Level", juce::NormalisableRange<float>(0.0f, 1.0f),
                                                       1.0f, synth::fractionPercentAttributes()));
    }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        prepareOutputStage(sampleRate, samplesPerBlock);
        smoothedLevel.reset(sampleRate, 0.01); // 10ms smoothing for anti-click
        smoothedLevel.setCurrentAndTargetValue(*levelParam);
        // The scratch the voices are summed through, sized here so a render never allocates.
        inputCopy.setSize(kNumVoices, std::max(samplesPerBlock, getBlockSize()), false, false, false);
    }

    void processModuleBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        juce::ignoreUnused(midiMessages);

        int numSamples = buffer.getNumSamples();
        int numChannels = buffer.getNumChannels();
        int voiceCount = std::min(kNumVoices, numChannels);

        if (numSamples == 0 || voiceCount == 0)
            return;

        // Taken and cleared before the sum, so the CV never leaks downstream.
        const float levelCv = blockCV(buffer, kLevelCvChannel);
        if (kLevelCvChannel < numChannels)
            buffer.clear(kLevelCvChannel, 0, numSamples);

        // Save the voice input before overwriting it (channels 0-7 are both input and output), a scratch-sized
        // chunk at a time so a block longer than the prepared size never needs a bigger scratch on the render
        // thread. Only a module rendered without ever being prepared sizes it here.
        if (inputCopy.getNumChannels() < voiceCount || inputCopy.getNumSamples() == 0)
            inputCopy.setSize(kNumVoices, numSamples, false, false, true);
        const int chunk = inputCopy.getNumSamples();
        for (int start = 0; start < numSamples; start += chunk) {
            const int n = std::min(chunk, numSamples - start);
            for (int ch = 0; ch < voiceCount; ++ch)
                inputCopy.copyFrom(ch, 0, buffer, ch, start, n);

            // Sum all voices into channel 0
            buffer.clear(0, start, n);
            for (int ch = 0; ch < voiceCount; ++ch)
                buffer.addFrom(0, start, inputCopy, ch, 0, n);
        }

        // Apply smoothed level; the shared soft limiter rounds only what passes 0.9
        smoothedLevel.setTargetValue(modulateNormalised(*levelParam, *levelParam, levelCv));
        float* outputData = buffer.getWritePointer(0);
        float peakIn = 0.0f;
        for (int s = 0; s < numSamples; ++s) {
            float raw = outputData[s];
            if (std::abs(raw) > peakIn)
                peakIn = std::abs(raw);
            float out = raw * smoothedLevel.getNextValue();
            outputData[s] = synth::softLimit(out);
        }

        // Copy to channel 1 for stereo output
        if (numChannels > 1)
            buffer.copyFrom(1, 0, buffer, 0, 0, numSamples);
    }

    juce::String getInputPortLabel(int i) const override {
        return i == kLevelCvChannel ? juce::String("Level") : "Voice " + juce::String(i);
    }
    LogicalPort mapInputChannel(int raw) const override {
        auto port = ModuleBase::mapInputChannel(raw);
        if (raw == kLevelCvChannel)
            port.role = PortRole::ModCV;
        return port;
    }
    std::vector<ModulationTarget> getModulationTargets() const override {
        return {{"Level", kLevelCvChannel, "level", true}};
    }
    // No output-side overrides and no Dual I/O registration: ModuleBase("Voice Mixer", 9, 2) matches
    // the StereoAudio::Auto shape, so the toggle and the collapsing "Audio"/Left/Right output jack
    // are inherited. Only the eight INPUT jacks are this module's own — which is exactly why the base
    // infers the output pair and never the input one.
    // Pure audio utility — processBlock's midiMessages parameter is unused.
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }

    ModuleType getModuleType() const override { return ModuleType::VoiceMixer; }

private:
    juce::AudioParameterFloat* levelParam;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedLevel;
    juce::AudioBuffer<float> inputCopy; // Sized in prepareToPlay so the render thread never allocates

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VoiceMixerModule)
};
