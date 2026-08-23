#pragma once

#include "CableColour.h"
#include "NoteColour.h"
#include "Theme/ThemeManager.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>
class GraphEditor; // forward declaration for AppearanceSettingsTab

// The Settings "Appearance" tab. Lists all themes (built-in + user) as selectable rows with
// a small color-swatch preview, applies instantly via ThemeManager::setActiveTheme(), and
// offers "Open Themes Folder" (File::revealToUser) and "Reload Themes"
// (ThemeManager::loadUserThemesFromFolder). Highlights the active row; updates if the manager
// broadcasts (it is a ChangeListener so external changes reflect here too).
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
    void selectThemeRow(int row);         // simulates a click -> setActiveTheme
    void setGraphEditor(GraphEditor* ge); // called by MainComponent; pushes cable config too

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

    // ---- Piano roll note colours ----
    // Twelve pitch-class swatches (C..B), backed by synth::ui::NoteColourOverrides — the same
    // sparse-override shape PianoRollComponent will read from once the live piano roll wires up
    // to this wave's persistence (see NoteColour.h's file comment). An unset swatch draws the
    // active theme's noteFill in a hollow/dimmed treatment so "not set" reads differently from
    // "set to a colour that happens to look similar".
    static constexpr int kNoteSwatchCount = 12;
    juce::String getNoteSwatchLabel(int pitchClass) const;
    juce::Colour getNoteSwatchColour(int pitchClass) const;
    bool isNoteSwatchOverridden(int pitchClass) const noexcept;
    void setNoteSwatchColour(int pitchClass, juce::Colour colour);
    void resetNoteSwatch(int pitchClass);
    void resetAllNoteColours();

private:
    class CableSwatchRow; // strip of clickable colour swatches for the active mode
    class NoteSwatchRow;  // strip of clickable pitch-class swatches (piano roll note colours)
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

    // Section headers, styled identically (see sectionHeaderFont in the .cpp constructor) so
    // "Theme", "Theme Gallery" and "Cables" read as one family of group titles rather than one-offs.
    juce::Label themeSectionLabel;
    juce::Label themeGallerySectionLabel;

    juce::Label modeLabel;
    juce::ComboBox modeCombo;
    juce::Label defaultDarkLabel;
    juce::ComboBox defaultDarkCombo;
    juce::Label defaultLightLabel;
    juce::ComboBox defaultLightCombo;

    // Hairline rules between sections, painted in paint() from these bounds — same pattern as
    // PreferencesSettingsTab::dividerBounds.
    std::vector<juce::Rectangle<int>> dividerBounds;

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

    // ---- Piano roll note colours ----
    juce::Label noteColoursTitleLabel;
    std::unique_ptr<NoteSwatchRow> noteSwatchRow;
    juce::TextButton resetNoteColoursButton{"Reset Note Colours"};
    synth::ui::NoteColourOverrides noteColourOverrides;

    /** Opens a ColourPickerPopup (with favourites) for pitch class `pitchClass`. */
    void openNoteColourPicker(int pitchClass, juce::Rectangle<int> screenArea);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AppearanceSettingsTab)
};
