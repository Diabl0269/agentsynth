#include "ControllerProfileStore.h"
#include "UserSettings.h"

namespace synth {

namespace {
constexpr const char* kMidiRemoteFolderName = "MidiRemote";
constexpr const char* kControllersFolderName = "Controllers";
} // namespace

juce::File ControllerProfileStore::resolveDefaultControllersDirectory() {
    // userSettingsOptions().getDefaultFile() is pure path arithmetic (special-location +
    // applicationName/folderName + osxLibrarySubFolder) — it never touches the filesystem and
    // never constructs a juce::PropertiesFile (confirmed against
    // juce_PropertiesFile.cpp: getDefaultFile() computes the File; the PropertiesFile ctor calls
    // it and then separately calls reload(), which is the actual I/O). This store must never open
    // one either, so it stops at the parent directory rather than going through PropertiesFile at
    // all — a profile is a shareable document living ALONGSIDE the settings file, not inside it
    // (docs/midi_remote.md §7).
    const juce::File settingsFile = userSettingsOptions().getDefaultFile();
    return settingsFile.getParentDirectory().getChildFile(kMidiRemoteFolderName).getChildFile(kControllersFolderName);
}

ControllerProfileStore::ControllerProfileStore()
    : controllersDir_(resolveDefaultControllersDirectory()) {}

ControllerProfileStore::ControllerProfileStore(juce::File controllersDir)
    : controllersDir_(std::move(controllersDir)) {}

ControllerProfileStore::LoadAllResult ControllerProfileStore::loadAll() const {
    LoadAllResult result;
    if (!controllersDir_.isDirectory())
        return result; // no controllers configured yet — not an error.

    for (const auto& file : controllersDir_.findChildFiles(juce::File::findFiles, /*recursive=*/false, "*.json")) {
        const auto json = juce::JSON::parse(file);
        ControllerProfile profile;
        if (profile.fromVar(json))
            result.profiles.push_back(std::move(profile));
        else
            result.skippedFiles.push_back(file.getFileName());
    }
    return result;
}

bool ControllerProfileStore::save(const ControllerProfile& profile) const {
    if (profile.id.isEmpty())
        return false;
    if (!controllersDir_.exists() && !controllersDir_.createDirectory())
        return false;

    const auto file = controllersDir_.getChildFile(profile.id + ".json");
    return file.replaceWithText(juce::JSON::toString(profile.toVar()));
}

bool ControllerProfileStore::exportProfile(const juce::String& profileId, const juce::File& destFile) const {
    const auto srcFile = controllersDir_.getChildFile(profileId + ".json");
    if (!srcFile.existsAsFile())
        return false;
    return srcFile.copyFileTo(destFile);
}

bool ControllerProfileStore::importProfile(const juce::File& srcFile, ControllerProfile& outProfile) const {
    if (!srcFile.existsAsFile())
        return false;

    const auto json = juce::JSON::parse(srcFile);
    ControllerProfile parsed;
    if (!parsed.fromVar(json) || parsed.id.isEmpty())
        return false;

    const auto destFile = controllersDir_.getChildFile(parsed.id + ".json");
    // Name-conflict check (docs/midi_remote.md §7) — see the header's importProfile() comment for
    // why an id conflict and a destination-filename conflict are the same check here.
    if (destFile.existsAsFile())
        return false;

    if (!controllersDir_.exists() && !controllersDir_.createDirectory())
        return false;
    if (!srcFile.copyFileTo(destFile))
        return false;

    outProfile = std::move(parsed);
    return true;
}

} // namespace synth
