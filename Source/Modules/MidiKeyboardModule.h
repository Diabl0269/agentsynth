#pragma once

#include "AudioEngine/SpscRing.h"
#include "ModuleBase.h"
#include "Modules/ParameterText.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_core/juce_core.h>

/**
 * @class MidiKeyboardModule
 * @brief Provides a MIDI keyboard interface for note input.
 */
class MidiKeyboardModule
    : public ModuleBase
    , private juce::MidiKeyboardState::Listener {
public:
    MidiKeyboardModule()
        : ModuleBase("MIDI Keyboard", 0, 0) {
        addParameter(octaveParam = new juce::AudioParameterInt(juce::ParameterID("octave", 1), "Octave", -2, 2, 0,
                                                               synth::octaveStepAttributes()));
        drained.reserve(kMaxPendingKeys);
        keyboardState.addListener(this);
    }

    // A MIDI source (the on-screen keys), like Track In / External MIDI — see ModuleBase's
    // isMidiInstrumentType comment: nothing should ever be wired into it.
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return true; }

    ~MidiKeyboardModule() override { keyboardState.removeListener(this); }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        juce::ignoreUnused(sampleRate, samplesPerBlock);
        // Not reset(): that forgets a held key with no note-off, and a hosted synth keeps its voices through a prepare.
        keyboardState.allNotesOff(0);
    }

    void processModuleBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        // Drained every block, bypassed or not: a bypassed keyboard must not hold keys back for a later block.
        drained.clear();
        pendingKeys.drain([&](const KeyEvent& e) { drained.push_back(e); }); // within reserve(): no allocation

        if (isBypassed()) {
            buffer.clear();
            return;
        }
        if (drained.empty())
            return;

        const int octaveShift = *octaveParam * 12;
        const int numSamples = buffer.getNumSamples();

        // The keys reach the render thread through a lock-free ring filled by this module's listener on the
        // keyboard state, never through MidiKeyboardState::processNextMidiBuffer: that takes the state's lock,
        // which the message thread holds on every key press. Placed the way that call placed them:
        // by press time, spread over the block.
        const int firstTime = drained.front().timeMs;
        const double scale = numSamples / static_cast<double>(drained.back().timeMs + 1 - firstTime);
        for (const auto& e : drained) {
            int note = e.note + octaveShift;
            if (note < 0 || note > 127)
                note = e.note; // out of range after the octave shift: left where it was
            const int pos =
                juce::jlimit(0, juce::jmax(0, numSamples - 1), juce::roundToInt((e.timeMs - firstTime) * scale));
            midiMessages.addEvent(e.on ? juce::MidiMessage::noteOn(e.channel, note, e.velocity)
                                       : juce::MidiMessage::noteOff(e.channel, note),
                                  pos);
        }
    }

    juce::MidiKeyboardState& getKeyboardState() { return keyboardState; }
    ModuleType getModuleType() const override { return ModuleType::MidiKeyboard; }

private:
    struct KeyEvent {
        std::int8_t channel;
        std::int8_t note;
        bool on;
        float velocity;
        int timeMs;
    };

    // Message thread (under the state's own lock, so one producer at a time) -> render thread.
    void handleNoteOn(juce::MidiKeyboardState*, int channel, int note, float velocity) override {
        pendingKeys.push({static_cast<std::int8_t>(channel), static_cast<std::int8_t>(note), true, velocity, nowMs()});
    }
    void handleNoteOff(juce::MidiKeyboardState*, int channel, int note, float velocity) override {
        pendingKeys.push({static_cast<std::int8_t>(channel), static_cast<std::int8_t>(note), false, velocity, nowMs()});
    }

    static int nowMs() noexcept { return static_cast<int>(juce::Time::getMillisecondCounter()); }

    static constexpr int kMaxPendingKeys = 1024;
    juce::MidiKeyboardState keyboardState;
    synth::SpscRing<KeyEvent> pendingKeys{kMaxPendingKeys};
    std::vector<KeyEvent> drained; // render thread's scratch, reserved once so a block never allocates
    juce::AudioParameterInt* octaveParam;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiKeyboardModule)
};
