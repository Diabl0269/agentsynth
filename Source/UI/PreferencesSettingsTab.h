#pragma once

#include "GraphEditor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <memory>
#include <optional>
#include <vector>

// The Settings "Preferences" tab (issues #216 / #217).
//
// Holds editor behaviour that is not appearance: smart-connection mode and double-click
// port disconnect. Each control persists through juce::ApplicationProperties and, when a
// GraphEditor is wired, pushes live so the canvas does not wait for a restart.
//
// NOTE: PreferencesSettingsTab.cpp MUST be added to BOTH the app target AND the test
// target in CMakeLists.txt.
class PreferencesSettingsTab : public juce::Component {
public:
    explicit PreferencesSettingsTab(juce::ApplicationProperties& props);
    ~PreferencesSettingsTab() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Called by SettingsWindow once the tab exists; pushes the persisted values onto the canvas.
    void setGraphEditor(GraphEditor* ge);

    // Testing hooks ---------------------------------------------------------
    GraphEditor::SmartConnectionMode getSmartConnectionMode() const;
    void setSmartConnectionMode(GraphEditor::SmartConnectionMode mode);
    bool isDoubleClickPortDisconnectEnabled() const;
    void setDoubleClickPortDisconnectEnabled(bool enabled);
    bool isAlignmentGuidesEnabled() const;
    void setAlignmentGuidesEnabled(bool enabled);
    bool getDefaultDualIOForNewModules() const;
    void setDefaultDualIOForNewModules(bool enabled);
    bool isLoopSelectionArmsEnabled() const;
    void setLoopSelectionArmsEnabled(bool enabled);
    bool isDoubleClickSpansLocatorsEnabled() const;
    void setDoubleClickSpansLocatorsEnabled(bool enabled);
    bool isNaturalScrollingEnabled() const;
    void setNaturalScrollingEnabled(bool enabled);
    bool isZoomScrollUpZoomsInEnabled() const;
    void setZoomScrollUpZoomsInEnabled(bool enabled);
    // "all" (every key labelled) vs "c" (only the Cs) — PianoRollComponent::KeyLabelMode, read by
    // TimelinePanelComponent::reloadPianoRollAppearancePrefs(). true == "all" (the default).
    bool isPianoRollKeyLabelModeAll() const;
    void setPianoRollKeyLabelModeAll(bool labelEveryKey);

    // Per-module overrides of getDefaultDualIOForNewModules() above ("Per-module I/O defaults..."
    // button). Keyed by module type (ModuleBase::getName(), e.g. "Reverb", "Filter"); a type with
    // no entry follows the global default — nullopt here means exactly that. Consumed by
    // GraphEditor::applyDefaultDualIOForNewModule, the same new-modules-only site the global
    // default itself is read from (see docs/fx_modules.md § Stereo I/O).
    std::optional<bool> getDualIOOverrideForType(const juce::String& moduleType) const;
    void setDualIOOverrideForType(const juce::String& moduleType, std::optional<bool> overrideValue);

    // Every module type that carries the Dual I/O parameter (granted by ModuleBase's constructor from
    // the module's channel shape), in the per-module popup's row order: the FX plus the split-block
    // voice modules
    // (docs/modules.md, docs/fx_modules.md § Stereo I/O).
    //
    // DISCOVERED, not hand-listed — a thin wrapper over synth::AIStateMapper::dualIOCapableModuleTypes(),
    // which probes the module factory and asks each module hasDualIOParameter(). This was a literal
    // list until the Ring Modulator turned out to be missing from it: the module is stereo, the
    // popup had no row for it, and nothing in the build noticed.
    static const std::vector<juce::String>& getDualIOModuleTypes();

    // Parses the "dualIOPerModuleDefaults" key straight from ApplicationProperties, independent of
    // any PreferencesSettingsTab instance. MainComponent calls this at startup to push the map into
    // the real GraphEditor before any tab exists — the same reason it re-reads
    // "defaultDualIOForNewModules" itself rather than waiting for Settings to be opened once.
    static std::map<juce::String, bool> loadDualIOPerModuleOverrides(juce::ApplicationProperties& props);

    // Test seam for the "Per-module I/O defaults..." popup: builds the exact content component the
    // button's onClick hands to a juce::CallOutBox, without launching the CallOutBox itself (which
    // needs real screen coordinates and, like every other control in this tab, cannot be driven
    // through a headless click — see the "NOT triggerClick()" comment on the tests above). One
    // juce::Label + juce::ComboBox pair per entry of getDualIOModuleTypes(), so a test can find the
    // Nth juce::ComboBox (or match by label text) and drive it with setSelectedId(id,
    // sendNotificationSync), exactly as it would a real click.
    std::unique_ptr<juce::Component> createDualIOPerModuleDefaultsPopupForTest();

    // Test-only: the hairline dividers paint() draws between preference groups, so a test can
    // assert one falls where the Dual I/O row ends without reaching into paint() itself.
    const std::vector<juce::Rectangle<int>>& getDividerBoundsForTest() const { return dividerBounds; }

    // Live filter across every preference row's label/tooltip text (round 3 follow-up item 2).
    // Setting the real searchField's text would also work, but that posts an async notification in
    // a real run — this drives the exact same code path (applySearchFilter) synchronously, the same
    // "set text without notification, then call the handler directly" idiom
    // ModuleLibraryComponent::setSearchText uses for its own headless tests.
    void setSearchFilterForTest(const juce::String& query);
    juce::String getSearchFilterForTest() const { return searchQuery; }
    // Invokes the search field's Esc handler exactly as a real key press would — the same "call the
    // callback directly" idiom TimelinePanelTests.cpp uses for its own onEscapeKey seam, since a
    // headless run cannot dispatch a real key event.
    void triggerSearchEscapeForTest() {
        if (searchField.onEscapeKey)
            searchField.onEscapeKey();
    }

private:
    void persistSmartConnectionMode(GraphEditor::SmartConnectionMode mode);
    void persistDoubleClickPortDisconnect(bool enabled);
    void persistAlignmentGuidesEnabled(bool enabled);
    void persistDefaultDualIOForNewModules(bool enabled);
    void persistLoopSelectionArms(bool enabled);
    void persistDoubleClickSpansLocators(bool enabled);
    void persistNaturalScrolling(bool enabled);
    void persistZoomScrollUpZoomsIn(bool enabled);
    void persistPianoRollKeyLabelMode(bool labelEveryKey);
    void persistDualIOPerModuleOverrides();

    // Shared by the real button and createDualIOPerModuleDefaultsPopupForTest() so the test seam
    // exercises the exact component a click would open, not a lookalike.
    std::unique_ptr<juce::Component> buildDualIOPerModuleDefaultsPopup();

    // Re-lays the tab for the current searchQuery: hides every row whose label/tooltip text does
    // not contain it (case-insensitive), collapsing the vertical gap and any now-orphaned divider.
    // Called from resized() and from every place searchQuery changes.
    void applySearchFilter(const juce::String& query);

    juce::ApplicationProperties& appProperties;
    GraphEditor* graphEditor{nullptr}; // weak, owned by MainComponent

    // Live filter field, top of the tab (SettingsWindow has no cross-tab search rig — see the class
    // comment above — so this is scoped to the Preferences tab, the same "per-tab, not per-window"
    // choice ModuleLibraryComponent's own search box makes for the module library). Esc clears it,
    // matching ModuleLibraryComponent::searchEditor's onEscapeKey.
    juce::TextEditor searchField;
    juce::String searchQuery; // trimmed, case-insensitive-compared in applySearchFilter/resized()

    juce::Label titleLabel;
    juce::Label smartConnectionLabel;
    juce::ComboBox smartConnectionCombo;
    juce::ToggleButton doubleClickDisconnectToggle{"Double-click port to disconnect"};
    // Moved here from AppearanceSettingsTab: this is canvas-editing behaviour (whether the graph
    // shows snap guides while dragging), the same family as the two toggles above it, not an
    // appearance/theme setting. Persistence key ("alignmentGuidesEnabled") is unchanged.
    juce::ToggleButton alignmentGuideToggle{"Show Alignment Guides"};
    // One line, one control: the old label + two-item ComboBox said the same thing in two widgets
    // and read as a mode picker rather than the on/off it actually is. The per-module override
    // button below shares this row (see resized()) rather than stacking under it — the two are one
    // group and read as one line, not a toggle followed by an unrelated row.
    juce::ToggleButton defaultDualIOToggle{"Split Left/Right jacks on new modules"};
    // Per-module overrides (Follow global / Always on / Always off) of the toggle above, one per
    // module type that carries the Dual I/O parameter — see buildDualIOPerModuleDefaultsPopup() and
    // the "dualIOPerModuleDefaults" JSON key.
    juce::TextButton perModuleDefaultsButton{"Per-module I/O defaults..."};
    juce::ToggleButton loopSelectionArmsToggle{"Timeline: P (loop selection) also switches looping on"};
    // The other half of the same locator conversation, so it sits in the same group as the row
    // above rather than getting a divider of its own: one is "make the locators from a selection",
    // this one is "make a clip from the locators".
    juce::ToggleButton doubleClickSpansLocatorsToggle{"Timeline: double-click inside the locators spans them"};
    juce::ToggleButton naturalScrollingToggle{"Natural scrolling"};
    // The one preference whose label needs a second line to explain WHICH surfaces it touches — a
    // bare "Natural scrolling" toggle in an app that also has a pannable canvas would read as
    // applying to everything.
    juce::Label naturalScrollingHint;
    // Sits directly under the natural-scrolling pair because it is the same gesture with a modifier
    // held, and users reach for both in the same visit. Independent of it, though: this one governs
    // the Cmd / Cmd+Shift wheel-ZOOM branches only, which is what its own caption spells out.
    juce::ToggleButton zoomScrollUpZoomsInToggle{"Scroll up to zoom in"};
    juce::Label zoomScrollUpZoomsInHint;
    // On (the default) labels every row in the piano roll's keys column; off labels only the Cs —
    // PianoRollComponent::KeyLabelMode::AllNotes / OctavesOnly.
    juce::ToggleButton pianoRollKeyLabelsToggle{"Label every key"};

    // Hairline rules between preference groups, painted in paint() from these bounds.
    std::vector<juce::Rectangle<int>> dividerBounds;

    // Per-module Dual I/O overrides, keyed by module type. Loaded once in the constructor via
    // loadDualIOPerModuleOverrides() and mutated only through setDualIOOverrideForType(), which
    // re-persists the whole map — small enough (one bool per module type) that there is no reason
    // to diff and write just the changed key.
    std::map<juce::String, bool> dualIOPerModuleOverrides;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PreferencesSettingsTab)
};
