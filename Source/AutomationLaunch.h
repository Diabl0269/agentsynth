#pragma once

#include <juce_core/juce_core.h>
#include <optional>

namespace synth {

// The command-line flag that puts the standalone app into automation-launch mode: never
// open an audio or MIDI device, never request microphone permission.
inline constexpr const char* kNoAudioDeviceFlag = "--no-audio-device";
// The environment-variable equivalent of kNoAudioDeviceFlag; accepts "1"/"true"/"yes" (trimmed,
// case-insensitive).
inline constexpr const char* kNoAudioDeviceEnvVar = "AGENTSYNTH_NO_AUDIO_DEVICE";

// The command-line flag that starts the local test-mode server: `--automation <port>` (0 = any free port). It
// implies kNoAudioDeviceFlag and kNoKeychainFlag. The companions below only matter together with it.
inline constexpr const char* kAutomationFlag = "--automation";
// `--automation-motion` leaves the app's animations on (they are switched off under --automation otherwise).
inline constexpr const char* kAutomationMotionFlag = "--automation-motion";
// `--automation-info <file>` also writes {"port":N,"pid":P} to that file once the server listens.
inline constexpr const char* kAutomationInfoFlag = "--automation-info";
// `--settings-dir <absolute dir>` keeps every setting and user-data folder of this process under that directory.
inline constexpr const char* kSettingsDirFlag = "--settings-dir";

// True when the test-mode server was asked for (kAutomationFlag present, whatever follows it). Such a launch also
// stays off the network and off shared machine state: no update check, no telemetry, no eager plugin scan and no
// MIDI device opened, so any number of instances run side by side.
bool isAutomationLaunch(const juce::StringArray& commandLineArgs);

// The port after kAutomationFlag (0..65535), or nullopt when the flag is absent or not followed by such a number.
std::optional<int> automationPortFromArgs(const juce::StringArray& commandLineArgs);

// The value after `flag`, or an empty string when the flag is absent or last.
juce::String flagValueFromArgs(const juce::StringArray& commandLineArgs, const juce::String& flag);

// Called once at startup, before anything reads a setting: points settings and user data at the
// kSettingsDirFlag directory, or under kAutomationFlag at a fresh temporary one so a running real instance's
// settings file is never shared. Returns the directory now in use, or an empty string when nothing was redirected
// (or the given path was not absolute).
juce::String applySettingsDirForLaunch(const juce::StringArray& commandLineArgs);

// True if `commandLineArgs` requests automation-launch mode via kNoAudioDeviceFlag or kAutomationFlag, or if
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
