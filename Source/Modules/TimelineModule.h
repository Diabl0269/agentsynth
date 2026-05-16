// Source/Modules/TimelineModule.h
#pragma once
#include "../TransportManager.h"
#include "ModuleBase.h"

class TimelineModule : public ModuleBase {
public:
    TimelineModule()
        : ModuleBase("Timeline", 0, 1) {} // No inputs, 1 MIDI output

    ModuleType getModuleType() const override { return ModuleType::Timeline; }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        if (!TransportManager::getInstance().isPlaying())
            return;
        // Logic to generate MIDI based on playhead position
    }
};
