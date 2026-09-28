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
#include <optional>
#include <vector>

namespace synth::midi {

// See ControllerHandshake.cpp for the port-hint/asymmetric-port rationale behind each of these.
juce::String applyHandshakePortHint(const juce::String& deviceName, const juce::String& portHint);
std::optional<ControllerProfile::Input>
resolveHandshakeOutput(const ControllerProfile::Input& input, const juce::String& portHint,
                       const std::vector<ControllerProfile::Input>& availableOutputs);
juce::String describeHandshakeIssue(const ControllerProfile::Input& input, const juce::String& portHint, bool resolved);

/** Tracks which profiles currently have their handshake "open" sent. MESSAGE THREAD ONLY -- see
 *  ControllerHandshake.cpp for the full contract. */
class ControllerHandshakeCoordinator {
public:
    /** `sink` outlives every call below. */
    explicit ControllerHandshakeCoordinator(RemoteFeedbackSink& sink)
        : sink_(sink) {}

    /** See ControllerHandshake.cpp's own doc comment above the definition. */
    void reconcile(const std::vector<ControllerProfile>& profiles, const std::vector<juce::String>& openSourceKeys,
                   const std::vector<ControllerProfile::Input>& availableOutputs);

    /** Sends `close` for every profile currently tracked as open, then forgets all of them --
     *  app quit/teardown. Call once, before `sink` goes away. */
    void shutdownAll();

    /** describeHandshakeIssue()'s result for `profileId` as of the last reconcile() call, or "". */
    juce::String getHandshakeIssue(const juce::String& profileId) const;

private:
    struct OpenEntry {
        ControllerProfile::Handshake handshake;
        ControllerProfile::Input output; // see ControllerHandshake.cpp's reconcile() comment
    };

    RemoteFeedbackSink& sink_;
    std::map<juce::String, OpenEntry> open_;
    std::map<juce::String, juce::String> issues_;
};

} // namespace synth::midi
