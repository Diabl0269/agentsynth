#include "PatchSaveLocation.h"
#include "ProjectBundle.h"

namespace synth {

PatchSaveMode PatchSaveLocation::modeFromString(const juce::String& text) {
    if (text == "global")
        return PatchSaveMode::Global;
    if (text == "custom")
        return PatchSaveMode::Custom;
    return PatchSaveMode::PerProject; // the default, and what an unknown/absent value means
}

juce::String PatchSaveLocation::modeToString(PatchSaveMode mode) {
    switch (mode) {
    case PatchSaveMode::Global:
        return "global";
    case PatchSaveMode::Custom:
        return "custom";
    case PatchSaveMode::PerProject:
    default:
        return "project";
    }
}

juce::File PatchSaveLocation::globalDirectory(const juce::File& projectsRoot) {
    auto dir = projectsRoot.getChildFile(kFolderName);
    dir.createDirectory();
    return dir;
}

juce::File PatchSaveLocation::resolve(PatchSaveMode mode, const juce::File& bundleDir, const juce::File& customDir,
                                      const juce::File& projectsRoot, bool* usedFallback) {
    if (usedFallback != nullptr)
        *usedFallback = false;

    if (mode == PatchSaveMode::PerProject && bundleDir != juce::File() && ProjectBundle::isBundle(bundleDir)) {
        auto dir = bundleDir.getChildFile(kFolderName);
        dir.createDirectory();
        return dir;
    }
    if (mode == PatchSaveMode::Custom && customDir != juce::File() && customDir.isDirectory())
        return customDir;

    if (mode != PatchSaveMode::Global && usedFallback != nullptr)
        *usedFallback = true;
    return globalDirectory(projectsRoot);
}

juce::File PatchSaveLocation::resolveFromSettings(const juce::PropertiesFile* settings, const juce::File& bundleDir,
                                                  const juce::File& projectsRoot) {
    if (settings == nullptr)
        return resolve(PatchSaveMode::PerProject, bundleDir, {}, projectsRoot);
    const auto mode = modeFromString(settings->getValue(kModeKey, "project"));
    const auto custom = settings->getValue(kCustomDirKey);
    return resolve(mode, bundleDir, custom.isEmpty() ? juce::File() : juce::File(custom), projectsRoot);
}

} // namespace synth
