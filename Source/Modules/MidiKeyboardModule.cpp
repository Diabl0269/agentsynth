#include "MidiKeyboardModule.h"

#include "AudioEngine/SpscRing.h"

namespace {
constexpr int kMaxPendingKeys = 1024;

struct KeyEvent {
    std::int8_t channel;
    std::int8_t note;
    bool on;
    float velocity;
    int timeMs;
};

int nowMs() noexcept { return static_cast<int>(juce::Time::getMillisecondCounter()); }
} // namespace

struct MidiKeyboardModule::Keys {
    synth::SpscRing<KeyEvent> pending{kMaxPendingKeys};
    std::vector<KeyEvent> drained; // render thread's scratch, reserved once so a block never allocates
};

MidiKeyboardModule::MidiKeyboardModule()
    : ModuleBase("MIDI Keyboard", 0, 0)
    , keys(std::make_unique<Keys>()) {
    addParameter(octaveParam = new juce::AudioParameterInt(juce::ParameterID("octave", 1), "Octave", -2, 2, 0,
                                                           synth::octaveStepAttributes()));
    keys->drained.reserve(kMaxPendingKeys);
    keyboardState.addListener(this);
}

MidiKeyboardModule::~MidiKeyboardModule() { keyboardState.removeListener(this); }

void MidiKeyboardModule::prepareToPlay(double sampleRate, int samplesPerBlock) {
    juce::ignoreUnused(sampleRate, samplesPerBlock);
    // Not reset(): that forgets a held key with no note-off, and a hosted synth keeps its voices through a prepare.
    keyboardState.allNotesOff(0);
}

void MidiKeyboardModule::processModuleBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    auto& drained = keys->drained;
    // Drained every block, bypassed or not: a bypassed keyboard must not hold keys back for a later block.
    drained.clear();
    keys->pending.drain([&](const KeyEvent& e) { drained.push_back(e); }); // within reserve(): no allocation

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

// Message thread (under the state's own lock, so one producer at a time) -> render thread.
void MidiKeyboardModule::handleNoteOn(juce::MidiKeyboardState*, int channel, int note, float velocity) {
    keys->pending.push({static_cast<std::int8_t>(channel), static_cast<std::int8_t>(note), true, velocity, nowMs()});
}

void MidiKeyboardModule::handleNoteOff(juce::MidiKeyboardState*, int channel, int note, float velocity) {
    keys->pending.push({static_cast<std::int8_t>(channel), static_cast<std::int8_t>(note), false, velocity, nowMs()});
}
