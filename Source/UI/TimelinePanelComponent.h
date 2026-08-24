#pragma once

#include "../Timeline/TimelineDoc.h"
#include "AutomationLaneEditor.h"
#include "ClipSelectionModel.h"
#include "EditTool.h"
#include "PianoRollComponent.h"
#include "TimelineClipLaneArea.h"
#include "TimelinePlayheadOverlay.h"
#include "TimelineRulerComponent.h"
#include "TimelineTrackHeaderComponent.h"
#include "TimelineTransportBar.h"
#include "TimelineViewState.h"
#include <array>
#include <functional>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

class AppUndoManager;  // Forward declaration (Source/AppUndoManager.h)
class ShortcutManager; // Forward declaration (Source/ShortcutManager.h)

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
// TimelinePlayheadOverlay.h and docs/layout.md §11).
//
// resized() lays out three regions:
//   - transport bar strip   (top,    Metrics::timelineTransportBarHeight) — houses the snap
//     selector and synth::ui::TimelineTransportBar (play/stop/record/loop + BPM/time-sig editors
//     + the bar:beat readout), left-aligned in the rest.
//   - track-header column   (left,   Metrics::timelineTrackHeaderWidth)
//   - lanes/ruler area      (remainder) — TimelineRulerComponent (Metrics::timelineRulerHeight)
//     docked at its top, a bar/beat grid painted directly by this component below it.
//
// A kResizeHandleHeight grab strip sits on top of the transport-bar strip's first rows (the
// transport controls are laid out below it): dragging it reports a desired panel HEIGHT through
// onResizeHeight/onResizeHeightCommitted. The panel still never sets its own bounds.
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
    , private juce::ChangeListener {
public:
    TimelinePanelComponent();
    ~TimelinePanelComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Wheel = horizontal scroll; Cmd+wheel (Ctrl on platforms without a Cmd key — mods.isCommandDown()
    // already abstracts this) = zoom around the cursor. Implemented once here (rather than
    // separately on the ruler) so the ruler and the lanes grid share identical behaviour — JUCE
    // bubbles an unhandled wheel event from the ruler child up to this override.
    //
    // Every branch reads the wheel through synth::ui::ScrollPolicy: the modifier-decided branches
    // (both zooms) take dominantWheelDelta() for MAGNITUDE and wheelGestureIsUpward() for
    // DIRECTION, so they survive macOS folding Shift+wheel into deltaX and stay "up zooms in"
    // regardless of the OS's natural-scrolling setting (XORed against zoomScrollInverted_ — see its
    // setter); the plain-scroll branches take scrollAmount(delta, scrollInverted_), which is the
    // juce::Viewport sign convention plus this panel's own (separate) inversion preference.
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    // Non-owning; may be null (tests, or before MainComponent finishes wiring). Forwarded to the
    // ruler and the playhead overlay — the two sub-components that talk to the transport directly
    // — and to the clip-lane area (it only reads the time signature, for Snap::Bar).
    void setTransport(synth::TransportService* transport);

    // Forwarded straight to the transport bar's own metronome toggle — see
    // TimelineTransportBar::setMetronome. Non-owning; may be null.
    void setMetronome(synth::Metronome* metronome);

    // THE low-rate transport poll, called from MainComponent's EXISTING 10 Hz timer (only while
    // this panel is visible). It adds no timer of its own.
    // Two jobs:
    //   - hand the snapshot to the playhead overlay, which owns its 30 Hz playing-only timer from
    //     the play/stop transitions it sees here (see TimelinePlayheadOverlay.h);
    //   - diff the small slice of transport state the RULER paints (time signature + loop trio) and
    //     repaint it only on a change. The position is deliberately NOT part of that diff: the
    //     playhead is the only thing that moves with it, and it repaints its own strip. Same gated
    //     idiom as the status bar's 5 Hz poll.
    void updateFromTransport(const synth::TransportService::PositionSnapshot& snapshot, double outputLatencySeconds);

    // Non-owning. Restores/persists the snap-selector choice under the "timelineSnap" key, same
    // pattern as AIChatComponent::setAccountService()'s non-owning setter. Also forwarded to the
    // transport bar, which restores/persists ITS OWN two keys ("timelineMetronomeEnabled",
    // "timelineCountInBars") — this panel has no other reason to know either setting exists, so it
    // is a pure forward, not a third copy of the restore/persist idiom. Also hands the roll its
    // PropertiesFile (pianoRoll_.setPropertiesFile — scale-panel visibility + user scales) and
    // runs reloadPianoRollAppearancePrefs() once, below.
    void setApplicationProperties(juce::ApplicationProperties* props);

    // Reads "pianoRollKeyLabels" ("all"/"c", default "all") and NoteColour.h's own
    // loadNoteColourOverrides, and pushes both into the roll. Called once from
    // setApplicationProperties, and left PUBLIC so a live settings change (the Preferences tab's
    // key-labels toggle, a note-colour edit) can re-push without a restart — MainComponent wires
    // that in a parallel task; this method itself does no listening of its own. A no-op with no
    // ApplicationProperties installed (or none passed to setApplicationProperties yet).
    void reloadPianoRollAppearancePrefs();

    // Non-owning; may be null (before setTimelineDoc is called, the panel is an inert shell with
    // an empty header column). The panel listens to the doc and
    // rebuilds/refreshes the track headers on every notification — that is the ONLY thing that
    // updates them: no timer, no polling. Also forwarded to the clip-lane area, which runs
    // the same "set doc, refresh once" seam (TimelineClipLaneArea::setTimelineDoc).
    void setTimelineDoc(synth::TimelineDoc* doc);
    synth::TimelineDoc* getTimelineDoc() const noexcept { return doc_; }

    // Non-owning. Handed to every track header (and driven by the "+ MIDI Track" button), so the
    // header column's whole conversation with the app goes through one seam. Must be set before
    // (or at the same time as) setTimelineDoc for the first build to be fully wired.
    void setTrackHeaderHost(TrackHeaderHost* host);

    // Forwarded to the clip-lane area — MainComponent's existing AppUndoManager is what
    // makes every clip drag/trim/split/duplicate/delete ONE undo step. Non-owning; may be null
    // (mutations then apply directly, off the undo stack — same degrade-gracefully contract every
    // other non-owning setter here has).
    void setUndoManager(AppUndoManager* undoManager);

    // The clip lane area and the selection model behind it. The panel owns the selection
    // model; the lane area only holds a reference to it (see TimelineClipLaneArea's ctor).
    synth::ui::ClipSelectionModel& getClipSelection() noexcept { return clipSelection_; }
    synth::ui::TimelineClipLaneArea& getClipLaneArea() noexcept { return clipLaneArea_; }
    // const overload: MainComponent::resolveEditSurface() is itself const and only needs
    // to compare addresses / walk the component tree, never to mutate either sub-component.
    const synth::ui::TimelineClipLaneArea& getClipLaneArea() const noexcept { return clipLaneArea_; }

    // ---- Edit tools (the Cubase-style tool row — see EditTool.h) ----
    //
    // ONE active tool for the whole timeline, owned here because the clip lanes and the piano roll
    // share a rect (only one is ever visible) and a tool row that changed meaning depending on
    // which editor happened to be showing would be a trap. Setting it pushes the tool into BOTH
    // editors and lights the matching strip button; the number keys (1/3/4/5/7/8) and the buttons
    // are the two ways a user reaches it.
    void setActiveTool(EditTool tool);
    EditTool getActiveTool() const noexcept { return activeTool_; }
    /** The strip button for a tool. Never null once the panel is constructed — the six buttons are
     *  built in the constructor, unconditionally (a headless build simply has no icon to draw in
     *  them). Exposed so a test can click one rather than synthesise a key press. */
    juce::DrawableButton* getToolButton(EditTool tool) const noexcept;

    // ---- Clip clipboard (Cmd+C/V/D on the TimelineClips surface) ----
    // This panel owns the clipboard because it already owns the selection it copies from — see
    // MainComponent::resolveEditSurface()/perform(), which delegate here exactly the way
    // GraphEditor owns its own module clipboard.
    //
    // Copies the CURRENTLY SELECTED clips — WHOLE clips: notes (each with its own muted flag),
    // name, length, muted flag and every audio field (assetRef, gainDb, the two fades,
    // sourceStartSeconds), with starts expressed RELATIVE to the earliest selected clip's start —
    // into an internal clipboard, replacing whatever was there. Returns false (clipboard left
    // untouched) when nothing is selected or there's no doc.
    bool copySelectedClips();
    // True once copySelectedClips() has captured at least one clip and nothing has cleared it
    // since — getCommandInfo's Paste-active gate for the TimelineClips surface.
    bool canPasteClips() const noexcept { return !clipClipboard_.empty(); }
    // Inserts every clipboard clip back onto ITS ORIGINAL TRACK, re-based so the EARLIEST clip
    // lands at the transport's CURRENT position (snapped via the shared view-state snap and the
    // transport's live time signature) and every other clip keeps its relative offset, with its
    // notes, name, mute state and audio fields restored.
    //
    // The track fallback is KIND-AWARE: the original track is used only if it still exists AND
    // still plays the clip's payload (an audio clip needs a TrackKind::Audio row, a MIDI clip a
    // Midi one — TimelineDoc::moveClipToTrack's rule); otherwise the doc's first track of the
    // required kind; otherwise that clip is skipped. Pasting an audio clip onto a MIDI row would
    // park an asset somewhere nothing will ever play it.
    //
    // Audio fields go back through setClipAsset/setClipGainDb/setClipFades rather than being
    // written into the struct, so the clipboard's assetRef passes the SAME bundle-relative
    // validation a loaded file's does — a clipboard is only as trustworthy as whatever filled it.
    // One recordTimelineChange for the whole paste; the pasted clips end up selected. Returns
    // false (no-op, clipboard untouched) when the clipboard is empty, there's no doc, or every
    // clip was skipped.
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
    // Cubase's "Repeat": `count` back-to-back copies of the selection BLOCK, the first starting
    // one block-length after the selection's own start, so the copies tile forward without
    // overlapping the source. The block length is the selection's span (max end - min start), not
    // each clip's own length — that is what keeps a multi-clip rhythm intact instead of
    // collapsing it. duplicateClip + moveClipToTrack per copy, ONE recordTimelineChange for the
    // whole repeat, and the final selection is every clip it created. Returns false when `count`
    // is < 1, there's no doc/selection, or nothing could be created.
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
     *  characters when the node doesn't resolve), or an "Add lane..." entry for a hosted
     *  plugin instance parameter that has none yet — `isAddEntry` distinguishes the two, `id` is
     *  only meaningful when it's false. In track order then lane order, existing lanes first, then
     *  add-lane entries — index i is menu id i + 1, the same convention
     *  TimelineTrackHeaderComponent::collectBindingOptions() uses. */
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

    // Escape closes the strip when it's open and idle (the editor's own keyPressed already
    // consumed it if there was tool-drag state to cancel — see AutomationLaneEditor's class
    // comment). Same panel-scoped idiom as every other timeline sub-component's Delete/Escape.
    bool keyPressed(const juce::KeyPress& key) override;

    // Trackpad pinch: plain = horizontal zoom, Shift = vertical (row height) zoom. The wheel
    // bindings live in mouseWheelMove; see its comment for the full Cubase-style table.
    void mouseMagnify(const juce::MouseEvent& e, float scaleFactor) override;

    // Pure geometry getters — later tasks and tests build on the same rects rather than
    // re-deriving the arithmetic in resized().
    juce::Rectangle<int> getTransportBarBounds() const noexcept { return transportBarBounds_; }
    // The WHOLE left column, including the "+ MIDI Track" strip at its top — the three regions
    // still tile the panel exactly (see TimelinePanelComponentTest.PanelRegionsTile).
    juce::Rectangle<int> getTrackHeaderBounds() const noexcept { return trackHeaderBounds_; }
    juce::Rectangle<int> getLanesBounds() const noexcept { return lanesBounds_; }

    // ---- Snap / zoom / scroll: the view-state verbs the shortcut layer drives ----
    //
    // Everything in this block is VIEW state: no TimelineDoc mutation, nothing on the undo stack.
    // Undoing a zoom is not a thing any DAW does, and putting one on the stack would bury the
    // user's last real edit under a pile of scrolls.

    /** Sets the grid division. Means the same thing as picking it from the snap combo — which is
     *  exactly what it IS now: the combo's onChange delegates here, so the combo, the shortcut
     *  layer and cycleSnapValue() below share one path to the view state, one persist and one set
     *  of repaints. Like the combo, it re-arms the master snap switch (see setSnapEnabled): asking
     *  for a division means "snap to THIS", and choosing Snap::Off is how you ask for no grid from
     *  here. Also feeds TimelineViewState::lastMusicalSnap, which is what cycleSnapValue's from-Off
     *  rule reads.
     *  @return true when the division actually changed (a re-pick of the current one still
     *          re-arms and re-persists, it just reports no change). */
    bool setSnapValue(TimelineViewState::Snap value);

    /** Steps the grid one division through the MUSICAL values only — Bar, 1, 1/2, 1/4, 1/8, 1/16,
     *  1/32, 1/64, 1/128 — with `direction` > 0 going FINER (toward 1/128) and < 0 going COARSER
     *  (toward Bar). Zero is a no-op.
     *
     *  Two rules, both chosen for how they feel under a held-down key rather than for symmetry:
     *
     *  - CLAMPED at both ends, never wrapping. Leaning on "finer" and parking at 1/128 is what the
     *    hand expects; wrapping silently back to Bar mid-flow moves every subsequent edit onto a
     *    64x coarser grid, and the user finds out from the result, not from the keypress.
     *  - Snap::Off is never a stop on the cycle — turning magnetism off stays the Q key's job. So
     *    cycling FROM Off (in either direction, one simple rule) enters at
     *    TimelineViewState::lastMusicalSnap, the last division the user actually chose, falling
     *    back to Snap::Bar if there somehow isn't one. "Either direction" is deliberate: from Off
     *    there is no current position for "one finer" to be relative to, so the only honest answer
     *    is "back where you were".
     *
     *  @return true when the division changed. */
    bool cycleSnapValue(int direction);

    /** Horizontal zoom by `factor` (> 1 in, < 1 out) around the CENTRE of the visible lanes, so a
     *  keyboard zoom keeps the music in front of you put. Runs through the same
     *  TimelineViewState::zoomAroundX + repaint path as Cmd+wheel and the trackpad pinch — one
     *  path, so a shortcut zoom and a wheel zoom can never drift apart in clamping or in what they
     *  repaint. A non-finite or non-positive factor is ignored. */
    void zoomTimelineHorizontal(double factor);

    /** Vertical (track row height) zoom by `factor`, anchored on the middle row of the visible
     *  lanes. Same zoomTrackRows path — including the header-column relayout and the scroll
     *  re-clamp — that Cmd+Shift+wheel and Shift+pinch use. */
    void zoomTimelineVertical(double factor);

    /** App-level scroll-direction preference, stacked on top of whatever the OS already did to the
     *  wheel deltas (see ScrollPolicy.h — JUCE hands us pre-flipped deltas, so this is a second,
     *  deliberate flip and not a re-application of the OS setting). Default false = "natural" =
     *  the juce::Viewport convention every other scrolling surface in the app already follows.
     *  Not persisted here: the owner (Preferences) decides whether a preference exists, the same
     *  way it owns the timeline's other opt-in behaviours. Forwarded to the piano roll (see
     *  setTransport/setUndoManager above for the same "one preference, every surface that scrolls"
     *  idiom) so a clip-lane scroll and a roll scroll never disagree about which way is "natural". */
    void setScrollInverted(bool inverted) noexcept;
    bool isScrollInverted() const noexcept { return scrollInverted_; }

    /** App-level ZOOM-direction preference for the Cmd/Cmd+Shift wheel-zoom gestures (horizontal
     *  and vertical) — independent of setScrollInverted above, which governs the PLAIN-scroll
     *  branches only. Default false = "up zooms in": mouseWheelMove derives the physical gesture
     *  direction via synth::ui::wheelGestureIsUpward (isReversed-aware, unlike a raw delta sign —
     *  see ScrollPolicy.h) and XORs it with this flag, so flipping the preference flips the sense
     *  of BOTH axes at once rather than requiring two separate settings. Forwarded to the piano
     *  roll for the same reason setScrollInverted is. Not persisted here — see setScrollInverted's
     *  comment. */
    void setZoomScrollInverted(bool inverted) noexcept;
    bool isZoomScrollInverted() const noexcept { return zoomScrollInverted_; }

    /** The user's bindings for this panel's OWN keys: the six tool digits, the snap toggle, the loop
     *  toggle and loop-the-selection. Non-owning and may stay null — with no manager installed
     *  keyPressed() falls back to the hardcoded Cubase defaults, which is what every headless test
     *  and every embedding without a settings store gets.
     *
     *  Resolution is strict once a manager IS installed, exactly as on PianoRollComponent: an action
     *  whose binding is unset or invalid (including an id this ShortcutManager has never heard of)
     *  has NO key rather than falling back to its default. Mixing the two would mean a binding the
     *  user deliberately cleared still fired on its factory key.
     *
     *  Escape is not resolved through here (it is a platform convention, not an app shortcut), and
     *  neither is anything the app dispatches as a command — Cmd+C/V/X/D, Space, and the grid
     *  commands all reach MainComponent, which owns that half.
     *
     *  Non-const (unlike PianoRollComponent's own copy of this pointer): the tool-strip/snap/
     *  follow buttons' tooltips are real juce::Button tooltips, which CACHE their text (unlike the
     *  roll's hand-drawn header, whose tooltip is resolved live on every hover query) — so this
     *  panel subscribes as a juce::ChangeListener on the installed manager to rebuild them whenever
     *  a binding changes, and that requires a non-const ShortcutManager* to add/removeChangeListener
     *  on. Unsubscribes from whichever manager was previously installed first, so re-installing (or
     *  clearing, with nullptr) never leaves a stale listener registered. */
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
    //
    // "Keep the playhead on screen while it plays" — a toggle next to snapToggleButton_ (same
    // external-state pattern: setClickingTogglesState(false), the shared bool is the truth, the
    // button only mirrors it). Persisted under "timelineFollowPlayhead", default OFF — an editor
    // that silently starts scrolling under a user who never asked for it is worse than one that
    // doesn't. Also forwards straight into pianoRoll_.setFollowPlayhead(enabled) — one flag, one
    // switch, for both the arrangement view and the roll — including from the
    // setApplicationProperties restore path.
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

    /** Adds a marker at the transport's current position, named "Marker N", coloured from the
     *  theme (see defaultMarkerColourArgb) — ONE recordTimelineChange when an undo manager is
     *  installed. Returns the new id, or an invalid one when there is no doc or the doc refused
     *  (kMaxMarkers). Public because it IS the "+ Track" menu's Add Marker action and the seam a
     *  test drives instead of the async menu. */
    synth::MarkerId addMarkerAtPlayhead();

    juce::TextButton& getAddTrackButton() noexcept { return addTrackButton_; }

    // ---- Resizable height (top-edge grab strip) ----
    //
    // The panel does NOT own its height: it reports the height the user is dragging for and the
    // OWNER (MainComponent) clamps it, lays the panel out and persists it. Everything here is the
    // grab strip plus the two callbacks it reports through.

    /** Height of the grab strip along the panel's top edge. It OVERLAPS the transport-bar strip
     *  instead of owning layout height of its own — the transport controls are laid out below it —
     *  so a resizable panel costs the rows beneath nothing. */
    static constexpr int kResizeHandleHeight = 5;

    /** Fired on every drag step with the height the user is asking for, measured from the panel's
     *  FIXED bottom edge. UNCLAMPED: the owner clamps it and re-runs its own layout, which is what
     *  makes the drag live. */
    std::function<void(int desiredHeight)> onResizeHeight;

    /** Fired once when a real drag ends, with the same desired height — the owner's cue to PERSIST.
     *  The per-drag-step callback above deliberately writes no settings, and a click that never
     *  dragged fires nothing at all. */
    std::function<void(int desiredHeight)> onResizeHeightCommitted;

    /** The grab strip itself. Exposed because a test has to drive mouse events through it — no OS
     *  mouse source exists headlessly (same reason the ruler's gestures are tested that way). */
    juce::Component& getResizeHandle() noexcept { return resizeHandle_; }
    /** Whether the strip is painting its brighter hairline — it repaints only when this CHANGES. */
    bool isResizeHandleHovered() const noexcept { return resizeHandle_.isHovered(); }

    /** Applies an "+ Track" menu choice. Exposed as the headless test seam for a menu that never
     *  runs in a test process — the same split TimelineTrackHeaderComponent's binding and context
     *  menus use (applyBindingMenuChoice / applyContextMenuChoice). Anything else is ignored. */
    void applyAddTrackMenuChoice(int menuId);
    juce::Viewport& getTrackHeaderViewport() noexcept { return trackHeaderViewport_; }
    int getTrackHeaderCount() const noexcept { return trackHeaderList_.headers.size(); }
    /** Header for the track at `index` in the doc's track order, or nullptr when out of range. */
    TimelineTrackHeaderComponent* getTrackHeaderAt(int index) const noexcept {
        return juce::isPositiveAndBelow(index, trackHeaderList_.headers.size())
                   ? trackHeaderList_.headers.getUnchecked(index)
                   : nullptr;
    }

private:
    // TimelineDoc::Listener — the single trigger for a header rebuild/refresh, AND the
    // clip-lane area's refresh (prunes the clip selection of anything the mutation removed).
    void timelineChanged(const synth::TimelineDoc& doc) override;

    void persistSnapChoice();
    // Rebuilds the header components when the set of track ids changed, and otherwise just
    // refreshes the existing ones in place (a mute toggle must not destroy and re-create rows).
    void syncTrackHeaders();
    void layoutTrackHeaders();

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
    // Called from the constructor and from lookAndFeelChanged() — a theme switch re-tints every
    // icon and can move the `toolActive` token, and both live in the LnF rather than in a
    // per-button copy.
    void applyToolStripTheme();
    // The one thing this panel needs to redo on a theme switch (every other colour it uses is read
    // at paint time through the same dynamic_cast).
    void lookAndFeelChanged() override;
    // The complement to lookAndFeelChanged() above: re-applies the tool-strip icons whenever this
    // component's ANCESTOR CHAIN changes, not just when its resolved LookAndFeel does. A themed
    // LookAndFeel change (setLookAndFeel/sendLookAndFeelChange) only reaches components that are
    // ALREADY attached as children at the moment it fires; the plugin editor calls
    // setLookAndFeel(&processor.getLookAndFeel()) on itself BEFORE it adds its MainComponent (and
    // this panel, several levels further down) as a child — see AgentSynthPluginEditor's
    // constructor — so that notification never reaches an unattached TimelinePanelComponent, and
    // its constructor-time applyToolStripTheme() call found no themed LookAndFeel on the ancestor
    // chain yet either. When the panel IS attached moments later (addAndMakeVisible), JUCE fires
    // parentHierarchyChanged() down the newly-added subtree — not lookAndFeelChanged() — so this is
    // the one hook guaranteed to run at that point. Idempotent and cheap either way.
    void parentHierarchyChanged() override;

    // The Viewport's content: a plain container whose height is (track count * row height).
    struct TrackHeaderList : juce::Component {
        juce::OwnedArray<TimelineTrackHeaderComponent> headers;
    };

    // The top-edge grab strip (see kResizeHandleHeight). Added LAST in the constructor so it wins
    // the hit test over the transport bar it overlaps, and carries the UpDownResizeCursor.
    //
    // The drag is measured in SCREEN coordinates against the panel's bottom edge, not as a delta:
    // the owner moves the panel's top edge under the cursor on every callback, so a
    // component-relative delta would chase itself. Both callbacks report the panel's DESIRED
    // height; clamping belongs to the owner.
    class ResizeHandle : public juce::Component {
    public:
        explicit ResizeHandle(TimelinePanelComponent& owner);
        void paint(juce::Graphics& g) override;
        void mouseEnter(const juce::MouseEvent& e) override;
        void mouseExit(const juce::MouseEvent& e) override;
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseDrag(const juce::MouseEvent& e) override;
        void mouseUp(const juce::MouseEvent& e) override;
        bool isHovered() const noexcept { return hovered_; }

    private:
        // Highlighted while hovered OR dragging, so the hairline doesn't dim mid-drag when the
        // pointer leaves the strip (it does, the moment the panel grows under it).
        bool isHighlighted() const noexcept { return hovered_ || dragging_; }
        // The desired height for `e`, from the panel's fixed bottom edge and the grab offset.
        int desiredHeightFor(const juce::MouseEvent& e) const;

        TimelinePanelComponent& owner_;
        bool hovered_ = false;
        bool dragging_ = false;
        // True once the gesture actually moved: a click that never dragged commits nothing, so a
        // stray click on the strip never writes the height back to settings.
        bool moved_ = false;
        // Where inside the panel's top edge the drag was grabbed — keeps that pixel under the
        // cursor for the whole gesture.
        int grabOffsetY_ = 0;
        int lastDesiredHeight_ = 0;
    };
    ResizeHandle resizeHandle_{*this};

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
    // The transport-bar twin of the piano roll's "Q": toggles TimelineViewState::snapEnabled from
    // the panel chrome, so the switch is discoverable without opening a clip. Toggle STATE mirrors
    // the shared flag via setSnapEnabled() — the button never owns it.
    juce::TextButton snapToggleButton_{"Q"};
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

    // The button opens a MIDI/Audio menu rather than adding a MIDI track outright.
    juce::TextButton addTrackButton_{"+ Track"};

    void showAddTrackMenu();
    // The theme's colour for a NEW marker (see addMarkerAtPlayhead). Falls back to the model's own
    // default with no themed LookAndFeel installed, like every other paint-time resolve here.
    juce::uint32 defaultMarkerColourArgb() const;

    // ---- Vertical track scroll/zoom (shared TimelineViewState::trackScrollY/rowHeightScale) ----
    // The themed row height with the shared vertical-zoom factor applied — the SAME value
    // TimelineClipLaneArea::getRowHeight computes, duplicated only because the two components
    // resolve their LookAndFeel independently.
    int currentRowHeight() const;
    double maxTrackScrollPx() const;
    void scrollTrackRows(double deltaPx);
    void zoomTrackRows(double factor, double anchorLaneY);
    // The ONE horizontal-zoom writer: Cmd+wheel, trackpad pinch and zoomTimelineHorizontal() all
    // land here, so the clamp behaviour and the repaint set are shared rather than copied three
    // times. anchorX is in the ruler's coordinate space (== TimelineViewState's x origin).
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

    // Non-owning, may stay null (see setShortcutManager). Non-const so this panel can
    // add/removeChangeListener on it (rebinding itself still belongs to Settings — this pointer is
    // never used to mutate a binding, only to subscribe to changes and read the current one).
    //
    // LIFETIME REQUIREMENT: the installed ShortcutManager must outlive this component, exactly
    // like every other "non-owning, may stay null" pointer here — the destructor's
    // removeChangeListener call dereferences it. MainComponent.h currently declares `timelinePanel`
    // BEFORE `shortcutManager`, so MEMBER teardown destroys shortcutManager FIRST and would leave
    // this pointer dangling by the time ~TimelinePanelComponent() runs; that call site needs an
    // explicit `timelinePanel.setShortcutManager(nullptr);` in ~MainComponent(), ahead of the
    // member cascade — the same pattern already used there for themeManager/appProperties — before
    // this feature is safe to ship. Not added here: MainComponent.cpp is outside this component's
    // own file boundary.
    ShortcutManager* shortcuts_ = nullptr;
    // juce::ChangeListener — rebuilds the tool-strip/snap/follow tooltips on every bindings change.
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    // Rebuilds every dynamic shortcut-hint tooltip this panel owns (see synth::shortcutHintFor):
    // the six tool-strip buttons, the snap toggle, and the follow-playhead toggle. Called from the
    // constructor (after those buttons exist), setShortcutManager (both install and clear), and
    // changeListenerCallback.
    void refreshShortcutTooltips();
    // True when `key` is what the user has bound to `actionId`. With no manager installed this is
    // `key == fallback`; with one installed the fallback is not consulted at all. The same three
    // lines PianoRollComponent::matchesAction runs — deliberately duplicated rather than shared,
    // because factoring it out would mean a header both surfaces include just to hold a two-branch
    // comparison, and the contract (not the code) is the thing that has to stay identical.
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
    TrackHeaderList trackHeaderList_;

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
