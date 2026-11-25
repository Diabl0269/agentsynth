#pragma once

#include "ModuleBase.h"
#include "Modules/ParameterText.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_core/juce_core.h>
#include <memory>

/**
 * @class MidiKeyboardModule
 * @brief Provides a MIDI keyboard interface for note input.
 */
class MidiKeyboardModule
    : public ModuleBase
    , private juce::MidiKeyboardState::Listener {
public:
    MidiKeyboardModule();
    ~MidiKeyboardModule() override;

    // A MIDI source (the on-screen keys), like Track In / External MIDI — see ModuleBase's
    // isMidiInstrumentType comment: nothing should ever be wired into it.
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return true; }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void processModuleBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    juce::MidiKeyboardState& getKeyboardState() { return keyboardState; }
    ModuleType getModuleType() const override { return ModuleType::MidiKeyboard; }

private:
    struct Keys; // the lock-free key hand-off and the render thread's scratch, defined in the .cpp

    void handleNoteOn(juce::MidiKeyboardState*, int channel, int note, float velocity) override;
    void handleNoteOff(juce::MidiKeyboardState*, int channel, int note, float velocity) override;

    juce::MidiKeyboardState keyboardState;
    std::unique_ptr<Keys> keys;
    juce::AudioParameterInt* octaveParam;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiKeyboardModule)
};
