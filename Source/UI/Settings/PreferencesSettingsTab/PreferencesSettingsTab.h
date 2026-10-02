#pragma once

#include "MidiRemote/RemoteModel.h"
#include "PatchSaveLocation.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Layout/ArrowKeyNavigation.h"
#include "UI/Layout/DialogKeyboard.h"
#include "UI/Layout/FoldAllButton.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <memory>
#include <optional>
#include <vector>

// The Settings "Preferences" tab.
//
// Holds editor behaviour that is not appearance: smart-connection mode and double-click
// port disconnect. Each control persists through juce::ApplicationProperties and, when a
// GraphEditor is wired, pushes live so the canvas does not wait for a restart.
//
// The rows are grouped into categories (Category below), picked from a drop-down under the title;
// only the selected category's rows are laid out, inside a scrolling viewport. "All" shows every
// category under its own collapsible header. A search query looks across every category.
// Adding a row: docs/layout/settings-preferences.md.
//
// NOTE: PreferencesSettingsTab.cpp MUST be added to BOTH the app target AND the test
// target in CMakeLists.txt.
class PreferencesSettingsTab : public juce::Component {
public:
    explicit PreferencesSettingsTab(juce::ApplicationProperties& props);
    ~PreferencesSettingsTab() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // The category picker's entries, in drop-down order. Each layout*Groups unit lays out exactly
    // one category (its rows), so a new row goes into the unit named for its category.
    // All is last in the enum (so the other ids stay put) but first in the drop-down; it is never a
    // layoutCategory, only a selection that shows every category under a collapsible header.
    enum class Category { Graph, Timeline, Files, Mixer, Panels, MidiRemote, All };
    static constexpr int kNumSections = 6; // every Category before All
    static juce::String categoryName(Category category);
    Category getSelectedCategory() const { return selectedCategory; }
    // Same path as picking the entry in the drop-down (fires the combo's onChange synchronously).
    void setSelectedCategory(Category category);

    // Called by SettingsWindow once the tab exists; pushes the persisted values onto the canvas.
    void setGraphEditor(GraphEditor* ge);

    // Testing hooks ---------------------------------------------------------
    GraphEditor::SmartConnectionMode getSmartConnectionMode() const;
    void setSmartConnectionMode(GraphEditor::SmartConnectionMode mode);
    bool isDoubleClickPortDisconnectEnabled() const;
    void setDoubleClickPortDisconnectEnabled(bool enabled);
    // Plain on/off, ON by default
    // (see docs/layout/module-card.md#deleting-a-module-reconnect-the-chain-fro23).
    bool isReconnectChainOnDeleteEnabled() const;
    void setReconnectChainOnDeleteEnabled(bool enabled);
    // Plain on/off, unlike getMacroAutoPortPreference() below — these are automations that are ON by default with
    // a plain escape hatch, not a replacement for pre-existing silent behaviour (which is why that one is a
    // tri-state "ask") (see docs/macros/auto-ports.md#ports-on-a-cable-drag).
    bool isMacroAutoCreatePortsOnDragEnabled() const;
    void setMacroAutoCreatePortsOnDragEnabled(bool enabled);
    bool isMacroAutoDeletePortsOnLastCableEnabled() const;
    void setMacroAutoDeletePortsOnLastCableEnabled(bool enabled);
    // Plain on/off, OFF by default — see the .cpp for what it gates.
    bool isMacroSpliceCableOnPortDeleteEnabled() const;
    void setMacroSpliceCableOnPortDeleteEnabled(bool enabled);
    // Reparent a module by dragging it across a macro hull without holding Cmd. Plain on/off, ON by
    // default (Cmd works either way) (see docs/macros/menu-and-membership.md).
    bool isMacroDragWithoutCmdEnabled() const;
    void setMacroDragWithoutCmdEnabled(bool enabled);
    // Drag empty space inside an expanded macro's outline to move the macro instead of panning.
    // Plain on/off, OFF by default (docs/layout/macro-cards.md).
    bool isMoveMacroOnHullDragEnabled() const;
    void setMoveMacroOnHullDragEnabled(bool enabled);
    // Plain on/off, ON by default — same shape as the two macro auto-port toggles above (a brand-new automation, not a
    // replacement for pre-existing silent behaviour)
    // (see docs/mixer/mixer.md#channels-follow-audio-not-tracks "main workflow").
    bool isMixerAutoCreateChannelOnConnectEnabled() const;
    void setMixerAutoCreateChannelOnConnectEnabled(bool enabled);
    bool isAlignmentGuidesEnabled() const;
    void setAlignmentGuidesEnabled(bool enabled);
    bool getDefaultDualIOForNewModules() const;
    void setDefaultDualIOForNewModules(bool enabled);
    // Whether grouping a selection with a crossing cable into a macro auto-creates matching ports, leaves the cables as
    // they are, or asks every time (the default). "Always ask" here is what lets a user who picked a side once
    // reconsider — GraphEditor's own modal offers no such way back in, only "remember this choice"
    // (see docs/macros/auto-ports.md#auto-creating-ports-when-grouping).
    GraphEditor::MacroAutoPortPreference getMacroAutoPortPreference() const;
    void setMacroAutoPortPreference(GraphEditor::MacroAutoPortPreference pref);
    bool isLoopSelectionArmsEnabled() const;
    void setLoopSelectionArmsEnabled(bool enabled);
    bool isDoubleClickSpansLocatorsEnabled() const;
    void setDoubleClickSpansLocatorsEnabled(bool enabled);
    bool isAskBeforeRemovingLfoEnabled() const;
    void setAskBeforeRemovingLfoEnabled(bool enabled);
    bool isNaturalScrollingEnabled() const;
    void setNaturalScrollingEnabled(bool enabled);
    bool isZoomScrollUpZoomsInEnabled() const;
    void setZoomScrollUpZoomsInEnabled(bool enabled);
    // Autosave: periodic sidecar save of the open bundle, gated on the edit serial having
    // moved since the last one — see MainComponent::maybeAutosave. DEFAULT ON at 2 minutes.
    bool isAutosaveEnabled() const;
    void setAutosaveEnabled(bool enabled);
    // Any exact integer 1-120 (a plain digits-only juce::TextEditor field, not a fixed-choice combo
    // or a slider with +/- buttons — see the constructor), in minutes.
    int getAutosaveIntervalMinutes() const;
    void setAutosaveIntervalMinutes(int minutes);
    // Cubase-style rotating backup history (see ProjectBundle::saveAutosave): how many PREVIOUS
    // autosave snapshots are kept as numbered autosave-<n>.json files alongside the live
    // autosave.json. Any exact integer 0-50; 0 disables the backup history entirely. DEFAULT 5.
    int getAutosaveBackupCount() const;
    void setAutosaveBackupCount(int count);
    // Per-type default track preset. Empty string == "Factory Default" (the sentinel row, id
    // kMixerDefaultPresetFactoryComboId) == the unchanged buildDefaultAudioChannel-based chain; a non-empty
    // name that no longer resolves to a listed preset is silently ignored by the setter (the combo keeps
    // its current selection) (see docs/mixer/track-presets.md#saving-and-setting-a-default).
    juce::String getMixerDefaultTrackPresetAudio() const;
    void setMixerDefaultTrackPresetAudio(const juce::String& presetName);
    juce::String getMixerDefaultTrackPresetInstrument() const;
    void setMixerDefaultTrackPresetInstrument(const juce::String& presetName);
    // Where the Mixer panel lives -- "tab"/"ownPanel"/"window", default "tab".
    // Read once at launch by MainComponent/MixerPlacementController and
    // re-applied immediately on every change (no restart) via the same settings-file
    // ChangeListener every other live preference here uses (see docs/mixer/panel.md).
    juce::String getMixerPlacement() const;
    void setMixerPlacement(const juce::String& placement);
    // "When a panel opens in its own window" -- "move" (default) or "both". Read once at
    // launch by BottomDockComponent and re-applied live the same way as getMixerPlacement()
    // above. Only the Mixer honours "both" today (see
    // BottomDockComponent::usesMixerMirrorForDetach and
    // docs/mixer/panel.md#placement-and-detachable-windows).
    juce::String getPanelDetachMode() const;
    void setPanelDetachMode(const juce::String& mode);
    // Where patch save/open dialogs start (Files & Autosave): with the project (default), the shared
    // Patches folder, or a chosen folder. Read at use time by MainComponent through
    // synth::PatchSaveLocation; see docs/layout/settings-preferences.md.
    synth::PatchSaveMode getPatchSaveMode() const;
    void setPatchSaveMode(synth::PatchSaveMode mode);
    juce::File getPatchSaveCustomFolder() const;
    void setPatchSaveCustomFolder(const juce::File& folder);
    // MIDI Remote group (docs/control/midi-remote-ui.md#settings). Default takeover is never
    // Takeover::useDefault; both are picked up live by MainComponent via the settings file.
    synth::Takeover getMidiRemoteDefaultTakeover() const;
    void setMidiRemoteDefaultTakeover(synth::Takeover takeover);
    bool isMidiRemoteShowBadgesEnabled() const;
    void setMidiRemoteShowBadgesEnabled(bool enabled);

    // "all" (every key labelled) vs "c" (only the Cs) — PianoRollComponent::KeyLabelMode, read by
    // TimelinePanelComponent::reloadPianoRollAppearancePrefs(). true == "all" (the default).
    bool isPianoRollKeyLabelModeAll() const;
    void setPianoRollKeyLabelModeAll(bool labelEveryKey);

    // Per-module overrides of getDefaultDualIOForNewModules() above ("Per-module I/O defaults..."
    // button). Keyed by module type (ModuleBase::getName(), e.g. "Reverb", "Filter"); a type with
    // no entry follows the global default — nullopt here means exactly that. Consumed by
    // GraphEditor::applyDefaultDualIOForNewModule, the same new-modules-only site the global
    // default itself is read from (see docs/modules/fx-modules.md#stereo-io-dual-io-toggle).
    std::optional<bool> getDualIOOverrideForType(const juce::String& moduleType) const;
    void setDualIOOverrideForType(const juce::String& moduleType, std::optional<bool> overrideValue);

    // Every module type that carries the Dual I/O parameter (granted by ModuleBase's constructor from
    // the module's channel shape), in the per-module popup's row order: the FX plus the split-block
    // voice modules
    // (docs/modules/modules.md, docs/modules/fx-modules.md#stereo-io-dual-io-toggle).
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

    // Parses the "macroAutoCreatePorts" tri-state preference ("ask" / "auto" / "leave") straight
    // from ApplicationProperties, independent of any PreferencesSettingsTab instance — the same
    // startup-restore role as loadDualIOPerModuleOverrides() above. MainComponent calls it at
    // construction so a "Remember my choice" from the macro auto-port modal the previous session
    // survives a relaunch: without this the editor's macroAutoPortPreference_ stays Unset on a
    // fresh launch (the tri-state is otherwise only pushed when Settings opens, via
    // setGraphEditor), and the modal re-asks every session. "ask" (or an unknown/absent key) maps
    // to Unset — the safe default that keeps asking, matching the tab's own combo default.
    static GraphEditor::MacroAutoPortPreference loadMacroAutoPortPreference(juce::ApplicationProperties& props);

    // Test seam for the "Per-module I/O defaults..." popup: builds the exact content component the
    // button's onClick hands to a juce::CallOutBox, without launching the CallOutBox itself (which
    // needs real screen coordinates and, like every other control in this tab, cannot be driven
    // through a headless click — see the "NOT triggerClick()" comment on the tests above). One
    // juce::Label + juce::ComboBox pair per entry of getDualIOModuleTypes(), so a test can find the
    // Nth juce::ComboBox (or match by label text) and drive it with setSelectedId(id,
    // sendNotificationSync), exactly as it would a real click.
    std::unique_ptr<juce::Component> createDualIOPerModuleDefaultsPopupForTest();

    // Test seams: the real category selector and the scroll view the rows live in.
    juce::ComboBox& getCategoryComboForTest() { return categoryCombo; }
    juce::TextEditor& getSearchFieldForTest() { return searchField; }
    juce::Component& getContentHostForTest() { return contentHost; }
    juce::Viewport& getContentViewportForTest() { return contentViewport; }

    // Test-only: the hairline dividers paint() draws between preference groups, so a test can
    // assert one falls where the Dual I/O row ends without reaching into paint() itself.
    const std::vector<juce::Rectangle<int>>& getDividerBoundsForTest() const { return dividerBounds; }

    // Collapsible sections of the "All" view (folds are ignored while a filter is active).
    bool isSectionCollapsed(Category category) const;
    void setSectionCollapsed(Category category, bool collapsed);
    void setAllSectionsCollapsed(bool collapsed);
    // True when every section is folded: the fold-all button then reads "Expand all".
    bool areAllSectionsCollapsed() const;
    // Test seams: a section's header, and the single Collapse all / Expand all button.
    juce::Button& getSectionHeaderForTest(Category category);
    synth::ui::FoldAllButton& getFoldAllButtonForTest() { return foldAllButton; }

    // Test seam: is the scrolled content taller than the visible viewport (i.e. is a
    // vertical scrollbar active)? Answers "does this tab clip its bottom groups" without reaching
    // into layoutContent. True when a window is too short to show every group, false when they fit.
    bool contentOverflowsViewportForTest() const { return contentHost.getHeight() > contentViewport.getHeight(); }

    // Live filter across every preference row's label/tooltip text.
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
    // The layout helpers' shared closures: layoutContent's own filter / visibility / divider steps.
    using GroupMatchFn = std::function<bool(std::initializer_list<juce::Component*>)>;
    using SetVisibleFn = std::function<void(std::initializer_list<juce::Component*>, bool)>;
    using BeginGroupFn = std::function<void(bool)>;

    // One per category: lays out that category's groups (and sets layoutCategory first).
    void layoutGraphGroups(int& y, int contentWidth, bool& pendingDivider, const GroupMatchFn& groupMatches,
                           const SetVisibleFn& setGroupVisible, const BeginGroupFn& beginGroup);
    void layoutTimelineGroups(int& y, int contentWidth, bool& pendingDivider, const GroupMatchFn& groupMatches,
                              const SetVisibleFn& setGroupVisible, const BeginGroupFn& beginGroup);
    void layoutAutosaveGroup(int& y, int contentWidth, bool& pendingDivider, const GroupMatchFn& groupMatches,
                             const SetVisibleFn& setGroupVisible, const BeginGroupFn& beginGroup);
    void layoutMixerGroups(int& y, int contentWidth, bool& pendingDivider, const GroupMatchFn& groupMatches,
                           const SetVisibleFn& setGroupVisible, const BeginGroupFn& beginGroup);
    void setupCategorySelector();

    // "All" view. Every layout*Groups unit calls enterCategory() first: it records where the
    // category's rows begin so placeSectionHeaders() can slot a header above them afterwards.
    void enterCategory(Category category, int y);
    bool categoryShown(Category category) const;
    bool sectionHeadersActive() const { return selectedCategory == Category::All && searchQuery.isEmpty(); }
    void placeSectionHeaders(int contentWidth);
    void refreshSectionTitles();
    void setupSectionControls(); // chained from setupCategorySelector()

    void persistSmartConnectionMode(GraphEditor::SmartConnectionMode mode);
    void persistDoubleClickPortDisconnect(bool enabled);
    void persistReconnectChainOnDelete(bool enabled);
    void persistMacroAutoCreatePortsOnDrag(bool enabled);
    void persistMacroAutoDeletePortsOnLastCable(bool enabled);
    void persistMacroSpliceCableOnPortDelete(bool enabled);
    void persistMacroDragWithoutCmd(bool enabled);
    void persistMoveMacroOnHullDrag(bool enabled);
    void persistMixerAutoCreateChannelOnConnect(bool enabled);
    void persistAlignmentGuidesEnabled(bool enabled);
    void persistDefaultDualIOForNewModules(bool enabled);
    void persistMacroAutoPortPreference(GraphEditor::MacroAutoPortPreference pref);
    void persistLoopSelectionArms(bool enabled);
    void persistDoubleClickSpansLocators(bool enabled);
    void persistAskBeforeRemovingLfo(bool enabled);
    void initTimelineEditingToggles();
    void persistNaturalScrolling(bool enabled);
    void persistZoomScrollUpZoomsIn(bool enabled);
    void persistAutosaveEnabled(bool enabled);
    void persistAutosaveIntervalMinutes(int minutes);
    void persistAutosaveBackupCount(int count);
    void persistMixerDefaultTrackPresetAudio(const juce::String& presetName);
    void persistMixerDefaultTrackPresetInstrument(const juce::String& presetName);
    void persistMixerPlacement(const juce::String& placement);
    void persistPanelDetachMode(const juce::String& mode);
    void persistPianoRollKeyLabelMode(bool labelEveryKey);
    void persistDualIOPerModuleOverrides();
    void persistMidiRemoteDefaultTakeover(synth::Takeover takeover);
    void persistMidiRemoteShowBadges(bool enabled);
    void persistPatchSaveMode(synth::PatchSaveMode mode);
    // The patch-save-location row: constructs its controls (chained from setupMidiRemoteControls(),
    // the constructor is baselined), refreshes the hint for the current mode, and lays it out.
    void setupPatchSaveLocationControls();
    void updatePatchSaveHint();
    void launchPatchSaveFolderChooser();
    void layoutPatchSaveLocationGroup(int& y, int contentWidth, bool& pendingDivider, const GroupMatchFn& groupMatches,
                                      const SetVisibleFn& setGroupVisible, const BeginGroupFn& beginGroup);

    // Constructs/wires the two Mixer -> per-type default track preset combos.
    // Pulled out of the constructor (which was tripping the function-size ratchet) into its own
    // named step, in PreferencesSettingsTabMixerDefaults.cpp alongside this group's other members.
    void setupMixerDefaultTrackPresetControls();
    // Constructs/wires the Mixer placement combo, same "own named step" reason as
    // setupMixerDefaultTrackPresetControls() above.
    void setupMixerPlacementControls();
    // The panel-detach-mode combo, chained from the tail of setupMixerPlacementControls()
    // for the same baselined-constructor reason.
    void setupPanelDetachModeControls();
    // The MIDI Remote group, chained from the tail of setupPanelDetachModeControls() for the
    // same baselined-constructor reason.
    void setupMidiRemoteControls();

    // Shared by the real button and createDualIOPerModuleDefaultsPopupForTest() so the test seam
    // exercises the exact component a click would open, not a lookalike.
    std::unique_ptr<juce::Component> buildDualIOPerModuleDefaultsPopup();

    // Shared muted-hint treatment for the small explanatory line under a preference row (naturalScrollingHint and
    // zoomScrollUpZoomsInHint): a fixed 18px height gives room for barely one line, so a hint whose
    // text is wider than the row would squeeze horizontally (Label's default
    // minimum-horizontal-scale) instead of wrapping. Callers set their own text and bounds; this
    // only sets the font/colour/wrap behaviour, at one spot, for both.
    void styleMutedHintLabel(juce::Label& hint);

    // Re-lays the tab for the current searchQuery: hides every row whose label/tooltip text does
    // not contain it (case-insensitive), collapsing the vertical gap and any now-orphaned divider.
    // Called from resized() and from every place searchQuery changes.
    void applySearchFilter(const juce::String& query);

    // Lays the preference groups into the viewport's content host, top-down in content coordinates,
    // accumulating a running height the host is sized to. This is what makes a vertical scrollbar
    // appear when the groups outgrow the window: exactly the structure ShortcutsSettingsTab uses for
    // its rows. Sliced out of resized() so a search filter can re-run just this content pass
    // (applySearchFilter -> resized -> layoutContent) without re-laying the pinned chrome.
    void layoutContent(int contentWidth);

    // Lays out the "Group 9" mixer-defaults row pair. Pulled out of layoutContent
    // (which was tripping the function-size ratchet) into its own named step; `groupMatches`/
    // `setGroupVisible`/`beginGroup` are layoutContent's own search-filter helpers, forwarded
    // through rather than duplicated.
    // The macro toggle group's rows (auto-port + drag without Cmd), and their construction; each a named step of
    // layoutContent / the constructor for the function-size ratchet.
    void initMacroToggles();
    bool
    layoutMacroToggleGroup(int& y, int contentWidth,
                           const std::function<bool(std::initializer_list<juce::Component*>)>& groupMatches,
                           const std::function<void(std::initializer_list<juce::Component*>, bool)>& setGroupVisible,
                           const std::function<void(bool)>& beginGroup);

    void layoutMixerDefaultTrackPresetGroup(
        int& y, int contentWidth, const std::function<bool(std::initializer_list<juce::Component*>)>& groupMatches,
        const std::function<void(std::initializer_list<juce::Component*>, bool)>& setGroupVisible,
        const std::function<void(bool)>& beginGroup);

    // Lays out the Mixer placement combo row -- chained from the TAIL of
    // layoutMixerDefaultTrackPresetGroup() (not called from layoutContent directly, and not
    // taking its `beginGroup` closure): layoutContent's own `pendingDivider` local (which
    // `beginGroup` updates) is a baselined function (function-size ratchet) that must not grow, so
    // `previousGroupWasVisible` carries the one bit that closure would otherwise have tracked,
    // and this draws its own divider directly into `dividerBounds` (a plain member) when needed.
    void layoutMixerPlacementGroup(
        int& y, int contentWidth, bool previousGroupWasVisible,
        const std::function<bool(std::initializer_list<juce::Component*>)>& groupMatches,
        const std::function<void(std::initializer_list<juce::Component*>, bool)>& setGroupVisible);

    // Lays out the panel-detach-mode row, chained from layoutMixerPlacementGroup() the same
    // way that one is chained from layoutMixerDefaultTrackPresetGroup().
    void layoutPanelDetachModeGroup(
        int& y, int contentWidth, bool previousGroupWasVisible,
        const std::function<bool(std::initializer_list<juce::Component*>)>& groupMatches,
        const std::function<void(std::initializer_list<juce::Component*>, bool)>& setGroupVisible);

    // Lays out the MIDI Remote group, chained from layoutPanelDetachModeGroup() the same way
    // that one is chained from layoutMixerPlacementGroup().
    void
    layoutMidiRemoteGroup(int& y, int contentWidth, bool previousGroupWasVisible,
                          const std::function<bool(std::initializer_list<juce::Component*>)>& groupMatches,
                          const std::function<void(std::initializer_list<juce::Component*>, bool)>& setGroupVisible);

    // Paints the group-separator hairlines. Called by ContentHost::paint (the viewport's viewed
    // component), so the rules scroll along with the groups they separate — same owner-delegation
    // idiom ShortcutsSettingsTab::RowsHost uses for its section chrome. Paints in the host's own
    // content coordinates, which is exactly the space layoutContent lays the dividers into.
    void paintContent(juce::Graphics&);

    juce::ApplicationProperties& appProperties;
    GraphEditor* graphEditor{nullptr}; // weak, owned by MainComponent

    // Live filter field, top of the tab (SettingsWindow has no cross-tab search rig — see the class
    // comment above — so this is scoped to the Preferences tab, the same "per-tab, not per-window"
    // choice ModuleLibraryComponent's own search box makes for the module library). Esc clears it,
    // matching ModuleLibraryComponent::searchEditor's onEscapeKey.
    juce::TextEditor searchField;
    // Category picker, left of the search field; one entry per Category, id = enum value + 1.
    juce::ComboBox categoryCombo;
    // Opens on the remembered category (setupCategorySelector); All when none is saved.
    Category selectedCategory{Category::All};
    // The category whose groups the layout pass is currently walking (set by each layout*Groups).
    Category layoutCategory{Category::All};
    // "All" view state: per-section fold, where each section's rows began in the last layout pass
    // (-1 = not laid out), and the header buttons. Fold state is per tab instance, not persisted.
    bool sectionCollapsed[kNumSections] = {};
    int sectionStartY[kNumSections] = {};
    // A fold header of the "All" view (defined in ...Sections.cpp). A juce::Button so it is
    // keyboard-reachable and named for screen readers.
    struct SectionHeader
        : juce::Button
        , synth::ui::FoldableHeader {
        SectionHeader(PreferencesSettingsTab& o, Category c);
        void refreshTitle();
        bool isFolded() const override;
        void setFolded(bool folded) override;
        void paintButton(juce::Graphics& g, bool hot, bool down) override;
        PreferencesSettingsTab& owner;
        Category category;
    };
    std::unique_ptr<SectionHeader> sectionHeaders[kNumSections];
    // The one Collapse all / Expand all strip button, pinned top-right of the rows in the All view.
    synth::ui::FoldAllButton foldAllButton;
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
    // "Always ask" / "Auto-create ports" / "Leave cables as is" — the tri-state
    // GraphEditor::MacroAutoPortPreference the "Create Macro" gesture reads before deciding
    // whether to show its own modal (see docs/macros/auto-ports.md).
    juce::Label macroAutoPortLabel_;
    juce::ComboBox macroAutoPortCombo_;
    // Plain on/off toggles, ON by default — see their getter/setter declarations above for why these are a
    // different shape from macroAutoPortCombo_ (see docs/macros/auto-ports.md#ports-on-a-cable-drag).
    juce::ToggleButton macroAutoCreatePortsOnDragToggle{"Auto-create macro ports when a cable or mixer send "
                                                        "crosses a boundary"};
    juce::ToggleButton macroAutoDeletePortsOnLastCableToggle{"Auto-delete macro ports when their last cable is "
                                                             "removed"};
    // OFF by default, unlike the macro auto-port pair above — see its getter/setter declarations for why.
    juce::ToggleButton macroSpliceCableOnPortDeleteToggle{"When deleting a macro port by hand, splice the cable "
                                                          "back together instead of dropping it"};
    juce::ToggleButton macroDragWithoutCmdToggle{"Drag modules into and out of macros without Cmd"};
    juce::ToggleButton moveMacroOnHullDragToggle{"Drag inside a macro's outline to move the macro instead of panning"};
    // Not macro-specific -- see initMacroToggles()' own comment for why it lives here.
    juce::ToggleButton reconnectChainOnDeleteToggle{"Reconnect the chain when deleting a module"};
    // Plain on/off, ON by default — see the getter/setter declarations above for why this is a different
    // shape from a tri-state "ask"
    // (see docs/mixer/mixer.md#channels-follow-audio-not-tracks "main workflow").
    juce::ToggleButton mixerAutoCreateChannelOnConnectToggle{
        "Auto-create a mixer channel when a MIDI track is connected"};
    juce::ToggleButton loopSelectionArmsToggle{"Timeline: P (loop selection) also switches looping on"};
    // The other half of the same locator conversation, so it sits in the same group as the row
    // above rather than getting a divider of its own: one is "make the locators from a selection",
    // this one is "make a clip from the locators".
    juce::ToggleButton doubleClickSpansLocatorsToggle{"Timeline: double-click inside the locators spans them"};
    juce::ToggleButton askBeforeRemovingLfoToggle{"Ask before removing an LFO's last destination"};
    juce::ToggleButton naturalScrollingToggle{"Natural scrolling"};
    // The one preference whose label needs a second line to explain WHICH surfaces it touches — a
    // bare "Natural scrolling" toggle in an app that also has a pannable canvas would read as
    // applying to everything.
    juce::Label naturalScrollingHint;
    // Sits directly under the natural-scrolling pair because it is the same gesture with a modifier
    // held, and users reach for both in the same visit. Independent of it, though: this one governs
    // the Cmd / Cmd+Shift wheel-ZOOM branches only, which is what its own caption spells out.
    //
    // A checkbox with a single-line hint carrying the explanation. The persisted key and boolean
    // contract are isZoomScrollUpZoomsInEnabled() / setZoomScrollUpZoomsInEnabled(); readers
    // outside this file (FocusArbitrationTests.cpp's
    // ZoomScrollPreferenceReachesTheTimelineAndTheRoll, MainComponent) go through those.
    juce::ToggleButton zoomScrollUpZoomsInToggle{"Scroll up to zoom in"};
    juce::Label zoomScrollUpZoomsInHint;
    // On (the default) labels every row in the piano roll's keys column; off labels only the Cs —
    // PianoRollComponent::KeyLabelMode::AllNotes / OctavesOnly.
    juce::ToggleButton pianoRollKeyLabelsToggle{"Label every key"};
    // Autosave. One single line: the toggle, then "Every: [field] min", then
    // "Keep: [field] backups" — three independent statements that read as one group, not stacked
    // rows. Both numeric fields are plain digits-only juce::TextEditors (exact-integer entry, no
    // +/- buttons and no fixed-choice list) - see the constructor for the commit-on-return/
    // commit-on-focus-lost handling.
    juce::ToggleButton autosaveEnabledToggle{"Autosave"};
    juce::Label autosaveIntervalLabel;
    juce::TextEditor autosaveIntervalEditor;
    juce::Label autosaveIntervalUnitLabel;
    juce::Label autosaveBackupCountLabel;
    juce::TextEditor autosaveBackupCountEditor;
    juce::Label autosaveBackupCountUnitLabel;
    // Mixer -> per-type default track preset, one combo each, "Factory Default" as
    // the leading sentinel row (see PreferencesSettingsTabInternal.h's combo-id constants).
    juce::Label mixerDefaultTrackPresetAudioLabel;
    juce::ComboBox mixerDefaultTrackPresetAudioCombo;
    juce::Label mixerDefaultTrackPresetInstrumentLabel;
    juce::ComboBox mixerDefaultTrackPresetInstrumentCombo;
    // Mixer placement -- Tab beside the Timeline (default, combo id 1) / Own
    // panel (2) / Window (3) (see docs/mixer/panel.md).
    juce::Label mixerPlacementLabel;
    juce::ComboBox mixerPlacementCombo;
    // "When a panel opens in its own window" -- Move it there (default, combo id 1) / Show
    // it in both places (2) (see docs/mixer/panel.md#placement-and-detachable-windows).
    juce::Label panelDetachModeLabel;
    juce::ComboBox panelDetachModeCombo;
    // Default takeover (Jump / Pick-up / Scale) and the badge switch.
    juce::Label midiRemoteTakeoverLabel;
    juce::ComboBox midiRemoteTakeoverCombo;
    juce::ToggleButton midiRemoteShowBadgesToggle{"Show MIDI badges on mapped controls"};
    // Patch save location: mode combo (ids: PatchSaveMode + 1), the custom-folder button and a hint
    // that names the folder the current mode resolves to.
    juce::Label patchSaveLabel;
    juce::ComboBox patchSaveCombo;
    juce::TextButton patchSaveChooseButton{"Choose..."};
    juce::Label patchSaveHint;
    std::unique_ptr<juce::FileChooser> patchSaveChooser;

    // Hairline rules between preference groups, painted in paint() from these bounds.
    std::vector<juce::Rectangle<int>> dividerBounds;

    // Per-module Dual I/O overrides, keyed by module type. Loaded once in the constructor via
    // loadDualIOPerModuleOverrides() and mutated only through setDualIOOverrideForType(), which
    // re-persists the whole map — small enough (one bool per module type) that there is no reason
    // to diff and write just the changed key.
    std::map<juce::String, bool> dualIOPerModuleOverrides;

    // The scrolled content: every preference group below is a child of this bare host, held by
    // contentViewport, so a vertical scrollbar shows when the group stack outgrows the window
    // instead of the lower groups getting clipped. Its paint() delegates the hairline
    // dividers back to the owner (see paintContent) — the same structure ShortcutsSettingsTab's
    // RowsHost uses for its section chrome.
    struct ContentHost : juce::Component {
        explicit ContentHost(PreferencesSettingsTab& o)
            : owner(o) {
            setWantsKeyboardFocus(false);
        }
        void paint(juce::Graphics& g) override { owner.paintContent(g); }
        PreferencesSettingsTab& owner;
    };

    juce::Viewport contentViewport;
    ContentHost contentHost{*this};
    synth::ui::ScrollIntoViewOnFocus followFocus_{contentViewport};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PreferencesSettingsTab)
};
