// Wires ControllerHandshakeCoordinator to the real MIDI output sink and reconciles it at every
// point profiles_/the open-device set can change -- see MidiLearnController.h's own doc comments on
// each method below for why each call site needs it (see
// docs/control/midi-remote-device-handshake.md#device-handshake).

#include "MidiRemote/MidiLearnController.h"

#include "AudioEngine/AudioEngine.h"

#include <juce_audio_devices/juce_audio_devices.h>

namespace synth::midi {

// Called once, from MainComponent::wireMidiRemoteEngine() -- never in HostMode::Hosted (hardware
// MIDI stays closed there, so there is nothing to send a handshake to; every method here is then a
// no-op, since handshakeCoordinator_ stays unset). Re-reconciles immediately so a profile whose
// handshake was loaded before this call (ControllerProfileStore's own construction-time load,
// which runs before wireMidiRemoteEngine()) still gets sent.
void MidiLearnController::setHandshakeFeedbackSink(RemoteFeedbackSink& sink) {
    handshakeCoordinator_.emplace(sink);
    reconcileHandshakes();
}

// Wired from MainComponent's destructor, before remoteFeedbackOutputs_ (the sink) is destroyed --
// app quit must send `close` while the sink can still open a real juce::MidiOutput.
void MidiLearnController::shutdownHandshakes() {
    if (handshakeCoordinator_)
        handshakeCoordinator_->shutdownAll();
}

// The shared "profiles_ changed" tail every profile mutation (updateProfile, addProfile,
// deleteProfile, template application, undo/redo, ...) now calls instead of
// remoteEngine_.setProfiles(profiles_) directly, so neither the engine publish nor the handshake
// reconcile is ever forgotten at a new call site.
void MidiLearnController::setProfilesAndReconcileHandshakes() {
    remoteEngine_.setProfiles(profiles_);
    reconcileHandshakes();
}

// Also called from refreshSources(): a device opening/closing needs the same reconcile, with an
// unchanged profile list, since reconcile()'s "is this profile's device open" check depends on it.
void MidiLearnController::reconcileHandshakes() {
    if (!handshakeCoordinator_)
        return;
    auto sources = engine_.getOpenMidiInputIdentifiers();

    // ResolveHandshakeOutput() needs every OUTPUT the app can currently see -- real
    // juce::MidiOutput enumeration in production, availableOutputsQuery_ in a test (see its own doc
    // comment on why: a headless test process has no CoreMIDI entitlement/bundle).
    std::vector<ControllerProfile::Input> availableOutputs;
    if (availableOutputsQuery_) {
        availableOutputs = availableOutputsQuery_();
    } else {
        for (const auto& info : juce::MidiOutput::getAvailableDevices())
            availableOutputs.push_back({info.identifier, info.name});
    }

    if (engine_.isHosted()) {
        // Hosted's synthetic "device" has no real juce::MidiInput/MidiOutput behind it -- mirror the
        // input side's own hostSourceKey() convenience so a Hosted-mode profile (identifier ==
        // hostSourceKey()) still resolves an "output" to send its handshake to via the identifier
        // match, exactly like MidiLearnControllerHandshakeTests.cpp's own fixture expects.
        sources.push_back(hostSourceKey());
        availableOutputs.push_back({hostSourceKey(), hostSourceKey()});
    }

    handshakeCoordinator_->reconcile(profiles_, sources, availableOutputs);
    // Feedback rides the same resolved output: the handshake's port is the only one the
    // device listens on in DAW mode.
    remoteEngine_.setHandshakeFeedbackOutputs(handshakeCoordinator_->getOpenOutputs());
}

juce::String MidiLearnController::getHandshakeIssueForProfile(const juce::String& profileId) const {
    return handshakeCoordinator_ ? handshakeCoordinator_->getHandshakeIssue(profileId) : juce::String();
}

} // namespace synth::midi
