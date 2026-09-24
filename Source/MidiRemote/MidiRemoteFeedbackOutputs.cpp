// The real juce::MidiOutput opener (identifier match, then name-match fallback) and the
// cache/failure-remembering shell around it. See the header for the caller-facing contract.

#include "MidiRemote/MidiRemoteFeedbackOutputs.h"

#include <juce_audio_devices/juce_audio_devices.h>

namespace synth::midi {

namespace {

// The default Opener: owns the juce::MidiOutput itself inside the returned lambda's capture (a
// std::shared_ptr, since std::function must be copyable) -- there is no separate ownership slot for
// it in MidiRemoteFeedbackOutputs, deliberately, since the sender function IS the only thing that
// needs it.
std::function<void(const juce::MidiMessage&)> openRealDevice(const ControllerProfile::Input& outputDevice) {
    std::unique_ptr<juce::MidiOutput> device = juce::MidiOutput::openDevice(outputDevice.identifier);
    if (device == nullptr && outputDevice.name.isNotEmpty()) {
        // Identifier match failed (a device that changed identifier across a driver reinstall, or
        // simply isn't plugged in under that id right now) -- ControllerProfile::Input's own
        // fallback field, same "identifier, then name" convention every other MIDI-input lookup in
        // this codebase uses (AudioEngine::ensureMidiDeviceOpen).
        for (const auto& info : juce::MidiOutput::getAvailableDevices()) {
            if (info.name != outputDevice.name)
                continue;
            device = juce::MidiOutput::openDevice(info.identifier);
            break;
        }
    }
    if (device == nullptr)
        return {};

    std::shared_ptr<juce::MidiOutput> owned = std::move(device);
    return [owned](const juce::MidiMessage& message) { owned->sendMessageNow(message); };
}

} // namespace

MidiRemoteFeedbackOutputs::MidiRemoteFeedbackOutputs()
    : opener_(&openRealDevice) {}

MidiRemoteFeedbackOutputs::MidiRemoteFeedbackOutputs(Opener opener)
    : opener_(std::move(opener)) {}

MidiRemoteFeedbackOutputs::~MidiRemoteFeedbackOutputs() = default;

void MidiRemoteFeedbackOutputs::sendFeedback(const ControllerProfile::Input& outputDevice,
                                             const juce::MidiMessage& message) {
    if (outputDevice.identifier.isEmpty())
        return;

    auto it = senders_.find(outputDevice.identifier);
    if (it == senders_.end()) {
        auto sender = opener_ ? opener_(outputDevice) : std::function<void(const juce::MidiMessage&)>();
        it = senders_.emplace(outputDevice.identifier, std::move(sender)).first;
    }

    if (it->second)
        it->second(message);
    // An empty sender means the open already failed once this session (a device unplugged, or
    // never present) -- deliberately not retried until closeAll(), so a controller with no output
    // configured, or one that's absent, costs nothing on every 60 Hz drain tick.
}

void MidiRemoteFeedbackOutputs::closeAll() { senders_.clear(); }

} // namespace synth::midi
