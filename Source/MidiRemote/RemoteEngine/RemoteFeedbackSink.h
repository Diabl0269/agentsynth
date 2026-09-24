#pragma once

// The one seam AudioEngine's counterpart doesn't need, but the app layer does (FRO139,
// docs/control/midi-remote.md#controller-feedback): where RemoteEngine::drain() sends a mapped
// parameter's value back out to the controller that mapped it. A two-method interface in its own
// header for the same reason RemoteMessageSink.h is one -- Core must never depend on
// juce_audio_devices (juce::MidiOutput lives there), so the app layer implements this against a
// plain juce::MidiMessage and its own device-opening logic (Source/MidiRemote/
// MidiRemoteFeedbackOutputs.h).

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include "MidiRemote/RemoteModel.h"

namespace synth::midi {

/** Implemented by the app layer (MidiRemoteFeedbackOutputs); consulted by RemoteEngine's drain. */
class RemoteFeedbackSink {
public:
    virtual ~RemoteFeedbackSink() = default;

    /** MESSAGE THREAD. Send `message` out to `outputDevice` (a ControllerProfile's own `output`,
     *  never the dead Audio-tab selector). Fire-and-forget: a device that fails to open is the
     *  implementation's problem to remember and stop retrying, not this call's. */
    virtual void sendFeedback(const ControllerProfile::Input& outputDevice, const juce::MidiMessage& message) = 0;
};

} // namespace synth::midi
