// Source/Modules/TimelineModule.h
#pragma once
#include "../TransportManager.h"
#include "ModuleBase.h"

class TimelineModule : public ModuleBase {
public:
    TimelineModule()
        : ModuleBase("Timeline", 0, 1) {}

    ModuleType getModuleType() const override { return ModuleType::Timeline; }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        // Prepare timeline resources
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        if (!TransportManager::getInstance().isPlaying())
            return;
    }

    void releaseResources() override {}

    double getTailLengthSeconds() const override { return 0.0; }

    // Required by juce::AudioProcessor
    bool isMidiEffect() const override { return true; }

    // JUCE AudioProcessor overrides
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }

    // State management
    void getStateInformation(juce::MemoryBlock& destData) override {}
    void setStateInformation(const void* data, int sizeInBytes) override {}
};
