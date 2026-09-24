#pragma once

// App-layer glue between the two MIDI Remote keys in the shared settings file (UserSettings.h) and
// the values the rest of the app uses. Core never reads settings, so RemoteEngine only ever sees
// the result of these (docs/control/midi-remote-ui.md#settings).

#include "MidiRemote/RemoteModel.h"
#include "UserSettings.h"

namespace synth::midi {

/** Never returns Takeover::useDefault — a default cannot itself defer to the default. */
inline Takeover loadDefaultTakeover(const juce::PropertiesFile& settings) {
    const auto value = settings.getValue(kMidiRemoteDefaultTakeoverSettingKey, "scale");
    if (value == "jump")
        return Takeover::jump;
    if (value == "pickup")
        return Takeover::pickup;
    return Takeover::scale;
}

inline const char* takeoverSettingValue(Takeover takeover) {
    switch (takeover) {
    case Takeover::jump:
        return "jump";
    case Takeover::pickup:
        return "pickup";
    default:
        return "scale";
    }
}

inline bool loadShowBadges(const juce::PropertiesFile& settings) {
    return settings.getBoolValue(kMidiRemoteShowBadgesSettingKey, true);
}

} // namespace synth::midi
