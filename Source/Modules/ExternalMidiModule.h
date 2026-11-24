#pragma once

#include "ModuleBase.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <memory>

namespace synth {
class RtMidiCollector;
}

class ExternalMidiModule : public ModuleBase {
public:
    ExternalMidiModule();
    ~ExternalMidiModule() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void processModuleBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    /** Any thread, one at a time: a MIDI driver callback. Never blocks the render thread. */
    void pushMidiMessage(const juce::MidiMessage& msg);

    void setMidiDeviceName(const juce::String& newName) { setModuleName(newName); }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return true; }

    ModuleType getModuleType() const override { return ModuleType::ExternalMidi; }

private:
    juce::AudioParameterInt* deviceIndexParam;
    juce::AudioParameterInt* channelParam;
    std::unique_ptr<synth::RtMidiCollector>
        collector; // lock-free: the render thread never waits on a MIDI driver thread
    int notesHeld = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExternalMidiModule)
};
