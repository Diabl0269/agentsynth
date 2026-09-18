// Concern: MIDI input/capture — opening named MIDI devices and dispatching incoming messages into
// the MIDI Remote sink, ExternalMidiModule nodes, and the MIDI collector.

#include "AudioEngine.h"
#include "MidiRemote/RemoteEngine/RemoteMessageSink.h"
#include "Modules/ExternalMidiModule.h"

void AudioEngine::ensureMidiDeviceOpen(const juce::String& deviceName) {
    // Hosted mode never opens hardware MIDI itself — the host owns device routing and forwards
    // note data through processBlock. Grabbing the port here would double-trigger every note.
    if (isHosted())
        return;

    for (auto& input : midiInputs) {
        if (input->getName() == deviceName) {
            return; // Already open
        }
    }

    for (auto& info : juce::MidiInput::getAvailableDevices()) {
        if (info.name == deviceName) {
            auto input = juce::MidiInput::openDevice(info.identifier, this);
            if (input != nullptr) {
                input->start();
                midiInputs.push_back(std::move(input));
            }
            break;
        }
    }
}

void AudioEngine::openMidiDevicesForRemote(const std::vector<juce::String>& deviceNames) {
    for (const auto& name : deviceNames)
        ensureMidiDeviceOpen(name);
}

std::vector<juce::String> AudioEngine::getOpenMidiInputIdentifiers() const {
    std::vector<juce::String> identifiers;
    identifiers.reserve(midiInputs.size());
    for (const auto& input : midiInputs)
        identifiers.push_back(input->getIdentifier());
    return identifiers;
}

void AudioEngine::handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message) {
    // The MIDI Remote source key is the device *identifier*, not getName() (docs/midi_remote.md
    // §6) — getIdentifier() returns a refcounted juce::String copy, not an allocation.
    // source == nullptr only from a test driving this override directly (see
    // Tests/Engine/DeviceChangeTests.cpp, Tests/Engine/BounceExporterTests.cpp).
    const juce::String sourceKey = source != nullptr ? source->getIdentifier() : juce::String();
    handleIncomingMidiMessageFromSource(sourceKey, message, source);
}

void AudioEngine::handleIncomingMidiMessageFromSource(const juce::String& sourceKey, const juce::MidiMessage& message,
                                                      juce::MidiInput* source) {
    // The external-MIDI interlock: a bounce sets this before a single sample renders and this is
    // the ONE place every delivery path (real hardware, and a direct test call) converges, so a
    // note arriving here is dropped rather than reaching ExternalMidiModule's own collector. See
    // suspendExternalMidi()'s comment for why this is a flag checked here rather than detaching
    // the underlying juce::MidiInput.
    if (externalMidiSuspended_.load(std::memory_order_acquire))
        return;

    // MIDI Remote gets first look, before the ExternalMidiModule fan-out and the collector push: a
    // message it consumes goes nowhere else (docs/midi_remote.md §4.3).
    if (auto* sink = remoteMessageSink_.load(std::memory_order_acquire); sink != nullptr)
        if (sink->handleMessage(sourceKey, message))
            return;

    for (auto* node : mainProcessorGraph.getNodes()) {
        if (auto* extMidi = dynamic_cast<ExternalMidiModule*>(node->getProcessor())) {
            // source == nullptr only from a test driving this path directly (see
            // Tests/Engine/DeviceChangeTests.cpp, Tests/Engine/BounceExporterTests.cpp) — real MIDI input
            // callbacks always hand back the device that called them.
            if (source != nullptr && source->getName() == extMidi->getName()) {
                extMidi->pushMidiMessage(message);
            }
        }
    }
    midiMessageCollector.addMessageToQueue(message);
}
