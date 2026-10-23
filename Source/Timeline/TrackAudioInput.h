#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <cstdint>

// The rules an Audio track's input follows, shared by the record path, the engine's monitoring and the routing pane
// so the three can never disagree: whether a chosen input can be heard or recorded now, and when a track plays its
// input live. Pure and allocation-free; any thread.
namespace synth {

// What the open audio device (or the host) offers as input right now.
struct AudioInputStatus {
    juce::String deviceName;      // the input device, or the one turning inputs on would open; empty when Hosted
    int deviceInputs = 0;         // inputs the device has
    int activeInputs = 0;         // inputs turned on and reaching the engine
    bool hosted = false;          // the host's inputs are all there is: no device name, nothing to turn on
    std::uint32_t activeMask = 0; // bit c set = 0-based input c is turned on
    bool isChannelActive(int channel) const noexcept {
        return channel >= 0 && channel < 32 && (activeMask & (std::uint32_t{1} << channel)) != 0;
    }
};

// Whether `input` reaches the engine now. Only a Device choice can be cut off: by a different device being open (an
// empty name on either side means "whatever is open", and the host's inputs carry no name) or by one of its channels
// being turned off. No input and the whole mix always count as connected.
inline bool isAudioInputConnected(const AudioInputChoice& input, const AudioInputStatus& status) noexcept {
    if (input.kind != AudioInputChoice::Kind::Device)
        return true;
    const bool sameDevice = status.hosted || input.deviceName.isEmpty() || status.deviceName.isEmpty() ||
                            input.deviceName == status.deviceName;
    if (!sameDevice)
        return false;
    for (int channel = input.firstChannel; channel < input.firstChannel + input.numChannels; ++channel)
        if (!status.isChannelActive(channel))
            return false;
    return true;
}

// The monitor rule: On always plays the input, Off never; Auto plays it while the track is armed and the transport is
// either stopped (so the player hears themselves before pressing Record) or recording (so the take is heard as it is
// made), but not during plain playback, where the track's clips play instead.
inline bool monitorHearsInput(MonitorMode mode, bool armed, bool playing, bool recording) noexcept {
    switch (mode) {
    case MonitorMode::On:
        return true;
    case MonitorMode::Off:
        return false;
    case MonitorMode::Auto:
        return armed && (!playing || recording);
    }
    return false;
}

} // namespace synth
