// Concern: automation-launch detection -- a pure, headless-testable helper so
// MainComponent's decision reduces to one function call instead of duplicating the
// flag/env-var parsing at every call site.

#include "AutomationLaunch.h"
#include "Auth/KeychainTokenStore.h"
#include "Modules/Sampler/SoundPackLibrary.h"
#include "UserSettings.h"

namespace synth {

namespace {
bool isTruthyEnv(const juce::String& envValue) {
    const juce::String trimmed = envValue.trim();
    return trimmed.equalsIgnoreCase("1") || trimmed.equalsIgnoreCase("true") || trimmed.equalsIgnoreCase("yes");
}
} // namespace

bool isAutomationLaunch(const juce::StringArray& commandLineArgs) { return commandLineArgs.contains(kAutomationFlag); }

bool isHiddenAutomationLaunch(const juce::StringArray& commandLineArgs) {
    return automationPortFromArgs(commandLineArgs).has_value() && !commandLineArgs.contains(kAutomationVisibleFlag);
}

std::optional<int> automationPortFromArgs(const juce::StringArray& commandLineArgs) {
    const int at = commandLineArgs.indexOf(kAutomationFlag);
    if (at < 0 || at + 1 >= commandLineArgs.size())
        return std::nullopt;
    const auto text = commandLineArgs[at + 1].trim();
    if (text.isEmpty() || !text.containsOnly("0123456789") || text.length() > 5)
        return std::nullopt;
    const int port = text.getIntValue();
    return port <= 65535 ? std::optional<int>(port) : std::nullopt;
}

// A plain decimal only, so a typo never starts playback at some surprising time; an hour is far past any measurement.
std::optional<double> playAfterSecondsFromArgs(const juce::StringArray& commandLineArgs) {
    if (!commandLineArgs.contains(kPlayAfterFlag))
        return std::nullopt;
    const auto text = flagValueFromArgs(commandLineArgs, kPlayAfterFlag);
    if (text.isEmpty() || !text.containsOnly("0123456789.") || text.indexOfChar('.') != text.lastIndexOfChar('.'))
        return std::nullopt;
    const double seconds = text.getDoubleValue();
    return seconds <= 3600.0 ? std::optional<double>(seconds) : std::nullopt;
}

juce::String flagValueFromArgs(const juce::StringArray& commandLineArgs, const juce::String& flag) {
    const int at = commandLineArgs.indexOf(flag);
    return (at >= 0 && at + 1 < commandLineArgs.size()) ? commandLineArgs[at + 1].trim().unquoted() : juce::String();
}

juce::String applySettingsDirForLaunch(const juce::StringArray& commandLineArgs) {
    juce::String dir = flagValueFromArgs(commandLineArgs, kSettingsDirFlag);
    if (dir.isNotEmpty() && !juce::File::isAbsolutePath(dir))
        dir = {};
    if (dir.isEmpty() && commandLineArgs.contains(kAutomationFlag)) {
        const auto fresh = juce::File::getSpecialLocation(juce::File::tempDirectory)
                               .getNonexistentChildFile(
                                   "agentsynth-automation-" + juce::String(juce::Time::currentTimeMillis()), {}, false);
        dir = fresh.getFullPathName();
    }
    if (dir.isEmpty())
        return {};
    juce::File(dir).createDirectory();
    setSettingsDirOverride(dir);
    setAutomationUserFolders(commandLineArgs.contains(kAutomationFlag));
    // A test run that was given its own SoundPacks folder sees only the packs in it (and downloads land there).
    const auto packs = juce::File(dir).getChildFile("SoundPacks");
    // A folder holding a file named `keep-bundled` still reads the app's own starter pack first (an "empty" user
    // folder).
    if (commandLineArgs.contains(kAutomationFlag) && packs.isDirectory())
        synth::sampler::SoundPackLibrary::setOnlyRoot(packs, packs.getChildFile("keep-bundled").existsAsFile());
    return dir;
}

bool isNoAudioDeviceLaunch(const juce::StringArray& commandLineArgs, const juce::String& envValue) {
    if (commandLineArgs.contains(kNoAudioDeviceFlag) || commandLineArgs.contains(kAutomationFlag))
        return true;

    return isTruthyEnv(envValue);
}

bool isMuteMetronomeRequested(const juce::StringArray& commandLineArgs, const juce::String& envValue) {
    return commandLineArgs.contains(kMuteMetronomeFlag) || isTruthyEnv(envValue);
}

bool isNoKeychainRequested(const juce::StringArray& commandLineArgs, const juce::String& envValue) {
    return commandLineArgs.contains(kNoKeychainFlag) || isTruthyEnv(envValue);
}

bool isTestAccountLaunch(const juce::StringArray& commandLineArgs, const juce::String& envValue) {
    return isAutomationLaunch(commandLineArgs) && (commandLineArgs.contains(kTestAccountFlag) || isTruthyEnv(envValue));
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
