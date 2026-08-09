#pragma once

#include "Theme.h"
#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace synth::theme {

// Registry of available themes (built-in + user), the active selection, persistence,
// and change notification. Persistence reuses an EXISTING juce::ApplicationProperties
// instance owned by MainComponent (key "themeId"). The manager never creates its own
// PropertiesFile.
//
// Lifetime: owned by the JUCEApplication subclass (Main.cpp) OR MainWindow; it must
// outlive every Component that reads it. See section 7.
class ThemeManager
    : public juce::ChangeBroadcaster
    , public juce::DarkModeSettingListener {
public:
    ThemeManager();
    ~ThemeManager() override;

    void darkModeSettingChanged() override;

    // Startup sequence in one call: register built-ins, load user JSON themes from the
    // user folder, then restore the persisted active id (falling back to the default
    // built-in if missing/invalid). Safe to call with props == nullptr in tests (then
    // nothing is persisted/restored and the default built-in is active).
    void initialise(juce::ApplicationProperties* props);

    // All themes in registration order: built-ins first, then user themes.
    const std::vector<Theme>& getThemes() const noexcept { return themes; }

    // The currently active theme (always valid after construction; defaults to the first
    // built-in even before initialise()).
    const Theme& getActiveTheme() const noexcept { return themes[(size_t)activeIndex]; }
    const juce::String& getActiveThemeId() const noexcept { return getActiveTheme().id; }

    // Select by id. Returns false (and does nothing) if id is unknown. On success:
    // updates the active index, persists "themeId" (if props set), and calls
    // sendChangeMessage() so listeners re-skin. Idempotent (selecting the active id is a no-op
    // that does NOT broadcast).
    bool setActiveTheme(const juce::String& id);

    // Register a user theme (e.g. parsed from JSON). If a theme with the same id already
    // exists it is REPLACED in place (so "Reload Themes" updates rather than duplicates).
    // Sets isUserTheme=true. Does not change the active selection. Does not broadcast.
    void addUserTheme(Theme theme);

    // Cross-platform: <userApplicationDataDirectory>/<synth::branding::kSettingsFolderName>/Themes .
    // Creates it if absent.
    static juce::File getUserThemesFolder();

    // Scan the user themes folder for "*.gtheme.json", parse each, addUserTheme() the valid
    // ones. Returns the count successfully loaded. Logs (once per failed file) on parse error.
    // Used at startup and by the "Reload Themes" button. Clears previously-loaded user themes
    // first (built-ins are preserved); if the active theme was a user theme that vanished,
    // falls back to the default built-in and broadcasts.
    int loadUserThemesFromFolder();

    enum class ThemeMode { Dark, Light, System };

    // Default theme accessors & mutators for Dark/Light defaults
    juce::String getDefaultDarkThemeId() const;
    bool setDefaultDarkThemeId(const juce::String& id);

    juce::String getDefaultLightThemeId() const;
    bool setDefaultLightThemeId(const juce::String& id);

    ThemeMode getThemeMode() const noexcept { return mode; }
    void setThemeMode(ThemeMode newMode);

    // Toggle between dark and light mode (switches active theme to default dark or default light theme)
    void toggleLightDarkMode();

    // The id of the default/fallback built-in (Obsidian). Used when persisted id is missing.
    static juce::String getDefaultThemeId() noexcept { return "obsidian"; }
    static juce::String getDefaultLightThemeFallbackId() noexcept { return "daylight"; }

private:
    void registerBuiltInThemes();                         // clears + adds the 3 built-ins
    int indexOfId(const juce::String& id) const noexcept; // -1 if not found

    std::vector<Theme> themes;
    int activeIndex{0};
    juce::String defaultDarkId{"obsidian"};
    juce::String defaultLightId{"daylight"};
    ThemeMode mode{ThemeMode::Dark};
    juce::ApplicationProperties* properties{nullptr}; // non-owning; may be null in tests

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ThemeManager)
};

} // namespace synth::theme
