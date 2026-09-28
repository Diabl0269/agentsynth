// FRO339 (docs/control/midi-remote-device-handshake.md#device-handshake): wires ControllerHandshakeCoordinator to
// the real MIDI output sink and reconciles it at every point profiles_/the open-device set can
// change -- see MidiLearnController.h's own doc comments on each method below for why each call
// site needs it.

#include "MidiRemote/MidiLearnController.h"

#include "AudioEngine/AudioEngine.h"

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

// The shared "profiles_ changed" tail every FRO131 mutation (updateProfile, addProfile,
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
    if (engine_.isHosted())
        sources.push_back(hostSourceKey());
    handshakeCoordinator_->reconcile(profiles_, sources);
}

} // namespace synth::midi
