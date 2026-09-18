#pragma once

// The one seam AudioEngine gains for MIDI Remote (docs/control/midi-remote.md#the-engine). Deliberately a
// two-method interface in its own header so Source/AudioEngine/ depends on *this* and never on
// RemoteEngine.h — the engine, its snapshot machinery and the whole MidiRemote model stay out of
// the audio engine's translation units.
//
// THREADING: handleMessage is called on a MIDI driver thread (standalone, one thread per open
// juce::MidiInput, several of which may run concurrently) or on the audio thread (HostMode::Hosted,
// per message from processHostBlock's input buffer). Implementations must be lock-free,
// allocation-free and logging-free. See Source/CLAUDE.md's MIDI-path tripwire.

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

namespace synth::midi {

/** Consulted by AudioEngine::handleIncomingMidiMessage *before* the MidiMessageCollector push and
 *  the ExternalMidiModule fan-out. */
class RemoteMessageSink {
public:
    virtual ~RemoteMessageSink() = default;

    /** MIDI/AUDIO THREAD. Returns true if the message was consumed by the remote engine, in which
     *  case the caller must not forward it anywhere else
     * (docs/control/midi-remote.md#are-mapped-messages-consumed-or-also-forwarded-to-the-graph). A profile with
     *  passMapped == true applies the message and still returns false. `sourceKey` is the
     *  juce::MidiInput device *identifier* (standalone) or kHostSourceKey (hosted). */
    virtual bool handleMessage(const juce::String& sourceKey, const juce::MidiMessage& message) noexcept = 0;
};

/** The single pseudo-controller source key in HostMode::Hosted
 * (docs/control/midi-remote.md#the-plugin-build-vst3au-inside-a-host). A function-local static, never a temporary:
 * constructing a juce::String on the MIDI path would allocate. */
const juce::String& hostSourceKey() noexcept;

} // namespace synth::midi
