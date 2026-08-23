#pragma once

#include "CableColour.h"
#include "NoteColour.h"
#include "Theme/ThemeManager.h"
#include <cmath>
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

    // ---- Testing hooks: real-layout regression coverage (bug: "Piano roll notes" invisible in
    // the actual Settings window) ----
    // Bounds are in the SCROLLED CONTENT's coordinate space (ContentHost below), which is what
    // resized() actually assigns them — the same space getContentHeightForTest() measures, so a
    // test can check both "was this laid out with real, non-empty space" and "does the scrollable
    // content area actually reach that far" without caring whether the Viewport's visible window
    // happens to be scrolled to it right now.
    juce::Rectangle<int> getNoteColoursTitleBoundsForTest() const { return noteColoursTitleLabel.getBounds(); }
    // Defined in the .cpp: NoteSwatchRow is only forward-declared here, so the header cannot
    // dereference it.
    juce::Rectangle<int> getNoteSwatchRowBoundsForTest() const;
    juce::Rectangle<int> getResetNoteColoursButtonBoundsForTest() const { return resetNoteColoursButton.getBounds(); }
    /** Full height of the scrollable content host — the ceiling a laid-out section's bottom edge
     *  must fall within for it to be reachable at all (by scrolling, if the viewport is shorter). */
    int getContentHeightForTest() const { return contentHost.getHeight(); }

    // ---- Testing hooks: theme-gallery wheel bubbling (bug: "vertical scroll only sometimes
    // works" in the Appearance tab) ----
    juce::Rectangle<int> getThemeListBoundsForTest() const { return themeList.getBounds(); }
    bool isThemeListScrollbarVisibleForTest() const { return themeList.getVerticalScrollBar().isVisible(); }
    int getThemeListScrollPositionForTest() const {
        return (int)std::llround(themeList.getVerticalScrollBar().getCurrentRangeStart());
    }
    /** The outer scrollable content's own vertical scroll position — what a wheel gesture that
     *  BUBBLES past the theme list (see ThemeListBox) should move. */
    int getContentScrollYForTest() const { return contentViewport.getViewPositionY(); }
    /** Synthesizes a wheel gesture over the theme list's own centre and routes it straight to
     *  `themeList.mouseWheelMove`, the same call juce::Desktop's real dispatch would make when the
     *  pointer sits there — a headless test has no OS to generate a real wheel event with. */
    void simulateWheelOverThemeListForTest(float deltaY) {
        juce::MouseWheelDetails wheel{};
        wheel.deltaY = deltaY;
        const auto pos = themeList.getBounds().getCentre().toFloat();
        juce::MouseEvent e(juce::Desktop::getInstance().getMainMouseSource(), pos, juce::ModifierKeys(), 0.0f, 0.0f,
                           0.0f, 0.0f, 0.0f, &themeList, &themeList, juce::Time::getCurrentTime(), pos,
                           juce::Time::getCurrentTime(), 1, false);
        themeList.mouseWheelMove(e, wheel);
    }

private:
    class CableSwatchRow; // strip of clickable colour swatches for the active mode
    class NoteSwatchRow;  // strip of clickable pitch-class swatches (piano roll note colours)
    class ThemeListModel; // juce::ListBoxModel drawing name + swatches; defined in .cpp? No —
                          // header-only tab keeps it inline. Implementers: define as a nested
                          // class here or in an AppearanceSettingsTab.cpp if they prefer; if a
                          // .cpp is added it MUST be added to BOTH CMakeLists (app + tests).
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

    // The scrolled content: a bare host whose paint delegates back to the tab so the section
    // dividers are drawn in the same coordinate space the controls are laid out in — same idiom as
    // ShortcutsSettingsTab::RowsHost. Needed because SettingsWindow opens this tab at a FIXED size
    // (MainComponent's settingsComp->setSize(500, 450), giving this tab roughly 498x419 once the
    // TabbedComponent's tab bar and outline are subtracted) that is smaller than every section's
    // combined natural height — without a Viewport, resized() used to lay the trailing sections
    // (part of Cables, and all of Piano Roll Notes) into an already-exhausted Rectangle, handing
    // them a zero-height juce::Rectangle: present in the tree via addAndMakeVisible, but with no
    // area to paint or hit-test, i.e. invisible.
    struct ContentHost : juce::Component {
        explicit ContentHost(AppearanceSettingsTab& o)
            : owner(o) {}
        void paint(juce::Graphics& g) override { owner.paintContent(g); }
        AppearanceSettingsTab& owner;
    };
    void paintContent(juce::Graphics& g);

    // juce::ListBox::mouseWheelMove (juce_ListBox.cpp) consumes the wheel whenever its OWN vertical
    // scrollbar is merely VISIBLE — with no "am I already at the limit" check the way
    // juce::Viewport itself has for its own wheel handling (Viewport::useMouseWheelMoveIfNeeded
    // only consumes when the position actually changes, else falls through to bubble). The theme
    // gallery is a ListBox nested inside this tab's OWN taller Viewport (contentViewport): with the
    // built-in default row height on ~4+ themes it needs its own scrollbar inside a fixed-height
    // 160px box, so a plain wheel gesture anywhere over the gallery always scrolled the LIST and
    // never the outer settings page, however far the list itself already was from its own top/
    // bottom — reading as "vertical scroll only sometimes works" depending on precisely where the
    // pointer sat. This restores Viewport's own parity: scroll the list when that would actually
    // move it, otherwise fall through to the base Component's default bubble-to-parent behaviour.
    class ThemeListBox : public juce::ListBox {
    public:
        void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override {
            auto& bar = getVerticalScrollBar();
            if (bar.isVisible()) {
                const auto before = bar.getCurrentRangeStart();
                juce::ListBox::mouseWheelMove(e, wheel);
                if (bar.getCurrentRangeStart() != before)
                    return; // actually scrolled the list -- consumed
            }
            // No internal scrollbar, or already at its limit in this direction: let the tab's own
            // Viewport have it, exactly like a plain juce::Component would.
            juce::Component::mouseWheelMove(e, wheel);
        }
    };

    juce::Viewport contentViewport;
    ContentHost contentHost{*this};

    synth::theme::ThemeManager& themeManager;
    juce::ApplicationProperties& appProperties;
    GraphEditor* graphEditor{nullptr}; // weak, owned by MainComponent
    ThemeListBox themeList;
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
