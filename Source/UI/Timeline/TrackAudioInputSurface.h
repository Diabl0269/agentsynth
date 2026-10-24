#pragma once

#include "Timeline/TrackAudioInput.h"

// TrackAudioInputSurface.h (docs/timeline/tracks.md#the-input-row): what the routing pane's audio Input row needs
// from the app -- the inputs on offer, their levels, turning them on and the Audio settings. Reached through ONE
// accessor on TrackHeaderHost (getAudioInputSurface(), null by default), like TrackChannelLinkSurface, so the host
// interface does not grow a method per input feature. MainComponent answers it from its AudioEngine.
namespace synth::ui {

struct TrackAudioInputSurface {
    virtual ~TrackAudioInputSurface() = default;

    /** The inputs on offer now. May probe the audio driver: call it on a refresh, never from a meter tick. */
    virtual synth::AudioInputStatus getAudioInputStatus() = 0;
    /** The loudest sample on 0-based input `channel` since the last read (reading resets it); 0 when it is off. */
    virtual float readInputPeak(int channel) = 0;
    /** "Turn on its inputs": asks for microphone access, then turns on the device's inputs. Lands asynchronously, as
     *  a device change; the app refreshes the pane then. */
    virtual void enableDeviceInputs() = 0;
    /** Opens Settings on its Audio tab. Not offered in a plugin, which has no such tab. */
    virtual void openAudioSettings() = 0;
    /** Whether track `id`'s sound reaches a mixer channel, i.e. whether another track can record it. A surface that
     *  cannot tell says yes. */
    virtual bool trackHasMixerChannel(synth::TrackId id) {
        juce::ignoreUnused(id);
        return true;
    }
};

} // namespace synth::ui
