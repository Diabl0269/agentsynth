// Concern: MIDI input/capture — opening named MIDI devices, reconciling the open set against the
// Audio tab's ticked devices as it changes live, and dispatching incoming messages into the MIDI
// Remote sink, ExternalMidiModule nodes, and the MIDI collector.

#include "AudioEngine.h"
#include "MidiRemote/RemoteEngine/RemoteMessageSink.h"
#include "Modules/ExternalMidiModule.h"
#include <algorithm>

juce::Array<juce::MidiDeviceInfo> AudioEngine::availableMidiInputs() const {
    return juce::MidiInput::getAvailableDevices();
}

bool AudioEngine::openMidiInput(const juce::MidiDeviceInfo& info) {
    auto input = juce::MidiInput::openDevice(info.identifier, this);
    if (input == nullptr)
        return false;
    input->start();
    midiInputs.push_back(std::move(input));
    return true;
}

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

    for (auto& info : availableMidiInputs()) {
        if (info.name == deviceName) {
            openMidiInput(info);
            break;
        }
    }
}

void AudioEngine::openMidiDevicesForRemote(const std::vector<juce::String>& deviceNames) {
    for (const auto& name : deviceNames)
        ensureMidiDeviceOpen(name);
}

// FRO262: initialiseDevices() only ever opened the MIDI inputs available at that ONE moment (app
// launch). A controller plugged in afterwards -- or ticked in the Audio tab once CoreMIDI/the OS
// enumerates it, which can arrive after that launch loop has already run -- was invisible to MIDI
// Learn/MIDI Remote and to general MIDI input alike, because nothing ever re-ran the open loop.
// changeListenerCallback calls this on every AudioDeviceManager change broadcast (device picks,
// sample-rate changes, AND MIDI Input ticks all share the one broadcast) to keep midiInputs in
// step with the Audio tab's checkboxes.
//
// Open is additive: anything ticked that isn't open yet. Close is deliberately NOT "anything
// unticked" -- deviceManager's own enabled-device bookkeeping stays empty for every user who has
// never touched the Audio tab (no saved DEVICESETUP to restore it from), while the launch loop
// above opens EVERY available input regardless of that bookkeeping. Treating "not enabled" as
// "close it" would therefore close every MIDI input for those users on the very first change
// broadcast (deviceManager.initialise() can itself send one) -- a straight regression of the
// existing "MIDI just works out of the box" behaviour. Close is instead keyed on physical
// presence: a device that has vanished from availableMidiInputs() (unplugged) is removed so a
// later replug -- which JUCE will hand a fresh identifier or, on some backends, the same one --
// is never mistaken for "already open" by ensureMidiDeviceOpen()'s own by-name check.
bool AudioEngine::reconcileMidiInputs() {
    if (isHosted())
        return false;

    bool changed = false;
    const auto available = availableMidiInputs();

    for (auto& info : available) {
        if (!deviceManager.isMidiInputDeviceEnabled(info.identifier))
            continue;
        const bool alreadyOpen = std::any_of(midiInputs.begin(), midiInputs.end(), [&](const auto& input) {
            return input->getIdentifier() == info.identifier;
        });
        if (!alreadyOpen && openMidiInput(info))
            changed = true;
    }

    // Windows workaround inherited from shutdown()'s own midiInputs.clear() loop (see that
    // function's comment / commit f8d5b10, "wrap MIDI device creation in platform-specific macros
    // for Windows") -- juce::MidiInput::stop() during a live reconcile hits the same platform
    // hazard shutdown() was written to avoid, so a disconnected device on Windows stays in
    // midiInputs (harmlessly inert; its driver thread is already gone) rather than risk it.
#if JUCE_LINUX || JUCE_BSD || JUCE_MAC || JUCE_IOS
    for (auto it = midiInputs.begin(); it != midiInputs.end();) {
        const bool stillPresent = std::any_of(available.begin(), available.end(), [&](const auto& info) {
            return info.identifier == (*it)->getIdentifier();
        });
        if (!stillPresent) {
            (*it)->stop();
            it = midiInputs.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
#endif

    return changed;
}

std::vector<juce::String> AudioEngine::getOpenMidiInputIdentifiers() const {
    std::vector<juce::String> identifiers;
    identifiers.reserve(midiInputs.size());
    for (const auto& input : midiInputs)
        identifiers.push_back(input->getIdentifier());
    return identifiers;
}

void AudioEngine::handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message) {
    // The MIDI Remote source key is the device *identifier*, not getName()
    // (docs/control/midi-remote.md#the-plugin-build-vst3au-inside-a-host §6) — getIdentifier() returns a refcounted
    // juce::String copy, not an allocation. source == nullptr only from a test driving this override directly (see
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
    // message it consumes goes nowhere else
    // (docs/control/midi-remote.md#are-mapped-messages-consumed-or-also-forwarded-to-the-graph).
    //
    // This runs on a juce::MidiInput driver thread, which never enters a render pass — the old code
    // here (pre-FRO197) read remoteMessageSink_ unguarded, so setRemoteMessageSink(nullptr) could
    // return and the sink object be destroyed while this thread was already past the load, about to
    // call handleMessage on it: a teardown use-after-free. ScopedRemoteSinkCall's fetch_add, BEFORE
    // the pointer load below (not just wrapping the handleMessage call), fixes that: both it and the
    // pointer store/load are seq_cst, so either this fetch_add is globally ordered before
    // setRemoteMessageSink's store — in which case drainRemoteSinkCalls() must observe a nonzero
    // count and wait — or this thread's load reads the store's nullptr and skips the call entirely.
    // There is no interleaving where this thread can dereference a sink the message thread has
    // already been allowed to free.
    {
        const ScopedRemoteSinkCall remoteSinkGuard(remoteSinkCallsInFlight_);
        if (auto* sink = remoteMessageSink_.load(std::memory_order_seq_cst); sink != nullptr)
            if (sink->handleMessage(sourceKey, message))
                return;
    }

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
