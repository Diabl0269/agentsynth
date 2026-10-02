#pragma once

#include "Mixer/TrackPresetManager.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "Transport/TransportNudge.h"
#include "UI/Layout/EdgeResizeHandle.h"
#include "UI/Layout/ReorderDrag/ReorderCancelKey.h"
#include "UI/Layout/ReorderDrag/ReorderDragAnimator.h"
#include "UI/Layout/ReorderDrag/ReorderFramePump.h"
#include "UI/Layout/ScrollTween.h"
#include "UI/Layout/SidePane/SidePane.h"
#include "UI/Layout/SidePane/SidePaneToggleButton.h"
#include "UI/PianoRoll/PianoRollComponent/PianoRollComponent.h"
#include "UI/Timeline/AutomationLanes/LaneShapes/DrawShapeStrip.h"
#include "UI/Timeline/AutomationLanes/TimelineAutomationLanes/TimelineAutomationLanes.h"
#include "UI/Timeline/ClipSelectionModel.h"
#include "UI/Timeline/CursorGlide/TimelineCursorGlide.h"
#include "UI/Timeline/EditTool.h"
#include "UI/Timeline/TimelineClipLaneArea/TimelineClipLaneArea.h"
#include "UI/Timeline/TimelinePlayheadOverlay.h"
#include "UI/Timeline/TimelineRoutingPane/TimelineRoutingPane.h"
#include "UI/Timeline/TimelineRowLayout.h"
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
namespace ui {
class ModMatrixPicker; // Forward declaration (Source/UI/Graph/ModMatrixPicker.h): the add-automation picker
} // namespace ui
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
// (BottomDockComponent) resizes the whole dock from every tab. The panel never sets its
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
    // ONE shared timer for the header column's channel-chip meters -- see timerCallback()
    // below.
    , private juce::Timer {
public:
    TimelinePanelComponent();
    ~TimelinePanelComponent() override;

    void paint(juce::Graphics& g) override;
    // Focus-region outline, drawn OVER children -- see paintOverChildren()'s definition in
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
    /** Every button this panel owns that has a keyboard shortcut, with its action id: the hint overlay's targets. */
    std::vector<std::pair<juce::Component*, juce::String>> getShortcutHintTargets();

    // ---- Draw shapes and the lane range (TimelinePanelShapes.cpp) ----
    void setDrawShape(DrawShape shape);
    DrawShape getDrawShape() const noexcept { return drawShape_; }
    /** Picks Draw and `shape`, and stamps it over the lane range when there is one: a shape button or key. */
    void pickDrawShape(DrawShape shape);
    DrawShapeStrip& getDrawShapeStrip() noexcept { return shapeStrip_; }
    TimelineAutomationLanes& getAutomationLanes() noexcept { return automationLanes_; }

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
    // repeatSelectedClips(1): one copy of the whole selection, starting where the selection ends, in
    // one recordTimelineChange; the new clips end up selected. Returns false when nothing is
    // selected or there's no doc.
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
    // Whether the clip lanes hold a live Range-tool range — Copy/Cut then act on it, not the selection.
    bool hasRangeSelection() const;
    // Captures the range's contents, clipped to its edges, into the clip clipboard. False if nothing.
    bool copyRange();
    // copyRange() then deletes the range as ONE recordTimelineChange. False when the copy fails.
    bool cutRange();

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

    // ---- Automation lanes (folded out under their tracks -- TimelinePanelAutomation.cpp) ----
    // Right-click-any-knob (ModuleComponent -> GraphEditor::onAutomateParameterRequested ->
    // MainComponent::automateParameter) lands in showAutomationLane().

    /** Expands the lane's track, scrolls its row into view and focuses its editor. No-op for a lane
     *  that doesn't resolve. */
    void showAutomationLane(synth::LaneId id);
    /** The lane last shown or focused; invalid when none. */
    synth::LaneId getSelectedAutomationLane() const noexcept { return selectedAutomationLane_; }
    /** Folds a track's lanes open or closed (runtime-only, never saved). */
    void setTrackAutomationExpanded(synth::TrackId track, bool expanded);
    bool isTrackAutomationExpandedForTest(synth::TrackId track) const;
    /** The lane's row over the lanes region, in this panel's coordinates; empty when not shown. */
    juce::Rectangle<int> laneRowBoundsForTest(synth::LaneId lane) const;
    /** The lane's editor / header while its row is shown, else nullptr. */
    AutomationLaneEditor* laneEditorForTest(synth::LaneId lane) const;
    AutomationLaneHeaderComponent* laneHeaderForTest(synth::LaneId lane) const;
    /** Re-derives the modulator rows under every open lane from the graph. Call after any graph change. */
    void refreshModulators();
    /** The lane's `index`th modulator row / band while shown, else nullptr. */
    ModulatorRow* modulatorRowForTest(synth::LaneId lane, int index) const;
    ModulatorBand* modulatorBandForTest(synth::LaneId lane, int index) const;
    /** That row over the lanes region, in this panel's coordinates; empty when not shown. */
    juce::Rectangle<int> modulatorRowBoundsForTest(synth::LaneId lane, int index) const;

    // ---- Adding a lane from the timeline (TimelinePanelAddAutomation.cpp) ----
    /** Opens the picker of `track`'s automatable parameters (the "+ Add automation..." row's and the header
     *  menu's action), anchored on `anchor`; picking one creates the lane on that track and shows it. A
     *  no-op without a host or a doc. */
    void openAddAutomationPicker(synth::TrackId track, juce::Component& anchor);
    /** Receives the picker instead of it being launched in a CallOutBox, which a headless test cannot
     *  show. The hook owns the picker and may pick from it. Null = launch for real. */
    void setAddAutomationPickerHookForTest(std::function<void(std::unique_ptr<ModMatrixPicker>)> hook) {
        addAutomationPickerHook_ = std::move(hook);
    }
    /** The track's "+ Add automation..." row while its lanes are open, else nullptr. */
    AddAutomationRow* addAutomationRowForTest(synth::TrackId track) const;
    /** That row over the panel, in this panel's coordinates; empty when the track has none. */
    juce::Rectangle<int> addAutomationRowBoundsForTest(synth::TrackId track) const;

    /** One lane choice: an EXISTING doc lane labelled "NodeName \xC2\xB7 param name", or an
     *  "Add lane..." entry for a parameter that has none yet (`isAddEntry`; `id` is meaningful only
     *  when false). Existing lanes first in track order, then add entries; index i is choice i + 1. */
    struct AutomationLaneOption {
        synth::LaneId id;
        juce::String label;
        bool isAddEntry = false;
        synth::ui::TrackHeaderHost::PluginLaneOption addOption; // populated only when isAddEntry
    };
    std::vector<AutomationLaneOption> collectAutomationLaneOptions() const;

    /** Applies choice `selectedId` (index + 1) of collectAutomationLaneOptions(): shows an existing
     *  lane, or adds the offered lane first. The headless backing of an "Add lane" choice. */
    void applyAutomationLaneMenuChoice(int selectedId);

    // The panel's own keys (tools, snap, loop, follow) -- see TimelinePanelShortcuts.cpp.
    bool keyPressed(const juce::KeyPress& key) override;

    // The cursor-glide keys are held, so their release has to be seen -- see TimelinePanelCursorGlide.cpp.
    // Both return false: they only observe, so the event keeps bubbling.
    bool keyStateChanged(bool isKeyDown) override;
    void modifierKeysChanged(const juce::ModifierKeys& modifiers) override;

    /** The cursor glide moves the transport through this state, so it accumulates with the cursor
     *  nudge actions instead of fighting them. Non-owning; null reverts to the panel's own. */
    void setTransportNudgeState(synth::TransportNudgeState* state) noexcept {
        nudge_ = state != nullptr ? state : &ownNudge_;
    }
    synth::ui::TimelineCursorGlide& getCursorGlide() noexcept { return cursorGlide_; }
    void setGlideClockForTest(std::function<double()> nowMs) { glideClockForTest_ = std::move(nowMs); }

    // Trackpad pinch: plain = horizontal zoom, Shift = vertical (row height) zoom.
    void mouseMagnify(const juce::MouseEvent& e, float scaleFactor) override;

    // Pure geometry getters — later tasks and tests build on the same rects rather than
    // re-deriving the arithmetic in resized().
    juce::Rectangle<int> getTransportBarBounds() const noexcept { return transportBarBounds_; }
    // The WHOLE left column, including the "+ MIDI Track" strip at its top — the three regions
    // still tile the panel exactly (see TimelinePanelComponentTest.PanelRegionsTile).
    juce::Rectangle<int> getTrackHeaderBounds() const noexcept { return trackHeaderBounds_; }

    // ---- Track-header column width (drag the seam; remembered in the user settings) ----
    static constexpr int kMinTrackHeaderWidth = 140;
    static constexpr int kMaxTrackHeaderWidth = 420;
    static constexpr int kTrackHeaderWidthKeyStep = 16;
    /** Clamps, re-lays out; `persist` writes the user setting. */
    void setTrackHeaderWidth(int width, bool persist);
    int getTrackHeaderWidth() const;
    /** +1 taller, -1 shorter, 0 default: one undo step through the host. */
    void stepTrackHeight(synth::TrackId track, int direction);
    static constexpr double kTrackHeightStepFactor = 1.25;
    int defaultTrackHeaderWidth() const;
    EdgeResizeHandle& getTrackHeaderWidthHandle() noexcept { return trackHeaderWidthHandle_; }
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

    /** The user's bindings for this panel's OWN keys: the seven tool digits, the snap toggle, the loop
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

    // ---- Side pane: the selected track's routing (docs/timeline/tracks.md#routing-from-the-side-pane) ----
    // A child of this panel, so it travels with the panel into a detached window; closed until the user opens
    // it, then remembered per tab ("timeline") -- see TimelinePanelSidePane.cpp.
    SidePane& getSidePane() noexcept { return sidePane_; }
    SidePaneToggleButton& getSidePaneButton() noexcept { return sidePaneButton_; }
    TimelineRoutingPane& getRoutingPane() noexcept { return routingPane_; }
    /** Shows or hides the side pane; `forceOpen` only ever opens it. */
    bool toggleSidePane(bool forceOpen = false);
    /** Points the routing pane at the selected track (the focused header row) and re-reads it -- called on a
     *  selection, document or graph change. */
    void refreshRoutingPane();
    /** The selected track: the header row that was last clicked or moved to with the arrow keys; invalid when none. */
    synth::TrackId getSelectedTrackId() const;

    // ---- Track headers ----
    // Menu ids for the "+ Track" button's menu. Numbered from 1 because juce::PopupMenu reserves 0
    // for "dismissed".
    static constexpr int kAddMidiTrackMenuId = 1;
    static constexpr int kAddAudioTrackMenuId = 2;
    // Below a separator, because a marker is NOT a track: it adds no header row and no graph node.
    // It shares this menu because "+ Track" is where a user reaches for "add something to the
    // arrangement", and a second button for one item would not earn its pixels.
    static constexpr int kAddMarkerMenuId = 3;
    // The Instrument submenu's three audio-producing MIDI instrument choices — see
    // TrackHeaderHost::addInstrumentTrack's own comment for why the set is exactly these three (not
    // every isMidiInstrumentType() member: Poly MIDI/Sequencer/Poly Sequencer don't produce audio).
    static constexpr int kAddInstrumentOscillatorMenuId = 4;
    static constexpr int kAddInstrumentWavetableMenuId = 5;
    static constexpr int kAddInstrumentSamplerMenuId = 6;
    // Poly variants of the Oscillator/Wavetable entries above — Sampler has no "poly"
    // parameter, so it has no poly entry.
    static constexpr int kAddInstrumentOscillatorPolyMenuId = 7;
    static constexpr int kAddInstrumentWavetablePolyMenuId = 8;
    // docs/mixer/mixer.md#creating-channels-in-an-existing-project: "Create channels" for existing
    // projects. Lives on this same menu rather than a per-track context menu or a mixer panel — there is no mixer panel
    // yet, and "+ Track" is already where every other channel-creating action in this doc lives (Audio Track,
    // Instrument Track); a project-wide sweep belongs beside them, not off a single track header, since it acts on
    // every track at once.
    static constexpr int kCreateChannelsMenuId = 9;
    // The Instrument submenu's "Plugin" sub-submenu. The single disabled row shown
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
    // applyAddTrackMenuChoice. Deliberately 10, not 9 — kCreateChannelsMenuId already
    // claims 9 on this same flat "+ Track" menu, and every flat id here must stay <
    // kAddInstrumentPluginMenuIdBase (100) with no collisions between them.
    static constexpr int kAddInstrumentPluginNoneMenuId = 10;
    static constexpr int kAddInstrumentPluginMenuIdBase = 100;
    // docs/mixer/track-presets.md: "Insert Track Preset from File..." (next free fixed id
    // after 10), and the two grouped preset submenus (Audio/Instrument), each `base + index` into
    // its own snapshot vector below — same click-time-collector hazard as the plugin list
    // (a preset can be saved/deleted between menu-open and click), same snapshot-resolution fix.
    // 5000/6000 leave ~900 ids of headroom per kind above the plugin range (100..a few hundred in
    // practice) with no plausible collision.
    static constexpr int kInsertTrackPresetFromFileMenuId = 11;
    static constexpr int kAddTrackPresetAudioMenuIdBase = 5000;
    static constexpr int kAddTrackPresetInstrumentMenuIdBase = 6000;
    static constexpr int kAddTrackPresetBusMenuIdBase = 7000; // same contract as the two above

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
    // The Viewport's content component — a pixel-level test seam for what a track-reorder drag
    // paints. See createComponentSnapshot() at the call site.
    juce::Component& getTrackHeaderListForTest() noexcept { return trackHeaderList_; }
    /** True from the first drag step of a track reorder until its drop has finished settling. */
    bool isTrackReorderActiveForTest() const noexcept { return trackReorder_.isReordering(); }
    /** The Esc key press a real track drag would receive from the window. */
    bool sendEscapeToTrackDragForTest() {
        return trackCancelKey_.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey), this);
    }
    int getTrackHeaderCount() const noexcept { return trackHeaderList_.headers.size(); }
    /** Header for the track at `index` in the doc's track order, or nullptr when out of range. */
    TimelineTrackHeaderComponent* getTrackHeaderAt(int index) const noexcept {
        return juce::isPositiveAndBelow(index, trackHeaderList_.headers.size())
                   ? trackHeaderList_.headers.getUnchecked(index)
                   : nullptr;
    }

    // ---- focused track (ephemeral UI state — NOT on TimelineDoc, never touches undo/reconcile) ----
    // Which track header row currently holds keyboard focus, as an index into the doc's track order
    // (-1 = none) -- see TimelinePanelTrackHeaders.cpp for the full focus-movement contract.
    int getFocusedTrackIndexForTest() const noexcept { return focusedTrackIndex_; }
    bool selectAdjacentTrack(int direction);
    bool handleRootFocusKey(const juce::KeyPress& key);

    // ---- "+ Track" keyboard stop (TimelinePanelTrackHeaders.cpp) ----
    // The last stop of the track-header column; rationale beside the definitions.
    /** Message thread only. Moves keyboard focus to "+ Track". */
    void focusAddTrackButton();
    /** True while "+ Track" holds keyboard focus (or, in test mode, was last focused through this panel). */
    bool isAddTrackButtonFocused() const;
    /** Keys for "+ Track" while it holds focus; false for any other focus owner or an unclaimed key. */
    bool handleAddTrackButtonKey(const juce::KeyPress& key);
    /** The "+ Track" menu's close callback (rationale beside the definition). */
    void finishAddTrackMenu(int result, bool openedFromKeyboard);
    /** Headless stand-in for real focus (no native window): focus moves are recorded, not grabbed. */
    void setRecordFocusForTest(bool record) noexcept { recordFocusForTest_ = record; }

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
    // Ticks every header's channel chip. Started by setTrackHeaderHost() (nothing to meter
    // before the app wires one up) and defined in TimelinePanelTrackHeaders.cpp.
    void timerCallback() override;
    void layoutTrackHeaders();

    // ---- Draw shapes (TimelinePanelShapes.cpp) ----
    void initDrawShapes();
    void updateShapeStripShowing();
    void refreshShapeTooltips();
    bool handleDrawShapeKey(const juce::KeyPress& key);
    // Lays out the transport row (snap, follow, shape strip, tool strip, transport bar).
    void layoutTransportRow();

    // ---- Side pane (TimelinePanelSidePane.cpp) ----
    void initSidePane();
    // Carves the pane's current width off the left of `body` (the area under the transport strip).
    void layoutSidePane(juce::Rectangle<int>& body);
    // The routing pane's meter, on this panel's existing 15 Hz tick.
    void tickRoutingPane();
    // Fills the snap selector (part of the constructor, split out to keep it under its size ratchet).
    void initSnapCombo();

    // ---- focused track index -------------------------------------------
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
    void stepTrackFocusFromHeader(int direction);
    bool recordFocusForTest_ = false;
    bool addTrackFocusRecorded_ = false;
    bool addTrackFromTop_ = false; // "+ Track" was entered from above (root Down, Up off row 0), not from the last row

    // ---- Clip keyboard mode (TimelinePanelClipKeyboard.cpp) ----
    bool enterTrackClips(synth::TrackId trackId);
    void followKeyboardClip(synth::ClipId id);
    void returnToTrackHeader(synth::TrackId trackId);
    void wireClipLaneCallbacks();

    // ---- Instrument -> Plugin submenu click-resolution snapshot ----
    // The exact option list `buildAddTrackMenu()` used to populate the "Plugin" sub-submenu, so
    // `applyAddTrackMenuChoice` resolves `kAddInstrumentPluginMenuIdBase + index` against what the
    // user actually saw rather than re-running collectInstrumentPluginMenuOptions() (which can have
    // changed — see kAddInstrumentPluginNoneMenuId's comment). Left as-is between menu builds
    // (never cleared on dismiss/apply): a stale snapshot from a menu that was shown but never acted
    // on is harmless, since nothing indexes it until another click arrives.
    std::vector<synth::PluginIdentity> instrumentPluginMenuSnapshot_;
    // Same snapshot-not-re-collect contract, for the two grouped preset submenus.
    std::vector<synth::TrackPresetInfo> audioTrackPresetMenuSnapshot_;
    std::vector<synth::TrackPresetInfo> instrumentTrackPresetMenuSnapshot_;
    std::vector<synth::TrackPresetInfo> busTrackPresetMenuSnapshot_; // same contract

    // ---- track-reorder drag (whole-row drag) ----
    // The row detects the gesture and hands up raw screen Y; this panel feeds it to the shared
    // ReorderDragAnimator (vertical axis, list coordinates). See TimelinePanelTrackDrag.cpp.
    void beginTrackDrag(synth::TrackId trackId, int screenY);
    void updateTrackDrag(int screenY);
    void endTrackDrag();
    void commitTrackDrag();
    void cancelTrackDrag();  // Esc
    void discardTrackDrag(); // a header rebuild is about to destroy the rows a held drag belongs to
    void startTrackFramesIfNeeded();
    void onTrackReorderFrame();
    void autoscrollForTrackPointer(int screenY);
    float trackPointerY(int screenY) const;
    // Places every row: static slots, or the animator's positions while a reorder is in flight.
    void placeTrackHeaders();

    ReorderDragAnimator trackReorder_;
    ReorderFramePump trackFrames_{*this};
    ReorderCancelKey trackCancelKey_;
    std::vector<synth::TrackId> reorderTrackIds_; // animator keys -> track ids, at press time
    synth::TrackId liftedTrackId_;                // the row drawn lifted (dragged, then settling)
    unsigned trackGenerationSeen_ = 0;
    float lastDraggedTrackStart_ = 0.0f;
    bool trackDragCancelled_ = false; // Esc pressed in this gesture
    bool committingTrackDrag_ = false;

    // ---- Automation lanes (TimelinePanelAutomation.cpp) ----
    // A header's fold arrow (or its key) lands here.
    void toggleAutomationForTrack(synth::TrackId trackId);
    // Wires automationLanes_ into the panel (part of the constructor, kept out of its size ratchet).
    void initAutomationLanes();
    // Re-syncs the lane pools, pushes their row geometry into the clip lanes and relayouts.
    void syncAutomationLanes();
    // Pushes the lanes' extra heights and the section-row override into the clip lanes' layout.
    void pushAutomationGeometry();
    // Pushes fold state into the track headers, relayouts the rows and re-clamps the scroll.
    void layoutAutomationRows();
    // Repositions the lane editors after a scroll, resize or relayout.
    void placeLaneBodies();
    // The picker for `track`, wired to create and show the chosen lane; null without a host or doc.
    std::unique_ptr<ModMatrixPicker> buildAddAutomationPickerFor(synth::TrackId track);
    void addAutomationLaneFromPicker(synth::TrackId track, const TrackHeaderHost::AutomatableParameter& parameter);
    std::function<void(std::unique_ptr<ModMatrixPicker>)> addAutomationPickerHook_;

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
    // Seven radio-group icon buttons, indexed by EditTool. unique_ptrs because juce::DrawableButton
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
    // draws the track-reorder gap marker -- see its paint() definition in
    // TimelinePanelTrackDrag.cpp.
    struct TrackHeaderList : juce::Component {
        explicit TrackHeaderList(TimelinePanelComponent& owner)
            : owner_(owner) {}
        void paint(juce::Graphics& g) override;
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
    // transportBar_/clipLaneArea_/pianoRoll_/automationLanes_); this is the one operation the
    // panel itself performs directly against it — reading the CURRENT position/time-signature at
    // paste time (see pasteClipsAtPlayhead()).
    synth::TransportService* transport_ = nullptr;

    // ---- Cursor glide (TimelinePanelCursorGlide.cpp) ----
    bool handleCursorGlideKey(const juce::KeyPress& key);
    /** Pages the view to show `beat` when it is off screen: the follow-playhead behaviour. */
    void pageViewToShowBeat(double beat);
    synth::ui::TimelineCursorGlide::Host makeCursorGlideHost();
    synth::TransportNudgeState ownNudge_;
    synth::TransportNudgeState* nudge_ = &ownNudge_;
    double lastOutputLatencySeconds_ = 0.0;     // as last handed to updateFromTransport
    std::function<double()> glideClockForTest_; // declared before cursorGlide_, whose host reads it
    synth::ui::TimelineCursorGlide cursorGlide_{*this, makeCursorGlideHost()};

    // NOTE AUDITION — the track the currently-sounding preview note was sent TO. Invalid means
    // nothing is sounding. See the onAuditionNote wiring in the constructor (TimelinePanelComponent.cpp)
    // for the latch rationale.
    synth::TrackId auditionTrackLatch_;

    // The button opens a MIDI/Audio menu rather than adding a MIDI track outright.
    juce::TextButton addTrackButton_{"+ Track"};
    EdgeResizeHandle trackHeaderWidthHandle_{EdgeResizeHandle::Axis::Horizontal};
    int trackHeaderWidth_ = 0; // 0 = the themed default
    int trackHeaderWidthAtPress_ = 0;
    void initTrackHeaderWidthHandle();

    // The theme's colour for a NEW marker (see addMarkerAtPlayhead). Falls back to the model's own
    // default with no themed LookAndFeel installed, like every other paint-time resolve here.
    juce::uint32 defaultMarkerColourArgb() const;

    // ---- Vertical track scroll/zoom (shared TimelineViewState::trackScrollY/rowHeightScale) ----
    // The clip lane's layout: the one row-geometry model both surfaces share.
    TimelineRowLayout rowLayout() const { return clipLaneArea_.getRowLayout(); }
    int liftedTrackRowHeight() const;
    // One track's row height (TimelinePanelTrackHeight.cpp).
    void beginTrackHeightDrag(synth::TrackId track);
    void dragTrackHeight(synth::TrackId track, int deltaPx);
    void endTrackHeightDrag(synth::TrackId track);
    void relayoutTrackRows();
    int trackIndexOf(synth::TrackId track) const;
    synth::TrackId heightDragTrack_;
    int heightDragStartPx_ = 0;
    double heightDragScale_ = 1.0;
    double maxTrackScrollPx() const;
    void scrollTrackRows(double deltaPx);
    // Mouse-wheel notch easing (axis 0 = beats, 1 = px); see UI/Layout/ScrollTween.h.
    void scrollByWheel(int axis, double amount, bool eased);
    void applyWheelScroll(int axis, double amount);
    synth::ui::ScrollTweenRunner wheelTween_;
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

    // Declared in this order so the button unbinds from the pane, and the pane lets go of its content, before either
    // is destroyed.
    TimelineRoutingPane routingPane_;
    SidePane sidePane_;
    SidePaneToggleButton sidePaneButton_;

    // The panel's own copy of the undo manager (markers, the add-track menu's marker entry).
    AppUndoManager* undoManager_ = nullptr;

    // After trackHeaderList_, which holds its lane headers and so must outlive it.
    TimelineAutomationLanes automationLanes_{viewState_, trackHeaderList_};
    synth::LaneId selectedAutomationLane_;

    DrawShape drawShape_ = DrawShape::Free;
    DrawShapeStrip shapeStrip_{*this};
    juce::Rectangle<int> transportBarBounds_;
    juce::Rectangle<int> trackHeaderBounds_;
    juce::Rectangle<int> lanesBounds_;
    juce::Rectangle<int> gridLanesBounds_; // lanesBounds_ minus the ruler strip at its top

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimelinePanelComponent)
};

} // namespace synth::ui
