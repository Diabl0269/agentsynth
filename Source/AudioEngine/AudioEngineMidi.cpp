// Concern: MIDI input/capture — opening named MIDI devices and dispatching incoming messages into ExternalMidiModule
// nodes and the MIDI collector.

#include "AudioEngine.h"
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

void AudioEngine::handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message) {
    // The external-MIDI interlock: a bounce sets this before a single sample renders and this is
    // the ONE place every delivery path (real hardware, and a direct test call) converges, so a
    // note arriving here is dropped rather than reaching ExternalMidiModule's own collector. See
    // suspendExternalMidi()'s comment for why this is a flag checked here rather than detaching
    // the underlying juce::MidiInput.
    if (externalMidiSuspended_.load(std::memory_order_acquire))
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
