#pragma once

#include "ModuleBase.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_core/juce_core.h>

/**
 * @class MidiKeyboardModule
 * @brief Provides a MIDI keyboard interface for note input.
 */
class MidiKeyboardModule : public ModuleBase {
public:
    MidiKeyboardModule()
        : ModuleBase("MIDI Keyboard", 0, 0) {
        addParameter(octaveParam = new juce::AudioParameterInt(juce::ParameterID("octave", 1), "Octave", -2, 2, 0));
    }

    // A MIDI source (the on-screen keys), like Track In / External MIDI — see ModuleBase's
    // isMidiInstrumentType comment: nothing should ever be wired into it.
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return true; }

    ~MidiKeyboardModule() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        juce::ignoreUnused(sampleRate, samplesPerBlock);
        keyboardState.reset();
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        if (isBypassed()) {
            buffer.clear();
            return;
        }

        juce::ignoreUnused(buffer);

        // Inject events from keyboard state
        // In a real JUCE app, this state would be connected to a MidiKeyboardComponent
        // For the module itself, it provides a bridge.

        int octaveShift = *octaveParam * 12;

        // Process messages from keyboardState
        // Note: MidiKeyboardState::processNextMidiBuffer adds events to the buffer
        keyboardState.processNextMidiBuffer(midiMessages, 0, buffer.getNumSamples(), true);

        // If octave shift is active, we'd need to manually transpose events.
        // For now, let's just use the keyboardState as is.
        // If we want octave shift, we might need a separate buffer or transpose events in midiMessages.
        if (octaveShift != 0) {
            juce::MidiBuffer transposedBuffer;
            for (const auto metadata : midiMessages) {
                auto msg = metadata.getMessage();
                if (msg.isNoteOn() || msg.isNoteOff() || msg.isAftertouch()) {
                    int note = msg.getNoteNumber() + octaveShift;
                    if (note >= 0 && note <= 127) {
                        msg.setNoteNumber(note);
                    }
                }
                transposedBuffer.addEvent(msg, metadata.samplePosition);
            }
            midiMessages.swapWith(transposedBuffer);
        }
    }

    juce::MidiKeyboardState& getKeyboardState() { return keyboardState; }
    ModuleType getModuleType() const override { return ModuleType::MidiKeyboard; }

private:
    juce::MidiKeyboardState keyboardState;
    juce::AudioParameterInt* octaveParam;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiKeyboardModule)
};
