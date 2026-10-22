// Concern: automation-launch detection -- a pure, headless-testable helper so
// MainComponent's decision reduces to one function call instead of duplicating the
// flag/env-var parsing at every call site.

#include "AutomationLaunch.h"
#include "Auth/KeychainTokenStore.h"

namespace synth {

namespace {
bool isTruthyEnv(const juce::String& envValue) {
    const juce::String trimmed = envValue.trim();
    return trimmed.equalsIgnoreCase("1") || trimmed.equalsIgnoreCase("true") || trimmed.equalsIgnoreCase("yes");
}
} // namespace

bool isNoAudioDeviceLaunch(const juce::StringArray& commandLineArgs, const juce::String& envValue) {
    if (commandLineArgs.contains(kNoAudioDeviceFlag))
        return true;

    return isTruthyEnv(envValue);
}

bool isNoKeychainRequested(const juce::StringArray& commandLineArgs, const juce::String& envValue) {
    return commandLineArgs.contains(kNoKeychainFlag) || isTruthyEnv(envValue);
}

bool isKeychainFreeLaunch(const juce::StringArray& commandLineArgs, const juce::String& noKeychainEnv,
                          const juce::String& noAudioDeviceEnv) {
    return isNoKeychainRequested(commandLineArgs, noKeychainEnv) ||
           isNoAudioDeviceLaunch(commandLineArgs, noAudioDeviceEnv);
}

bool applyKeychainPolicyForLaunch(const juce::StringArray& commandLineArgs, const juce::String& noKeychainEnv,
                                  const juce::String& noAudioDeviceEnv) {
    const bool keychainFree = isKeychainFreeLaunch(commandLineArgs, noKeychainEnv, noAudioDeviceEnv);
    if (keychainFree)
        KeychainTokenStore::useInMemoryStoreForProcess(true);
    return keychainFree;
}

} // namespace synth
