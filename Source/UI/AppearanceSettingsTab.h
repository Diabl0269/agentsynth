#pragma once

#include "Theme/ThemeManager.h"
#include <juce_gui_basics/juce_gui_basics.h>

// The Settings "Appearance" tab. Lists all themes (built-in + user) as selectable rows with
// a small color-swatch preview, applies instantly via ThemeManager::setActiveTheme(), and
// offers "Open Themes Folder" (File::revealToUser) and "Reload Themes"
// (ThemeManager::loadUserThemesFromFolder). Highlights the active row; updates if the manager
// broadcasts (it is a ChangeListener so external changes reflect here too).
// Also contains a toggle for alignment guides visibility.
class AppearanceSettingsTab
    : public juce::Component
    , private juce::ChangeListener {
public:
    AppearanceSettingsTab(gsynth::theme::ThemeManager& manager, juce::ApplicationProperties& props);
    ~AppearanceSettingsTab() override;

    void resized() override;
    void paint(juce::Graphics&) override;

    // Testing hooks (mirrors SettingsWindow's pattern).
    // Row count == the list model's row count == number of registered themes. (juce::ListBox
    // itself exposes no getNumRows(); read from the manager, which the model also defers to.)
    int getThemeRowCount() const { return (int)themeManager.getThemes().size(); }
    juce::String getSelectedThemeId() const;
    void selectThemeRow(int row); // simulates a click -> setActiveTheme

private:
    class ThemeListModel; // juce::ListBoxModel drawing name + swatches; defined in .cpp? No —
                          // header-only tab keeps it inline. Implementers: define as a nested
                          // class here or in an AppearanceSettingsTab.cpp if they prefer; if a
                          // .cpp is added it MUST be added to BOTH CMakeLists (app + tests).
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

    gsynth::theme::ThemeManager& themeManager;
    juce::ApplicationProperties& appProperties;
    juce::ListBox themeList;
    std::unique_ptr<ThemeListModel> listModel;
    juce::TextButton openFolderButton{"Open Themes Folder"};
    juce::TextButton reloadButton{"Reload Themes"};
    juce::Label titleLabel;
    juce::ToggleButton alignmentGuideToggle{"Show Alignment Guides"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AppearanceSettingsTab)
};
