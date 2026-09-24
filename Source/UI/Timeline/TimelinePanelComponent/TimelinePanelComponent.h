#pragma once

#include "Mixer/TrackPresetManager.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/PianoRoll/PianoRollComponent/PianoRollComponent.h"
#include "UI/Timeline/AutomationLaneEditor.h"
#include "UI/Timeline/ClipSelectionModel.h"
#include "UI/Timeline/EditTool.h"
#include "UI/Timeline/TimelineClipLaneArea/TimelineClipLaneArea.h"
#include "UI/Timeline/TimelinePlayheadOverlay.h"
#include "UI/Timeline/TimelineRulerComponent.h"
#include "UI/Timeline/TimelineTrackHeaderComponent.h"
#include "UI/Timeline/TimelineTransportBar.h"
#include "UI/Timeline/TimelineViewState.h"
#include <array>
#include <functional>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

class AppUndoManager;  // Forward declaration (Source/AppUndoManager.h)
class ShortcutManager; // Forward declaration (Source/ShortcutManager/ShortcutManager.h)

namespace synth {
class TransportService;
class Metronome; // Forward declaration (Source/Transport/Metronome.h)
} // namespace synth

// Bottom-docked timeline panel: ruler, grid, zoom/scroll, snap selector, click-to-seek/
// drag-to-loop.
//
// MainComponent docks this full-width, above the status bar, toggled via the toolbar button /
// Cmd+T shortcut and slid in/out through the same coordinated AnimationDriver that already
// animates the library/AI panels (see MainComponent::beginPanelSlide()). This class owns
// none of that: it is layout + paint, with no timer and no animation of its own — updateFromTransport()
// is driven by MainComponent's EXISTING 10 Hz timer, and the only timer anywhere under this panel
// is the playhead overlay's, which runs only while the transport plays (see
// TimelinePlayheadOverlay.h and docs/layout/animation.md).
//
// resized() lays out three regions:
//   - transport bar strip   (top,    Metrics::timelineTransportBarHeight) — houses the snap
//     selector and synth::ui::TimelineTransportBar (play/stop/record/loop + BPM/time-sig editors
//     + the bar:beat readout), left-aligned in the rest.
//   - track-header column   (left,   Metrics::timelineTrackHeaderWidth)
//   - lanes/ruler area      (remainder) — TimelineRulerComponent (Metrics::timelineRulerHeight)
//     docked at its top, a bar/beat grid painted directly by this component below it.
//
// The panel has no resize affordance of its own: the bottom dock's one top-edge handle
// (MixerDockComponent, FRO231) resizes the whole dock from every tab. The panel never sets its
// own bounds.
//
// The single synth::ui::TimelineViewState (beat<->pixel mapping — zoom, scroll, snap) is owned
// here and shared by reference with the ruler; getViewState() exposes it so every consumer maps
// beats to pixels identically.
//
// Headless-safe: paint()/resized() dynamic_cast<AppLookAndFeel*> and fall back to literal
// values/colours when the themed LnF is absent (test runner has no themed LnF installed).
namespace synth::ui {

class TimelinePanelComponent
    : public juce::Component
    , private synth::TimelineDoc::Listener
    , private juce::ChangeListener
    // FRO14: ONE shared timer for the header column's channel-chip meters -- see timerCallback()
    // below.
    , private juce::Timer {
public:
    TimelinePanelComponent();
    ~TimelinePanelComponent() override;

    void paint(juce::Graphics& g) override;
    // T159: focus-region outline, drawn OVER children -- see paintOverChildren()'s definition in
    // TimelinePanelLayout.cpp for why.
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;

    // Wheel = horizontal scroll; Cmd+wheel (Ctrl on platforms without a Cmd key) = zoom around the
    // cursor. See mouseWheelMove()'s definition in TimelinePanelLayout.cpp for the full modifier
    // table and the ScrollPolicy rationale.
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    // Non-owning; may be null (tests, or before MainComponent finishes wiring).
    void setTransport(synth::TransportService* transport);

    // Non-owning; may be null.
    void setMetronome(synth::Metronome* metronome);

    // Called from MainComponent's existing 10 Hz timer (only while this panel is visible); adds no
    // timer of its own. See its definition in TimelinePanelComponent.cpp for what it does with the
    // snapshot.
    void updateFromTransport(const synth::TransportService::PositionSnapshot& snapshot, double outputLatencySeconds);

    // Non-owning. Restores/persists the snap-selector choice, forwards to the transport bar and the
    // piano roll for their own prefs, and runs reloadPianoRollAppearancePrefs() once -- see its
    // definition in TimelinePanelLayout.cpp.
    void setApplicationProperties(juce::ApplicationProperties* props);

    // Reads the roll's appearance prefs (key-label density, note-colour overrides) and pushes them
    // into the roll. A no-op with no ApplicationProperties installed. Public so a live settings
    // change can re-push without a restart -- see its definition in TimelinePanelLayout.cpp.
    void reloadPianoRollAppearancePrefs();

    // Non-owning; may be null (before this is called, the panel is an inert shell with an empty
    // header column). See its definition in TimelinePanelComponent.cpp for the listen/rebuild
    // contract.
    void setTimelineDoc(synth::TimelineDoc* doc);
    synth::TimelineDoc* getTimelineDoc() const noexcept { return doc_; }

    // Non-owning. Must be set before (or at the same time as) setTimelineDoc for the first build to
    // be fully wired -- see its definition in TimelinePanelTrackHeaders.cpp.
    void setTrackHeaderHost(TrackHeaderHost* host);

    // Non-owning; may be null (mutations then apply directly, off the undo stack). Forwarded to the
    // clip-lane area, making every clip drag/trim/split/duplicate/delete ONE undo step -- see its
    // definition in TimelinePanelComponent.cpp for why.
    void setUndoManager(AppUndoManager* undoManager);

    // The clip lane area and the selection model behind it. The panel owns the selection
    // model; the lane area only holds a reference to it (see TimelineClipLaneArea's ctor).
    synth::ui::ClipSelectionModel& getClipSelection() noexcept { return clipSelection_; }
    synth::ui::TimelineClipLaneArea& getClipLaneArea() noexcept { return clipLaneArea_; }
    // const overload: MainComponent::resolveEditSurface() is itself const and only needs
    // to compare addresses / walk the component tree, never to mutate either sub-component.
    const synth::ui::TimelineClipLaneArea& getClipLaneArea() const noexcept { return clipLaneArea_; }

    // ---- Edit tools (the Cubase-style tool row — see EditTool.h) ----
    // ONE active tool for the whole timeline, shared by the clip lanes and the piano roll -- see
    // setActiveTool()'s definition in TimelinePanelStrips.cpp for why.
    void setActiveTool(EditTool tool);
    EditTool getActiveTool() const noexcept { return activeTool_; }
    /** The strip button for a tool. Never null once the panel is constructed. */
    juce::DrawableButton* getToolButton(EditTool tool) const noexcept;

    // ---- Clip clipboard (Cmd+C/V/D on the TimelineClips surface) ----
    // See TimelinePanelClipClipboard.cpp for why this panel owns the clipboard.

    // Copies the CURRENTLY SELECTED clips (notes, name, length, muted flag, audio fields), with
    // starts RELATIVE to the earliest selected clip's start, into an internal clipboard, replacing
    // whatever was there. Returns false (clipboard untouched) when nothing is selected or there's
    // no doc.
    bool copySelectedClips();
    // True once copySelectedClips() has captured at least one clip and nothing has cleared it
    // since — getCommandInfo's Paste-active gate for the TimelineClips surface.
    bool canPasteClips() const noexcept { return !clipClipboard_.empty(); }
    // Inserts every clipboard clip back onto its original track (or the doc's first track of a
    // matching kind — see the definition), re-based so the EARLIEST clip lands at the transport's
    // CURRENT position and every other clip keeps its relative offset. One recordTimelineChange for
    // the whole paste; the pasted clips end up selected. Returns false (no-op) when the clipboard is
    // empty, there's no doc, or every clip was skipped.
    bool pasteClipsAtPlayhead();
    // doc_->duplicateClip() per selected clip, batched into one recordTimelineChange however many
    // clips are selected; the new clips end up selected. Returns false when nothing is selected or
    // there's no doc.
    bool duplicateSelectedClips();

    // copySelectedClips() followed by deleting the selection, as ONE recordTimelineChange — so
    // undo brings a cut back in a single step, and the clipboard survives it. Returns false
    // (nothing copied, nothing deleted) when the copy half fails.
    bool cutSelectedClips();
    // Whether Cut/Copy have anything to act on — the getCommandInfo gate for both.
    bool canCutClips() const noexcept;
    // Whether ANY clip is selected. Same answer as canCutClips today; a separate name because the
    // commands that ask (Duplicate, Repeat) are asking about the selection, not about the
    // clipboard, and the two should be free to diverge.
    bool hasClipSelection() const noexcept;
    // Selects every clip on every track (Cmd+A on the clip-lane surface). Returns false when
    // there's no doc or the arrangement has no clips at all.
    bool selectAllClips();
    // Cubase's "Repeat": `count` back-to-back copies of the selection BLOCK, as ONE
    // recordTimelineChange for the whole repeat -- see its definition for the tiling rule. Returns
    // false when `count` is < 1, there's no doc/selection, or nothing could be created.
    bool repeatSelectedClips(int count);

    // ---- Piano roll ----
    // Swaps the lanes region (gridLanesBounds_ — the same rect the clip-lane area occupies) to
    // synth::ui::PianoRollComponent, editing `id`. A no-op if `id` does not resolve to a live
    // clip (PianoRollComponent::openClip's own contract). Double-clicking a clip in the lane area
    // calls this via the onClipDoubleClicked hook wired in the constructor.
    void openPianoRoll(synth::ClipId id);
    // Swaps back to the clip lanes. Wired to PianoRollComponent::onCloseRequested (back button,
    // Escape with nothing selected, or the edited clip disappearing from the doc) — also callable
    // directly.
    void closePianoRoll();
    bool isPianoRollOpen() const noexcept { return pianoRoll_.isOpen(); }
    synth::ui::PianoRollComponent& getPianoRoll() noexcept { return pianoRoll_; }
    // const overload — see getClipLaneArea()'s twin above for why (MainComponent::
    // resolveEditSurface()).
    const synth::ui::PianoRollComponent& getPianoRoll() const noexcept { return pianoRoll_; }

    // ---- Automation strip ----
    // Docked at the BOTTOM of the lanes region (gridLanesBounds_), toggled by lane selection: the
    // clip-lane area (and the piano roll, sharing the same rect) shrink by exactly
    // Metrics::timelineAutomationStripHeight while the strip is open. Right-click-any-knob
    // (ModuleComponent -> GraphEditor::onAutomateParameterRequested -> MainComponent) is the other
    // entry point into this — see MainComponent::automateParameter().

    /** Opens the strip editing `id`. A no-op if `id` doesn't resolve to a live lane. */
    void showAutomationLane(synth::LaneId id);
    /** Closes the strip (clip-lane area/piano roll return to full height). The strip's own close
     *  button and this panel's Escape-when-idle (keyPressed below) both route here. */
    void closeAutomationStrip();
    bool isAutomationStripVisible() const noexcept { return automationStripVisible_; }
    synth::LaneId getSelectedAutomationLane() const noexcept { return selectedAutomationLane_; }
    synth::ui::AutomationLaneEditor& getAutomationLaneEditor() noexcept { return automationEditor_; }
    juce::ComboBox& getAutomationLaneCombo() noexcept { return laneCombo_; }
    juce::ComboBox& getAutomationRecordModeCombo() noexcept { return recordModeCombo_; }
    juce::Button& getAutomationCloseButton() noexcept { return automationCloseButton_; }
    juce::Rectangle<int> getAutomationStripBounds() const noexcept { return automationStripBounds_; }

    /** One entry in the lane picker: either an EXISTING doc lane labelled "NodeName \xC2\xB7 paramId"
     *  (resolved via TrackHeaderHost::getNodeDisplayName; falls back to the uuid's first 8
     *  characters when the node doesn't resolve), or an "Add lane..." entry for a hosted plugin
     *  instance parameter that has none yet -- `isAddEntry` distinguishes the two, `id` is only
     *  meaningful when it's false. In track order then lane order, existing lanes first, then
     *  add-lane entries -- index i is menu id i + 1; see collectAutomationLaneOptions()'s
     *  definition in TimelinePanelStrips.cpp for why. */
    struct AutomationLaneOption {
        synth::LaneId id;
        juce::String label;
        bool isAddEntry = false;
        synth::ui::TrackHeaderHost::PluginLaneOption addOption; // populated only when isAddEntry
    };
    std::vector<AutomationLaneOption> collectAutomationLaneOptions() const;

    // ---- Headless hooks (juce::PopupMenu::showMenuAsync's "doesn't run headlessly" idiom applies
    // here too — tests drive the choice directly rather than through a live juce::ComboBox) ----
    void applyAutomationLaneMenuChoice(int selectedId);
    void applyAutomationRecordModeChoice(int selectedId);

    // Escape closes the strip when it's open and idle -- see its definition in
    // TimelinePanelShortcuts.cpp for how it interacts with AutomationLaneEditor's own Escape.
    bool keyPressed(const juce::KeyPress& key) override;

    // Trackpad pinch: plain = horizontal zoom, Shift = vertical (row height) zoom.
    void mouseMagnify(const juce::MouseEvent& e, float scaleFactor) override;

    // Pure geometry getters — later tasks and tests build on the same rects rather than
    // re-deriving the arithmetic in resized().
    juce::Rectangle<int> getTransportBarBounds() const noexcept { return transportBarBounds_; }
    // The WHOLE left column, including the "+ MIDI Track" strip at its top — the three regions
    // still tile the panel exactly (see TimelinePanelComponentTest.PanelRegionsTile).
    juce::Rectangle<int> getTrackHeaderBounds() const noexcept { return trackHeaderBounds_; }
    juce::Rectangle<int> getLanesBounds() const noexcept { return lanesBounds_; }

    // ---- Snap / zoom / scroll: the view-state verbs the shortcut layer drives ----
    // Everything in this block is VIEW state: no TimelineDoc mutation, nothing on the undo stack.
    // See TimelinePanelLayout.cpp for the design rationale behind each member below.

    /** Sets the grid division; the same thing as picking it from the snap combo.
     *  @return true when the division actually changed (a re-pick of the current one still
     *          re-arms and re-persists, it just reports no change). */
    bool setSnapValue(TimelineViewState::Snap value);

    /** Steps the grid one division through the MUSICAL values only — Bar, 1, 1/2, 1/4, 1/8, 1/16,
     *  1/32, 1/64, 1/128 — with `direction` > 0 going FINER (toward 1/128) and < 0 going COARSER
     *  (toward Bar). Zero is a no-op; CLAMPED at both ends, never wrapping.
     *  @return true when the division changed. */
    bool cycleSnapValue(int direction);

    /** Horizontal zoom by `factor` (> 1 in, < 1 out) around the CENTRE of the visible lanes. A
     *  non-finite or non-positive factor is ignored. */
    void zoomTimelineHorizontal(double factor);

    /** Vertical (track row height) zoom by `factor`, anchored on the middle row of the visible
     *  lanes. */
    void zoomTimelineVertical(double factor);

    /** App-level scroll-direction preference, stacked on top of whatever the OS already did to the
     *  wheel deltas. Default false = "natural" = the juce::Viewport convention every other
     *  scrolling surface in the app already follows. Not persisted here (see the owner's own
     *  persist path). Forwarded to the piano roll so a clip-lane scroll and a roll scroll never
     *  disagree about which way is "natural". */
    void setScrollInverted(bool inverted) noexcept;
    bool isScrollInverted() const noexcept { return scrollInverted_; }

    /** App-level ZOOM-direction preference for the Cmd/Cmd+Shift wheel-zoom gestures (horizontal
     *  and vertical) — independent of setScrollInverted above, which governs the PLAIN-scroll
     *  branches only. Default false = "up zooms in". Forwarded to the piano roll for the same
     *  reason setScrollInverted is. */
    void setZoomScrollInverted(bool inverted) noexcept;
    bool isZoomScrollInverted() const noexcept { return zoomScrollInverted_; }

    /** The user's bindings for this panel's OWN keys: the six tool digits, the snap toggle, the loop
     *  toggle and loop-the-selection. Non-owning and may stay null -- with no manager installed
     *  keyPressed() falls back to the hardcoded Cubase defaults. Escape and anything the app
     *  dispatches as a command (Cmd+C/V/X/D, Space, the grid commands) are not resolved through
     *  here -- see its definition in TimelinePanelComponent.cpp for the rest of the contract. */
    void setShortcutManager(ShortcutManager* manager);
    const ShortcutManager* getShortcutManager() const noexcept { return shortcuts_; }

    TimelineViewState& getViewState() noexcept { return viewState_; }
    TimelineRulerComponent& getRuler() noexcept { return ruler_; }
    TimelinePlayheadOverlay& getPlayhead() noexcept { return playhead_; }
    juce::ComboBox& getSnapCombo() noexcept { return snapCombo_; }
    juce::TextButton& getSnapToggleButton() noexcept { return snapToggleButton_; }
    // play/stop/record/loop + BPM/time-sig editors + the bar:beat readout.
    TimelineTransportBar& getTransportBar() noexcept { return transportBar_; }

    // ---- Follow playhead ----
    // "Keep the playhead on screen while it plays", persisted under "timelineFollowPlayhead",
    // default OFF -- see its definition in TimelinePanelLayout.cpp.
    void setFollowPlayheadEnabled(bool enabled);
    bool isFollowPlayheadEnabled() const noexcept { return followPlayhead_; }
    /** Test seam: no OS mouse source exists headlessly, so a test drives the click via
     *  `getFollowPlayheadButtonForTest().onClick()` rather than synthesising a real click. */
    juce::DrawableButton& getFollowPlayheadButtonForTest() noexcept { return followPlayheadButton_; }

    /** How many times updateFromTransport() has been called. Test hook: it is what proves the
     *  10 Hz poll never reaches a hidden panel. */
    int getTransportUpdateCountForTest() const noexcept { return transportUpdateCount_; }

    // ---- Track headers ----
    // Menu ids for the "+ Track" button's menu. Numbered from 1 because juce::PopupMenu reserves 0
    // for "dismissed".
    static constexpr int kAddMidiTrackMenuId = 1;
    static constexpr int kAddAudioTrackMenuId = 2;
    // Below a separator, because a marker is NOT a track: it adds no header row and no graph node.
    // It shares this menu because "+ Track" is where a user reaches for "add something to the
    // arrangement", and a second button for one item would not earn its pixels.
    static constexpr int kAddMarkerMenuId = 3;
    // T183 (P9-3b): the Instrument submenu's three audio-producing MIDI instrument choices — see
    // TrackHeaderHost::addInstrumentTrack's own comment for why the set is exactly these three (not
    // every isMidiInstrumentType() member: Poly MIDI/Sequencer/Poly Sequencer don't produce audio).
    static constexpr int kAddInstrumentOscillatorMenuId = 4;
    static constexpr int kAddInstrumentWavetableMenuId = 5;
    static constexpr int kAddInstrumentSamplerMenuId = 6;
    // FRO48 (P9-3k): poly variants of the Oscillator/Wavetable entries above — Sampler has no "poly"
    // parameter, so it has no poly entry.
    static constexpr int kAddInstrumentOscillatorPolyMenuId = 7;
    static constexpr int kAddInstrumentWavetablePolyMenuId = 8;
    // FRO26 (P9-3e, docs/mixer/mixer.md#creating-channels-in-an-existing-project): "Create channels" for existing
    // projects. Lives on this same menu rather than a per-track context menu or a mixer panel — there is no mixer panel
    // yet (P9-5), and "+ Track" is already where every other channel-creating action in this doc lives (Audio Track,
    // Instrument Track); a project-wide sweep belongs beside them, not off a single track header, since it acts on
    // every track at once.
    static constexpr int kCreateChannelsMenuId = 9;
    // FRO42 (P9-3h): the Instrument submenu's "Plugin" sub-submenu. The single disabled row shown
    // in place of an empty/scanning submenu (never actually selectable — JUCE never delivers a
    // disabled item's id — but named for clarity and so applyAddTrackMenuChoice has an explicit
    // no-op to ignore rather than falling through by luck). Every real plugin entry's id is
    // `kAddInstrumentPluginMenuIdBase + index`, `index` into the SNAPSHOT `buildAddTrackMenu()`
    // captures into `instrumentPluginMenuSnapshot_` at build time. Deliberately NOT the same
    // contract as collectAutomationLaneOptions/applyAutomationLaneMenuChoice: an automation lane
    // is document data mutated only on the message thread, so re-running the collector at click
    // time is safe and cheap. The known-plugin list backing this menu is mutated by
    // `PluginScanService::runScan` on a BACKGROUND thread and re-sorted by name in
    // `MainComponent::getInstrumentPluginOptions()` — a scan that completes between the menu
    // opening and the click landing can silently change what index N means, resolving the click
    // against a plugin the menu never actually showed at that row. Resolving against a snapshot
    // taken when the menu was built (what the user is actually looking at) closes that: see
    // applyAddTrackMenuChoice. Deliberately 10, not 9 — FRO26's kCreateChannelsMenuId already
    // claims 9 on this same flat "+ Track" menu, and every flat id here must stay <
    // kAddInstrumentPluginMenuIdBase (100) with no collisions between them.
    static constexpr int kAddInstrumentPluginNoneMenuId = 10;
    static constexpr int kAddInstrumentPluginMenuIdBase = 100;
    // FRO13 (P9-7, docs/mixer/track-presets.md): "Insert Track Preset from File..." (next free fixed id
    // after 10), and the two grouped preset submenus (Audio/Instrument), each `base + index` into
    // its own snapshot vector below — same click-time-collector hazard as the plugin list
    // (a preset can be saved/deleted between menu-open and click), same snapshot-resolution fix.
    // 5000/6000 leave ~900 ids of headroom per kind above the plugin range (100..a few hundred in
    // practice) with no plausible collision.
    static constexpr int kInsertTrackPresetFromFileMenuId = 11;
    static constexpr int kAddTrackPresetAudioMenuIdBase = 5000;
    static constexpr int kAddTrackPresetInstrumentMenuIdBase = 6000;

    /** Adds a marker at the transport's current position, named "Marker N", coloured from the
     *  theme (see defaultMarkerColourArgb) — ONE recordTimelineChange when an undo manager is
     *  installed. Returns the new id, or an invalid one when there is no doc or the doc refused
     *  (kMaxMarkers). Public because it IS the "+ Track" menu's Add Marker action and the seam a
     *  test drives instead of the async menu. */
    synth::MarkerId addMarkerAtPlayhead();

    juce::TextButton& getAddTrackButton() noexcept { return addTrackButton_; }

    /** Applies an "+ Track" menu choice. Exposed as the headless test seam for a menu that never
     *  runs in a test process. Anything else is ignored. */
    void applyAddTrackMenuChoice(int menuId);
    juce::Viewport& getTrackHeaderViewport() noexcept { return trackHeaderViewport_; }
    // T166: the Viewport's content component — a pixel-level test seam for the track-reorder drop
    // indicator (TrackHeaderList::paintOverChildren), which a synthesized-event drag can otherwise
    // only assert through side effects (the eventual doc mutation), never through what actually
    // got painted. See createComponentSnapshot() at the call site.
    juce::Component& getTrackHeaderListForTest() noexcept { return trackHeaderList_; }
    int getTrackHeaderCount() const noexcept { return trackHeaderList_.headers.size(); }
    /** Header for the track at `index` in the doc's track order, or nullptr when out of range. */
    TimelineTrackHeaderComponent* getTrackHeaderAt(int index) const noexcept {
        return juce::isPositiveAndBelow(index, trackHeaderList_.headers.size())
                   ? trackHeaderList_.headers.getUnchecked(index)
                   : nullptr;
    }

    // ---- T161: focused track (ephemeral UI state — NOT on TimelineDoc, never touches undo/reconcile) ----
    // Which track header row currently holds keyboard focus, as an index into the doc's track order
    // (-1 = none) -- see TimelinePanelTrackHeaders.cpp for the full focus-movement contract.
    int getFocusedTrackIndexForTest() const noexcept { return focusedTrackIndex_; }
    bool selectAdjacentTrack(int direction);

    /** Builds the "+ Track" menu WITHOUT showing it — openAddTrackMenu() calls this then shows the
     *  result async. The headless test seam for inspecting menu CONTENTS (item text, enabled state,
     *  submenus). Triggers ensureInstrumentPluginsScanned() on the host first (openAddTrackMenu()'s
     *  own contract), so the Plugin submenu this builds reflects a scan that has at least started. */
    juce::PopupMenu buildAddTrackMenu();

    /** The Instrument submenu's "Plugin" sub-submenu options, re-collected fresh on every call --
     *  see kAddInstrumentPluginNoneMenuId's comment for why applyAddTrackMenuChoice resolves a
     *  click against buildAddTrackMenu()'s snapshot instead of calling this again. Empty when the
     *  host is null or offers nothing yet. */
    std::vector<synth::PluginIdentity> collectInstrumentPluginMenuOptions() const;

protected:
    /** Opens the "+ Track" button's menu (MIDI Track / Audio Track / Add Marker). The default
     *  implementation shows a real `juce::PopupMenu` via `showMenuAsync`. Protected virtual so a
     *  headless test can override it rather than crash on the display-less menu window -- see its
     *  definition in TimelinePanelTrackHeaders.cpp. */
    virtual void openAddTrackMenu();

private:
    // TimelineDoc::Listener — the single trigger for a header rebuild/refresh, AND the
    // clip-lane area's refresh (prunes the clip selection of anything the mutation removed).
    void timelineChanged(const synth::TimelineDoc& doc) override;

    void persistSnapChoice();
    // Rebuilds the header components when the set of track ids changed, and otherwise just
    // refreshes the existing ones in place (a mute toggle must not destroy and re-create rows).
    void syncTrackHeaders();
    // FRO14: ticks every header's channel chip. Started by setTrackHeaderHost() (nothing to meter
    // before the app wires one up) and defined in TimelinePanelTrackHeaders.cpp.
    void timerCallback() override;
    void layoutTrackHeaders();

    // ---- T161: focused track index -------------------------------------------
    // The index a track header row's onSelectRequested (click) reports lands here directly —
    // resolved from a TrackId rather than trusting a captured loop index, so it stays correct even
    // if track order/set changed between the header being built and the click landing.
    void setFocusedTrack(synth::TrackId id);
    // onFocusMoveRequested's destination: `direction` is -1 (Up) or +1 (Down) -- see its definition
    // in TimelinePanelTrackHeaders.cpp for the clamping rule.
    void moveFocusedTrack(int direction);
    // Brings row `index` fully inside the header viewport's visible window -- see its definition in
    // TimelinePanelTrackHeaders.cpp for why it reads trackScrollY rather than the viewport's own
    // cached visible area.
    void ensureTrackVisible(int index);
    int focusedTrackIndex_ = -1;

    // ---- FRO42 (P9-3h): Instrument -> Plugin submenu click-resolution snapshot ----
    // The exact option list `buildAddTrackMenu()` used to populate the "Plugin" sub-submenu, so
    // `applyAddTrackMenuChoice` resolves `kAddInstrumentPluginMenuIdBase + index` against what the
    // user actually saw rather than re-running collectInstrumentPluginMenuOptions() (which can have
    // changed — see kAddInstrumentPluginNoneMenuId's comment). Left as-is between menu builds
    // (never cleared on dismiss/apply): a stale snapshot from a menu that was shown but never acted
    // on is harmless, since nothing indexes it until another click arrives.
    std::vector<synth::PluginIdentity> instrumentPluginMenuSnapshot_;
    // FRO13 (P9-7): same snapshot-not-re-collect contract, for the two grouped preset submenus.
    std::vector<synth::TrackPresetInfo> audioTrackPresetMenuSnapshot_;
    std::vector<synth::TrackPresetInfo> instrumentTrackPresetMenuSnapshot_;

    // ---- T166: track-reorder drag (whole-row drag) ----
    // See syncTrackHeaders()'s definition in TimelinePanelTrackHeaders.cpp for the division of
    // labour between the row and this panel.
    synth::TrackId draggingTrackId_; // invalid (default) when no drag is in progress
    int dragInsertionIndex_ = -1;    // boundary (0..headerCount) the drag would drop at; -1 = none
    void beginTrackDrag(synth::TrackId trackId, int screenY);
    void updateTrackDrag(int screenY);
    void endTrackDrag(int screenY);
    // Screen Y -> a BOUNDARY index in [0, headerCount] ("insert before row N"), rounded to the
    // nearest row edge. Shared by updateTrackDrag (live drop-indicator position) and endTrackDrag
    // (the actual drop target), which is why this returns a boundary rather than a resting index —
    // endTrackDrag is the one place that converts a boundary into TimelineDoc::moveTrack's target.
    int trackDropBoundaryForScreenY(int screenY) const;

    // ---- Automation strip ----
    // A header's "A" button click lands here. The header itself never knows open/closed state, so
    // this is the one place that decides: if the strip is already open on THIS track's lane, close
    // it; otherwise open it on the track's first lane. A track with no lanes is a no-op (the button
    // is hidden in that case anyway — see TimelineTrackHeaderComponent::refreshFromDoc()).
    void toggleAutomationForTrack(synth::TrackId trackId);
    // Repopulates the lane picker from the doc, preserving the current selection when it still
    // resolves. Called whenever the doc notifies while the strip is open, and by showAutomationLane().
    void syncAutomationLaneCombo();
    // Re-reads the active lane's recordMode into the combo (no notification — this is a REFLECTION
    // of doc state, not an edit).
    void syncAutomationRecordModeCombo();

    // ---- Clip clipboard ----
    // One captured clip, relative to the earliest selected clip's start at copy time (see
    // copySelectedClips()). Notes are already clip-relative in the doc, so they need no rebasing;
    // addNote() reassigns their ids on paste regardless of what's stored here.
    struct ClipboardClip {
        synth::TrackId originalTrack;
        // The track kind this clip needs on paste, derived at COPY time from the payload
        // (non-empty assetRef -> Audio, else Midi) rather than from the track it sat on: the
        // payload is what decides where it can be played, and it is also what
        // TimelineDoc::moveClipToTrack checks.
        synth::TrackKind requiredKind = synth::TrackKind::Midi;
        double relativeStartBeat = 0.0;
        double lengthBeats = 4.0;
        juce::String name;
        std::vector<synth::MidiNote> notes; // each note's own muted flag travels with it
        bool muted = false;
        // Audio fields — captured and restored so copy/paste of an audio clip yields the same
        // clip, not a silent husk pointing at nothing (they were dropped before, which is exactly
        // what "the clipboard drops audio clips" looked like from the outside).
        juce::String assetRef;
        double gainDb = 0.0;
        double fadeInBeats = 0.0;
        double fadeOutBeats = 0.0;
        double sourceStartSeconds = 0.0;
    };
    std::vector<ClipboardClip> clipClipboard_;
    // The beatsPerBar TimelineViewState::snapBeat needs for pasteClipsAtPlayhead()'s Snap::Bar
    // case — same formula (and same "4.0 with no transport" fallback) as every other timeline
    // sub-component's own currentBeatsPerBar()/beatsPerBarFrom() helper.
    double currentBeatsPerBarForPaste() const;
    // The first track of `kind` in doc order, or an invalid id. The paste fallback (see
    // pasteClipsAtPlayhead) and nothing else.
    synth::TrackId firstTrackOfKind(synth::TrackKind kind) const;

    // ---- Edit-tool strip ----
    EditTool activeTool_ = EditTool::Select;
    // Six radio-group icon buttons, indexed by EditTool. unique_ptrs because juce::DrawableButton
    // has no default constructor (it needs a name and a style up front).
    std::array<std::unique_ptr<juce::DrawableButton>, kAllEditTools.size()> toolButtons_;
    // Re-applies the icons and the active-tool highlight colour from the current LookAndFeel.
    // Called from the constructor, lookAndFeelChanged() and parentHierarchyChanged() below.
    void applyToolStripTheme();
    void lookAndFeelChanged() override;
    // See TimelinePanelStrips.cpp for why this ALSO needs its own hook, separate from
    // lookAndFeelChanged() above.
    void parentHierarchyChanged() override;

    // The Viewport's content: a plain container whose height is (track count * row height). Also
    // draws the T166 track-reorder drop indicator -- see its paintOverChildren() definition in
    // TimelinePanelTrackHeaders.cpp for why.
    struct TrackHeaderList : juce::Component {
        explicit TrackHeaderList(TimelinePanelComponent& owner)
            : owner_(owner) {}
        void paintOverChildren(juce::Graphics& g) override;
        juce::OwnedArray<TimelineTrackHeaderComponent> headers;

    private:
        TimelinePanelComponent& owner_;
    };

    TimelineViewState viewState_;
    // The panel owns the clip selection; the lane area only holds a reference to it (same
    // relationship it has with viewState_ below).
    synth::ui::ClipSelectionModel clipSelection_;
    TimelineRulerComponent ruler_{viewState_};
    // Positioned over gridLanesBounds_ in resized() — the SAME rect the grid below it is painted
    // into by this component's own paint(). Added as a child AFTER the grid is painted (parent
    // paint() always precedes children) and BEFORE playhead_ (added last, below), so z-order reads
    // grid -> clips -> playhead with no second place ever painting the grid.
    synth::ui::TimelineClipLaneArea clipLaneArea_{viewState_, clipSelection_};
    // Occupies the exact same rect as clipLaneArea_ (gridLanesBounds_), added right after
    // it (addChildComponent — not addAndMakeVisible, so it starts invisible) so z-order still
    // reads grid -> clips/piano-roll -> playhead. Only one of clipLaneArea_/pianoRoll_ is visible
    // at a time; openPianoRoll()/closePianoRoll() toggle it. The roll owns its OWN beat<->x
    // mapping (keys column as a real gutter, its own zoom/scroll), which is why it is registered as
    // playhead_'s LocalPlayheadClient: while it is open the overlay skips its rows and hands it the
    // drawn beat instead. See PianoRollComponent's class comment.
    synth::ui::PianoRollComponent pianoRoll_{viewState_};
    // Added LAST in the constructor so it sits on top of the ruler AND the clip lane area/piano
    // roll; spans ruler + lanes and intercepts no mouse clicks (see TimelinePlayheadOverlay's ctor).
    TimelinePlayheadOverlay playhead_{viewState_};
    juce::ComboBox snapCombo_;
    // Toggles TimelineViewState::snapEnabled from the panel chrome, so the switch is discoverable
    // without opening a clip. Toggle STATE mirrors the shared flag via setSnapEnabled() — the
    // button never owns it. Labelled "Snap" rather than with its key: the key moved from Q to J
    // (Cubase's snap key — Q is Cubase's *quantise*, which is what the roll uses it for), and a
    // button that spells its own letter goes stale the moment the binding is rebound. The live key
    // is in the tooltip, through synth::shortcutHintFor.
    juce::TextButton snapToggleButton_{"Snap"};
    // The one writer for the snap switch from panel chrome/keys: flips the flag, persists, syncs
    // the button's lit state, and repaints every grid painter.
    void setSnapEnabled(bool enabled);
    // Left-aligned in the transport-bar strip, the snap combo stays right of it.
    TimelineTransportBar transportBar_;

    // ---- Follow playhead (see the public accessors above) ----
    bool followPlayhead_ = false;
    juce::DrawableButton followPlayheadButton_{"Follow Playhead", juce::DrawableButton::ImageOnButtonBackground};
    void persistFollowPlayheadChoice();

    // The slice of transport state the RULER paints, diffed by updateFromTransport. `hasRulerState_`
    // keeps the very first poll from counting as a change (the default-constructed struct below is
    // not what a live transport reports — its loop end starts at 4 beats).
    struct RulerTransportState {
        int timeSigNumerator = 4;
        int timeSigDenominator = 4;
        bool looping = false;
        double loopStartPpq = 0.0;
        double loopEndPpq = 0.0;

        bool operator==(const RulerTransportState& other) const noexcept {
            return timeSigNumerator == other.timeSigNumerator && timeSigDenominator == other.timeSigDenominator &&
                   looping == other.looping && loopStartPpq == other.loopStartPpq && loopEndPpq == other.loopEndPpq;
        }
    };
    RulerTransportState rulerState_;
    bool hasRulerState_ = false;
    int transportUpdateCount_ = 0;

    juce::ApplicationProperties* appProperties_ = nullptr;
    synth::TimelineDoc* doc_ = nullptr;
    TrackHeaderHost* trackHeaderHost_ = nullptr;
    // Non-owning, set by setTransport() alongside the sub-component forwards it already
    // does. Every other consumer of the transport reads it from its OWN copy (ruler_/playhead_/
    // transportBar_/clipLaneArea_/pianoRoll_/automationEditor_); this is the one operation the
    // panel itself performs directly against it — reading the CURRENT position/time-signature at
    // paste time (see pasteClipsAtPlayhead()).
    synth::TransportService* transport_ = nullptr;

    // NOTE AUDITION — the track the currently-sounding preview note was sent TO. Invalid means
    // nothing is sounding. See the onAuditionNote wiring in the constructor (TimelinePanelComponent.cpp)
    // for the latch rationale.
    synth::TrackId auditionTrackLatch_;

    // The button opens a MIDI/Audio menu rather than adding a MIDI track outright.
    juce::TextButton addTrackButton_{"+ Track"};

    // The theme's colour for a NEW marker (see addMarkerAtPlayhead). Falls back to the model's own
    // default with no themed LookAndFeel installed, like every other paint-time resolve here.
    juce::uint32 defaultMarkerColourArgb() const;

    // ---- Vertical track scroll/zoom (shared TimelineViewState::trackScrollY/rowHeightScale) ----
    // The themed row height with the shared vertical-zoom factor applied -- see its definition in
    // TimelinePanelLayout.cpp for why it duplicates TimelineClipLaneArea::getRowHeight.
    int currentRowHeight() const;
    double maxTrackScrollPx() const;
    void scrollTrackRows(double deltaPx);
    void zoomTrackRows(double factor, double anchorLaneY);
    // anchorX is in the ruler's coordinate space (== TimelineViewState's x origin) -- see its
    // definition in TimelinePanelLayout.cpp for why this is the ONE horizontal-zoom writer.
    void zoomHorizontalAroundX(double factor, double anchorX);
    // The lanes-region anchors a keyboard zoom uses: the centre of what is on screen, in the same
    // coordinate spaces the wheel/pinch handlers feed their anchors from.
    double visibleCentreXInRuler() const noexcept;
    double visibleCentreYInLanes() const noexcept;

    // App-level scroll inversion — see setScrollInverted(). Every plain-scroll branch in
    // mouseWheelMove goes through synth::ui::scrollAmount with this flag.
    bool scrollInverted_ = false;
    // App-level ZOOM-scroll inversion — see setZoomScrollInverted(). Both Cmd-modified zoom
    // branches in mouseWheelMove XOR this against synth::ui::wheelGestureIsUpward(wheel).
    bool zoomScrollInverted_ = false;

    // Non-owning, may stay null (see setShortcutManager). Expected to outlive this component --
    // see the destructor's definition in TimelinePanelComponent.cpp for the lifetime contract and
    // why the destructor itself does not trust this pointer directly.
    ShortcutManager* shortcuts_ = nullptr;
    // Mirrors `shortcuts_` (set together in setShortcutManager), used ONLY by the destructor.
    // Automatically null once the referenced ShortcutManager is destroyed, unlike `shortcuts_`
    // itself, which cannot know.
    juce::WeakReference<ShortcutManager> shortcutsWeak_;
    // juce::ChangeListener — rebuilds the tool-strip/snap/follow tooltips on every bindings change.
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    // Rebuilds every dynamic shortcut-hint tooltip this panel owns -- see its definition in
    // TimelinePanelComponent.cpp for the full call-site list.
    void refreshShortcutTooltips();
    // True when `key` is what the user has bound to `actionId`. With no manager installed this is
    // `key == fallback`; with one installed the fallback is not consulted at all. See its
    // definition in TimelinePanelShortcuts.cpp for why this duplicates PianoRollComponent's own
    // copy rather than sharing it.
    bool matchesAction(const juce::KeyPress& key, const juce::String& actionId, const juce::KeyPress& fallback) const;
    // Pushes trackScrollY into the header viewport and repaints the lanes — the ONE place the two
    // columns are brought back in step after any scroll/zoom writer.
    void syncTrackScroll();

    // Forwards scrollbar/drag scrolling of the header column into the shared trackScrollY, so the
    // lanes follow a scrollbar drag exactly like they follow the wheel.
    struct HeaderViewport : juce::Viewport {
        std::function<void(int)> onScrolledY;
        void visibleAreaChanged(const juce::Rectangle<int>& newVisibleArea) override {
            if (onScrolledY)
                onScrolledY(newVisibleArea.getY());
        }
    };
    HeaderViewport trackHeaderViewport_;
    TrackHeaderList trackHeaderList_{*this};

    // The strip's own copy of the undo manager (record-mode/lane-picker edits made directly
    // by this panel, as opposed to automationEditor_'s edits, which it holds its own copy for).
    AppUndoManager* undoManager_ = nullptr;

    // Automation strip chrome — docked at the bottom of gridLanesBounds_ when automationStripVisible_.
    // All start invisible (addChildComponent, not addAndMakeVisible); resized()/showAutomationLane()/
    // closeAutomationStrip() are the only things that flip their visibility.
    synth::ui::AutomationLaneEditor automationEditor_{viewState_};
    juce::TextButton automationToolPointerButton_;
    juce::TextButton automationToolPencilButton_;
    juce::TextButton automationToolLineButton_;
    juce::TextButton automationToolEraserButton_;
    juce::ComboBox laneCombo_;
    juce::ComboBox recordModeCombo_;
    juce::TextButton automationCloseButton_;
    bool automationStripVisible_ = false;
    synth::LaneId selectedAutomationLane_;
    juce::Rectangle<int> automationStripBounds_; // empty when the strip is closed

    juce::Rectangle<int> transportBarBounds_;
    juce::Rectangle<int> trackHeaderBounds_;
    juce::Rectangle<int> lanesBounds_;
    juce::Rectangle<int> gridLanesBounds_; // lanesBounds_ minus the ruler strip at its top

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimelinePanelComponent)
};

} // namespace synth::ui
