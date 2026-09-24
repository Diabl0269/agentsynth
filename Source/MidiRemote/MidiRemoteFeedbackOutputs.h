#pragma once

// App-layer, NOT Core: implements synth::midi::RemoteFeedbackSink over a real juce::MidiOutput
// (FRO139, docs/control/midi-remote.md#controller-feedback) -- Core must never depend on
// juce_audio_devices (Source/CLAUDE.md's Core-layering rule; see RemoteFeedbackSink.h's own
// comment), so this lives here exactly like ControllerProfileStore does for its own app-layer
// concern. Owned by MainComponent, handed to RemoteEngine::setFeedbackSink() only in the standalone
// app (never in HostMode::Hosted -- a hosted plugin has no MIDI output of its own).

#include "MidiRemote/RemoteEngine/RemoteFeedbackSink.h"

#include <functional>
#include <map>
#include <set>

namespace synth::midi {

/**
 * @class MidiRemoteFeedbackOutputs
 * @brief One opened juce::MidiOutput per device identifier, cached for reuse, with a failure
 *        remembered rather than retried every drain tick.
 *
 * MESSAGE THREAD ONLY -- sendFeedback() is called from RemoteEngine::drain(), which never leaves
 * the message thread (see RemoteEngine.h's own class comment).
 */
class MidiRemoteFeedbackOutputs final : public RemoteFeedbackSink {
public:
    /** Opens `outputDevice` and returns a function that sends one juce::MidiMessage to it, or an
     *  empty std::function on failure. Test seam: a fake opener can hand back a recording lambda
     *  instead of touching real hardware. The real opener (the default) tries
     *  juce::MidiOutput::openDevice(outputDevice.identifier) first, then falls back to the first
     *  juce::MidiOutput::getAvailableDevices() entry whose name matches outputDevice.name. */
    using Opener = std::function<std::function<void(const juce::MidiMessage&)>(const ControllerProfile::Input&)>;

    MidiRemoteFeedbackOutputs();
    /** Test/injection constructor: `opener` replaces the real juce::MidiOutput path entirely. */
    explicit MidiRemoteFeedbackOutputs(Opener opener);
    ~MidiRemoteFeedbackOutputs() override;

    void sendFeedback(const ControllerProfile::Input& outputDevice, const juce::MidiMessage& message) override;

    /** Drops every cached open device and every remembered open failure -- call when the MIDI
     *  device set changes (AudioEngine::onMidiDevicesChanged), so a device that just reappeared (or
     *  a cached juce::MidiOutput now talking to a device that vanished) is retried fresh rather than
     *  staying wrong or silently dead for the rest of the session. */
    void closeAll();

private:
    Opener opener_;
    // identifier -> sender. A present-but-empty entry means "tried and failed" -- present, so
    // sendFeedback() never calls the real opener again for it until closeAll(), but empty, so
    // calling it is a checked no-op rather than a null-function-call crash.
    std::map<juce::String, std::function<void(const juce::MidiMessage&)>> senders_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiRemoteFeedbackOutputs)
};

} // namespace synth::midi
