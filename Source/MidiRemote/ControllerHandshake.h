#pragma once

// FRO339 (docs/control/midi-remote-device-handshake.md#device-handshake): sends a ControllerProfile's declared
// open/close handshake bytes (RemoteModel.h's ControllerProfile::Handshake) to its own `input`
// device's matching MIDI output -- e.g. Novation LCXL3's DAW-mode enable/disable SysEx, which the
// device latches with no keep-alive (stays enabled until the close bytes arrive or it is
// power-cycled). Core: only RemoteFeedbackSink (already the app layer's juce::MidiOutput seam for
// controller feedback, FRO139) and RemoteModel.h -- no juce_audio_devices dependency of its own.

#include "MidiRemote/RemoteEngine/RemoteFeedbackSink.h"
#include "MidiRemote/RemoteModel.h"

#include <juce_core/juce_core.h>
#include <map>
#include <vector>

namespace synth::midi {

/**
 * @class ControllerHandshakeCoordinator
 * @brief Tracks which profiles currently have their handshake "open" sent, so reconcile() never
 *        double-sends open and never leaves a profile enabled with nothing to close it.
 *
 * MESSAGE THREAD ONLY -- reconcile()/shutdownAll() call into the injected RemoteFeedbackSink,
 * exactly like RemoteEngine::drain()'s own feedback send (Source/CLAUDE.md's MIDI Remote
 * threading rule): never called from a MIDI/audio thread. Standalone only -- HostMode::Hosted
 * never opens hardware MIDI, so its caller (MainComponent) never wires this in that mode.
 */
class ControllerHandshakeCoordinator {
public:
    /** `sink` outlives every call below -- owned by the app layer (MidiRemoteFeedbackOutputs),
     *  exactly like RemoteEngine::setFeedbackSink()'s own borrowed pointer. */
    explicit ControllerHandshakeCoordinator(RemoteFeedbackSink& sink)
        : sink_(sink) {}

    /** Sends `handshake.openMessage` for every profile that declares a non-empty handshake, whose
     *  input device is in `openSourceKeys` (AudioEngine::getOpenMidiInputIdentifiers(), matched by
     *  profile.input.identifier -- the same test MidiRemotePanelComponent::isProfilePresent uses),
     *  and that isn't already tracked as open. Sends `handshake.closeMessage` for every
     *  previously-open profile that no longer qualifies -- removed, its handshake cleared, or its
     *  device no longer open (unplugged) -- using the LAST profile state seen open, since a
     *  removed profile is no longer in `profiles` to re-derive it from. Call after every
     *  profile-list or open-device-set change (a template application, Add/Remove Controller, a
     *  replug, app startup). Idempotent: an unchanged set of open handshakes sends nothing. */
    void reconcile(const std::vector<ControllerProfile>& profiles, const std::vector<juce::String>& openSourceKeys);

    /** Sends `close` for every profile currently tracked as open, then forgets all of them --
     *  app quit/teardown. Call once, before the RemoteFeedbackSink this coordinator was built with
     *  goes away. */
    void shutdownAll();

private:
    struct OpenEntry {
        ControllerProfile::Handshake handshake;
        ControllerProfile::Input input; // the profile's own `input` -- also the handshake's output
    };

    RemoteFeedbackSink& sink_;
    // profileId -> what's currently open, so close can be sent for a profile no longer present in
    // the `profiles` reconcile() is next called with.
    std::map<juce::String, OpenEntry> open_;
};

} // namespace synth::midi
