#pragma once

#include <juce_core/juce_core.h>

namespace synth {

// The command-line flag that puts the standalone app into automation-launch mode: never
// open an audio or MIDI device, never request microphone permission.
inline constexpr const char* kNoAudioDeviceFlag = "--no-audio-device";
// The environment-variable equivalent of kNoAudioDeviceFlag; accepts "1"/"true"/"yes" (trimmed,
// case-insensitive).
inline constexpr const char* kNoAudioDeviceEnvVar = "AGENTSYNTH_NO_AUDIO_DEVICE";

// True if `commandLineArgs` requests automation-launch mode via kNoAudioDeviceFlag, or if
// `envValue` (as read from kNoAudioDeviceEnvVar) does.
bool isNoAudioDeviceLaunch(const juce::StringArray& commandLineArgs, const juce::String& envValue);

// The command-line flag that keeps the sign-in token out of the platform credential store for
// this process (in memory only). Automation launches (kNoAudioDeviceFlag) imply it.
inline constexpr const char* kNoKeychainFlag = "--no-keychain";
// The environment-variable equivalent of kNoKeychainFlag; same "1"/"true"/"yes" semantics.
inline constexpr const char* kNoKeychainEnvVar = "AGENTSYNTH_NO_KEYCHAIN";

// True if `commandLineArgs` has kNoKeychainFlag, or `envValue` (kNoKeychainEnvVar) is truthy.
bool isNoKeychainRequested(const juce::StringArray& commandLineArgs, const juce::String& envValue);

// True when this launch must not touch the keychain: kNoKeychainFlag/env, or an automation launch
// (isNoAudioDeviceLaunch).
bool isKeychainFreeLaunch(const juce::StringArray& commandLineArgs, const juce::String& noKeychainEnv,
                          const juce::String& noAudioDeviceEnv);

// Called once at startup, before any AccountService or KeychainTokenStore exists: when
// isKeychainFreeLaunch() holds, switches KeychainTokenStore to its in-memory store for the
// process. Returns whether it did.
bool applyKeychainPolicyForLaunch(const juce::StringArray& commandLineArgs, const juce::String& noKeychainEnv,
                                  const juce::String& noAudioDeviceEnv);

} // namespace synth
