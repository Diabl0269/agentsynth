#pragma once

#include "AudioEngine/AudioInputLayoutStore.h"
#include "Timeline/TimelineDoc/TimelineTypes.h"
#include <cstdint>
#include <vector>

// The rules an Audio track's input follows, shared by the record path, the engine's monitoring and the routing pane
// so the three can never disagree: whether a chosen input can be heard or recorded now, and when a track plays its
// input live. Pure; message thread (a status carries a vector).
namespace synth {

// What the open audio device (or the host) offers as input right now.
struct AudioInputStatus {
    juce::String deviceName;      // the input device, or the one turning inputs on would open; empty when Hosted
    int deviceInputs = 0;         // inputs the device has
    int activeInputs = 0;         // inputs turned on and reaching the engine
    bool hosted = false;          // the host's inputs are all there is: no device name, nothing to turn on
    std::uint32_t activeMask = 0; // bit c set = 0-based input c is turned on
    // The device's inputs as the user laid them out (pairs and names, AudioInputLayoutStore::slotsFor); empty when
    // Hosted or when no interface is open.
    std::vector<InputSlot> layout;
    bool isChannelActive(int channel) const noexcept {
        return channel >= 0 && channel < 32 && (activeMask & (std::uint32_t{1} << channel)) != 0;
    }
    const InputSlot* findNamedInput(const juce::String& name) const noexcept {
        for (const auto& slot : layout)
            if (name.isNotEmpty() && slot.name == name)
                return &slot;
        return nullptr;
    }
    /** The layout's name for exactly this mono jack or pair; empty when it has none. */
    juce::String nameOfInput(int firstChannel, int numChannels) const {
        for (const auto& slot : layout)
            if (slot.firstJack == firstChannel && slot.numJacks == numChannels)
                return slot.name;
        return {};
    }
};

// Where a Device choice points now. A choice with a name is found by that name on the interface that is open (its
// jacks and channel count are the input's there); with no name it is the jacks it was picked on. A name the open
// interface does not have is not connected, whatever jacks it was picked on.
struct ResolvedAudioInput {
    bool connected = true;
    bool nameMissing = false; // named, and the open interface has no input of that name
    int firstChannel = 0;
    int numChannels = 1;
};

// Resolves `input` against what is open now: whether it reaches the engine, and through which jacks. Only a Device
// choice can be cut off: by a different device being open (an empty name on either side means "whatever is open", and
// the host's inputs carry no name), by an input name the open interface lacks, or by one of its channels being turned
// off. No input and the whole mix always count as connected; a Track choice depends on its source
// (isTrackInputConnected) and counts as connected here.
inline ResolvedAudioInput resolveAudioInput(const AudioInputChoice& input, const AudioInputStatus& status) {
    ResolvedAudioInput resolved;
    resolved.firstChannel = input.firstChannel;
    resolved.numChannels = input.numChannels;
    if (input.kind != AudioInputChoice::Kind::Device)
        return resolved;
    if (input.inputName.isNotEmpty() && !status.hosted) {
        const auto* slot = status.findNamedInput(input.inputName);
        if (slot == nullptr) {
            resolved.connected = false;
            resolved.nameMissing = true;
            return resolved;
        }
        resolved.firstChannel = slot->firstJack;
        resolved.numChannels = slot->numJacks;
        for (int channel = slot->firstJack; channel < slot->firstJack + slot->numJacks; ++channel)
            resolved.connected = resolved.connected && status.isChannelActive(channel);
        return resolved;
    }
    const bool sameDevice = status.hosted || input.deviceName.isEmpty() || status.deviceName.isEmpty() ||
                            input.deviceName == status.deviceName;
    if (!sameDevice) {
        resolved.connected = false;
        return resolved;
    }
    for (int channel = input.firstChannel; channel < input.firstChannel + input.numChannels; ++channel)
        resolved.connected = resolved.connected && status.isChannelActive(channel);
    return resolved;
}

inline bool isAudioInputConnected(const AudioInputChoice& input, const AudioInputStatus& status) {
    return resolveAudioInput(input, status).connected;
}

// What a Track choice's source looks like right now: whether the track still exists and whether its sound reaches a
// mixer channel (the strip a take is tapped from).
struct TrackInputSource {
    bool exists = false;
    bool hasChannel = false;
};

// Whether `input` can record now. Only a Track choice can be cut off: by its source being deleted or having no mixer
// channel. Every other kind is judged by isAudioInputConnected.
inline bool isTrackInputConnected(const AudioInputChoice& input, const TrackInputSource& source) noexcept {
    return input.kind != AudioInputChoice::Kind::Track || (source.exists && source.hasChannel);
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
