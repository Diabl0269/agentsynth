#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace synth {

/** Where patch (`.json`) save/open dialogs start: with the open project, in one shared folder for
 *  every project, or in a folder the user picked. Stored in the user settings as `patchSaveMode`
 *  ("project" / "global" / "custom") plus `patchSaveCustomDir` (absolute path). */
enum class PatchSaveMode { PerProject, Global, Custom };

/** Resolves the effective patch folder for a mode. Pure and unit-testable: the bundle dir, the
 *  projects root and the settings are all passed in, never read from globals. */
struct PatchSaveLocation {
    static constexpr const char* kModeKey = "patchSaveMode";
    static constexpr const char* kCustomDirKey = "patchSaveCustomDir";
    /** Sub-folder name used both inside a bundle and under the projects root. */
    static constexpr const char* kFolderName = "Patches";

    static PatchSaveMode modeFromString(const juce::String& text);
    static juce::String modeToString(PatchSaveMode mode);

    /** `<projectsRoot>/Patches`, created on demand. */
    static juce::File globalDirectory(const juce::File& projectsRoot);

    /** The folder for `mode`, created on demand when it is one we own (Global / per-project).
     *  PerProject with no saved bundle (`bundleDir` empty or not a bundle) and Custom with a
     *  missing/empty folder both fall back to the Global folder; `usedFallback` reports it. */
    static juce::File resolve(PatchSaveMode mode, const juce::File& bundleDir, const juce::File& customDir,
                              const juce::File& projectsRoot, bool* usedFallback = nullptr);

    /** resolve() with the mode and custom folder read from `settings` (null = defaults). */
    static juce::File resolveFromSettings(const juce::PropertiesFile* settings, const juce::File& bundleDir,
                                          const juce::File& projectsRoot);
};

} // namespace synth
