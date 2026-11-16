// Concern: the test-mode MIDI injector. It calls AudioEngine::handleIncomingMidiMessageFromSource, the seam every
// real juce::MidiInput callback goes through, so a Mackie Control bound to `source` (or a generic controller on it)
// hears the message exactly as from hardware. It runs on the message thread instead of a driver thread, which the
// sink's contract allows (it is thread-agnostic and lock-free).

#include "TestMode/TestModeMidiInject.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent/MainComponent.h"
#include "TestMode/TestModeComponents.h"

namespace synth::testmode {

namespace {

MainComponent* injectMainComponent() {
    if (auto* window = dynamic_cast<juce::ResizableWindow*>(findMainWindow()))
        return dynamic_cast<MainComponent*>(window->getContentComponent());
    return nullptr;
}

// {source: "<input identifier>", bytes: [0xE0, 0, 64]}: one complete message, sysex included (F0 ... F7).
StepResult cmdMidiInject(const juce::var& args, Scratch&, int) {
    auto* mc = injectMainComponent();
    if (mc == nullptr)
        return StepResult::failed("no main window");
    const auto source = args.getProperty("source", {}).toString();
    const auto* bytes = args.getProperty("bytes", {}).getArray();
    if (source.isEmpty() || bytes == nullptr || bytes->isEmpty())
        return StepResult::failed("midiInject takes {source: \"<input identifier>\", bytes: [..]}");
    juce::MemoryBlock data;
    for (const auto& byte : *bytes) {
        const int value = (int)byte;
        if (value < 0 || value > 255)
            return StepResult::failed("midiInject: every byte must be 0-255");
        const auto raw = (juce::uint8)value;
        data.append(&raw, 1);
    }
    int used = 0;
    const juce::MidiMessage message(data.getData(), (int)data.getSize(), used, 0, 0.0, false);
    if (used != (int)data.getSize())
        return StepResult::failed("midiInject: the bytes are not exactly one MIDI message");
    mc->getAudioEngine().handleIncomingMidiMessageFromSource(source, message);
    auto* o = new juce::DynamicObject();
    o->setProperty("sent", (int)data.getSize());
    return StepResult::done(juce::var(o));
}

} // namespace

void registerMidiInjectCommands(CommandTable& table) { table.add("midiInject", {cmdMidiInject}); }

} // namespace synth::testmode
