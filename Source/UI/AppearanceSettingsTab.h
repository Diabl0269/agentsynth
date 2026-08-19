#pragma once

#include "CableColour.h"
#include "Theme/ThemeManager.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
class GraphEditor; // forward declaration for AppearanceSettingsTab

// The Settings "Appearance" tab. Lists all themes (built-in + user) as selectable rows with
// a small color-swatch preview, applies instantly via ThemeManager::setActiveTheme(), and
// offers "Open Themes Folder" (File::revealToUser) and "Reload Themes"
// (ThemeManager::loadUserThemesFromFolder). Highlights the active row; updates if the manager
// broadcasts (it is a ChangeListener so external changes reflect here too).
// Also contains a toggle for alignment guides.
class AppearanceSettingsTab
    : public juce::Component
    , private juce::ChangeListener {
public:
    AppearanceSettingsTab(synth::theme::ThemeManager& manager, juce::ApplicationProperties& props);
    ~AppearanceSettingsTab() override;

    void resized() override;
    void paint(juce::Graphics&) override;

    // Testing hooks (mirrors SettingsWindow's pattern).
    // Row count == the list model's row count == number of registered themes. (juce::ListBox
    // itself exposes no getNumRows(); read from the manager, which the model also defers to.)
    int getThemeRowCount() const { return (int)themeManager.getThemes().size(); }
    juce::String getSelectedThemeId() const;
    void selectThemeRow(int row);                 // simulates a click -> setActiveTheme
    void setAlignmentGuidesEnabled(bool enabled); // called by MainComponent to sync UI state
    void setGraphEditor(GraphEditor* ge);         // called by MainComponent; pushes cable config too

    // ---- Cable colours (issue #157) ----
    // The tab owns the persisted config; GraphEditor is handed the resolved values and never
    // touches ApplicationProperties itself.
    synth::ui::CableColourMode getCableColourMode() const noexcept { return cableColourMode; }
    void setCableColourMode(synth::ui::CableColourMode mode); // persists + repaints
    const synth::ui::CableColourOverrides& getCableColourOverrides() const noexcept { return cableColourOverrides; }

    /** Number of swatches shown for the active mode: 6 signal kinds or 8 module categories. */
    int getCableSwatchCount() const noexcept;
    /** Display label for swatch `index` under the active mode. */
    juce::String getCableSwatchLabel(int index) const;
    /** Colour swatch `index` currently resolves to (override if pinned, else the theme token). */
    juce::Colour getCableSwatchColour(int index) const;
    /** True when swatch `index` is pinned by the user rather than following the theme. */
    bool isCableSwatchOverridden(int index) const;
    /** Pin swatch `index` to `colour`; persists and repaints the canvas. */
    void setCableSwatchColour(int index, juce::Colour colour);
    /** Unpin swatch `index` so it follows the active theme again. */
    void resetCableSwatch(int index);
    /** Unpin every swatch in BOTH modes. */
    void resetAllCableColours();

private:
    class CableSwatchRow; // strip of clickable colour swatches for the active mode
    class ThemeListModel; // juce::ListBoxModel drawing name + swatches; defined in .cpp? No —
                          // header-only tab keeps it inline. Implementers: define as a nested
                          // class here or in an AppearanceSettingsTab.cpp if they prefer; if a
                          // .cpp is added it MUST be added to BOTH CMakeLists (app + tests).
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

    synth::theme::ThemeManager& themeManager;
    juce::ApplicationProperties& appProperties;
    GraphEditor* graphEditor{nullptr}; // weak, owned by MainComponent
    juce::ListBox themeList;
    std::unique_ptr<ThemeListModel> listModel;
    juce::TextButton openFolderButton{"Open Themes Folder"};
    juce::TextButton reloadButton{"Reload Themes"};
    juce::Label modeLabel;
    juce::ComboBox modeCombo;
    juce::Label defaultDarkLabel;
    juce::ComboBox defaultDarkCombo;
    juce::Label defaultLightLabel;
    juce::ComboBox defaultLightCombo;

    juce::ToggleButton alignmentGuideToggle{"Show Alignment Guides"};

    // ---- Cable colour controls ----
    juce::Label cablesTitleLabel;
    juce::Label cableModeLabel;
    juce::ComboBox cableModeCombo;
    std::unique_ptr<CableSwatchRow> cableSwatchRow;
    juce::TextButton resetCableColoursButton{"Reset Cable Colours"};

    synth::ui::CableColourMode cableColourMode = synth::ui::CableColourMode::BySignalType;
    synth::ui::CableColourOverrides cableColourOverrides;

    // Index of the swatch whose ColourSelector callout is open, or -1. The callout owns the
    // selector, so this is how changeListenerCallback knows what the user is editing.
    int activeSwatchIndex = -1;

    /** Pushes the current mode + overrides to the canvas. */
    void applyCableColoursToEditor();
    /** Opens a ColourSelector callout for swatch `index`. */
    void openCableColourPicker(int index, juce::Rectangle<int> screenArea);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AppearanceSettingsTab)
};
