#pragma once

#include <juce_core/juce_core.h>

namespace synth {

// The command-line flag that puts the standalone app into automation-launch mode (FRO29): never
// open an audio or MIDI device, never request microphone permission.
inline constexpr const char* kNoAudioDeviceFlag = "--no-audio-device";
// The environment-variable equivalent of kNoAudioDeviceFlag; accepts "1"/"true"/"yes" (trimmed,
// case-insensitive).
inline constexpr const char* kNoAudioDeviceEnvVar = "AGENTSYNTH_NO_AUDIO_DEVICE";

// True if `commandLineArgs` requests automation-launch mode via kNoAudioDeviceFlag, or if
// `envValue` (as read from kNoAudioDeviceEnvVar) does.
bool isNoAudioDeviceLaunch(const juce::StringArray& commandLineArgs, const juce::String& envValue);

} // namespace synth
