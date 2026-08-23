#pragma once

#include "../Timeline/MusicalScale.h"
#include "../Timeline/TimelineDoc.h"
#include "EditTool.h"
#include "NoteColour.h"
#include "NoteSelectionModel.h"
#include "ScaleAssistPanel.h"
#include "TimelinePlayheadOverlay.h"
#include "TimelineViewState.h"
#include "UIAnimation.h"
#include <array>
#include <cmath>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <optional>
#include <utility>
#include <vector>

class AppUndoManager;  // Forward declaration (Source/AppUndoManager.h)
class ShortcutManager; // Forward declaration (Source/ShortcutManager.h)

namespace synth {
class TransportService; // Forward declaration (Source/Transport/TransportService.h)
}

// PianoRollComponent — the per-clip note editor shown INSIDE the timeline panel's lanes region (no
// separate window). TimelinePanelComponent swaps this in for synth::ui::TimelineClipLaneArea (same
// rect, same z-order slot, below the playhead overlay) when a clip is double-clicked, and swaps
// back on the back button, Escape-with-nothing-selected, or the edited clip disappearing.
//
// Non-owning refs/pointers (TimelineViewState&, TimelineDoc* / AppUndoManager* / TransportService*)
// may be null and degrade to "read but don't mutate". Every edit previews locally and commits to
// the doc exactly once on mouse-up via AppUndoManager::recordTimelineChange, so a multi-note
// move/resize/velocity-scrub/delete is ONE undo step however many notes it touches.
//
// Coordinate system: the roll owns its OWN horizontal mapping (beatToX/xToBeat below) — its own
// zoom and its own scroll origin, independent of the panel-wide TimelineViewState. x ==
// leftGutterWidth() is the first visible beat, so the keys column (and, while it is open, the
// scale-assist panel to its LEFT) is a real GUTTER and the first bar of a clip is reachable rather
// than hidden under an opaque strip. leftGutterWidth() is kKeysColumnWidth alone with the panel
// closed, kScalePanelWidth + kKeysColumnWidth while it is open — every place that used to hardcode
// kKeysColumnWidth as "the grid's left offset" now goes through it (see the Scale Assist section
// below), so opening the panel shifts the grid, the hit-testing and the clip-framing maths
// together rather than drifting apart. The shared TimelineViewState is still consulted for ONE
// thing: the snap division (snapBeat/divisionBeats), so the roll's grid, its snapped edits and the
// panel's snap selector never disagree.
//
// Because that mapping differs from the panel's, the panel-wide TimelinePlayheadOverlay would draw
// the playhead at the wrong x inside this rect. The roll therefore implements
// TimelinePlayheadOverlay::LocalPlayheadClient: while it is open the overlay stops drawing and
// repainting inside the roll's region and pushes the DRAWN beat here instead (setPlayheadBeat),
// and the roll draws the line at its own x under the same strip-confined repaint discipline
// (requestRepaintStrip — zero repaints while the position is unchanged, one strip while playing).
// No timer is added here: the overlay's single playing-only timer still drives everything.
//
// Notes are clip-relative in the doc (MidiNote::startBeat); every doc read/write here converts to
// absolute beats via clip->startBeat and back.
//
// Vertical row mapping: yForPitch/pitchForY do NOT map pitch directly to y. They map through
// visiblePitches_ — a sorted, ascending list of every pitch that currently gets a ROW (all 128 when
// no scale filtering is active). Row distance between two pitches is the distance between their
// INDICES in that list, not the semitone distance between them, which is what lets pitch-visibility
// mode collapse the out-of-scale gaps into zero-height rows instead of just recolouring them.
// yForPitch of a pitch that is not itself visible (an edge case — a note landing between visible
// rows) falls back to the nearest visible row's y. visiblePitches_ is rebuilt (see
// rebuildVisiblePitches) whenever the scale context changes, the roll opens a clip, or a doc
// mutation could have added/removed the note that was the only thing keeping an out-of-scale pitch
// visible; firstVisiblePitch_ is re-clamped to the nearest surviving visible pitch on every rebuild,
// so it is always a member of visiblePitches_. Vertical wheel-scroll and vertical zoom walk
// visiblePitches_ by INDEX for the same reason — a scroll gesture over collapsed rows must move a
// consistent number of ROWS, not skip past them at whatever their semitone spacing happens to be.
//
// The panel's edit-tool strip pushes the active tool in (setActiveTool). Select is the whole
// gesture table above; Split / Glue / Erase / Mute / Draw replace it with single-click actions and
// disable move, resize, velocity scrub and marquee entirely — see setActiveTool for why. The note
// CLIPBOARD (copy/cut/paste/duplicate/repeat) lives here too rather than in the panel, because a
// copied block is anchored on its own earliest note and is therefore paste-able into any clip: the
// roll keeps it across openClip so "copy in one clip, paste in another" works.
//
// See docs/layout.md §16 (TL5-8) for the gesture table.
namespace synth::ui {

class PianoRollComponent
    : public juce::Component
    , public juce::TooltipClient
    , public TimelinePlayheadOverlay::LocalPlayheadClient
    , private juce::Timer {
public:
    // Piano-roll-only constants; not shared with Theme::Metrics.
    static constexpr int kKeysColumnWidth = 44;
    // The scale-assist panel's fixed width when open, carved from the LEFT of the keys column —
    // see leftGutterWidth() and the class comment.
    static constexpr int kScalePanelWidth = 170;
    static constexpr int kHeaderHeight = 20;
    // Default vertical zoom, and its clamps (Cmd+Shift+wheel scales it — see mouseWheelMove).
    static constexpr double kPixelsPerSemitone = 10.0;
    static constexpr double kMinPixelsPerSemitone = 4.0;
    static constexpr double kMaxPixelsPerSemitone = 40.0;
    // Floor under a new/resized note's length when Snap is Off: 1/16 of a beat, matching
    // TransportService::kMinLoopLengthBeats (finer than the snap selector's finest division —
    // Snap Off is exactly the mode that wants sub-grid freedom).
    // With Snap ON a NEW note is exactly one snap division long (1 bar quantise -> a 1-bar note).
    static constexpr double kMinNoteLengthBeats = 0.0625;
    // Resize handle zone at a note's right edge, in px (no left-edge resize in v1).
    static constexpr int kResizeZonePx = 5;
    // Local playhead line: same width/strip margin the panel overlay uses, so the line reads as one
    // stroke across the ruler and the roll.
    static constexpr float kPlayheadLineWidth = TimelinePlayheadOverlay::kLineWidth;
    static constexpr int kPlayheadStripHalfWidth = TimelinePlayheadOverlay::kStripHalfWidth;
    // Momentary "Q was pressed" highlight, in ms. Driven by a ONE-SHOT juce::Timer (it stops itself
    // in the first callback) and repainting only the button's own rect — bounded, never a loop.
    static constexpr int kQuantiseFlashMs = 120;

    // getTooltipFor() builds the Q/Scale header buttons' tooltip text dynamically — see
    // quantiseTooltipText()/scaleTooltipText() below — rather than a static string with a
    // hardcoded key name that would go stale the moment the user rebinds
    // "timelineSnapToggle"/"pianoRollToggleScalePanel" (see synth::shortcutHintFor).

    explicit PianoRollComponent(TimelineViewState& viewState);
    // Not '= default': the scale-panel slide's AnimationDriver callbacks capture 'this', so any
    // in-flight animation must be stopped before the object goes away (mirrors
    // ModuleLibraryComponent's own destructor for the same reason).
    ~PianoRollComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    // Cmd+wheel        -> horizontal zoom around the beat under the cursor
    // Cmd+Shift+wheel  -> vertical zoom (pixels per semitone) around the pitch under the cursor
    // Shift+wheel / trackpad deltaX -> horizontal scroll
    // plain wheel      -> vertical (pitch) scroll
    // Nothing bubbles to the panel: the roll's zoom/scroll are its own, so the shared
    // TimelineViewState must not move when the wheel lands here.
    //
    // EVERY branch reads its amount through synth::ui::ScrollPolicy (ScrollPolicy.h) rather than a
    // raw delta member, for the two reasons spelled out there: macOS folds Shift+wheel into
    // `deltaX`, so the modifier-decided branches (both zooms) must take the DOMINANT axis or go
    // silently dead under Shift; and the plain-scroll branches route their sign through
    // scrollAmount() so "natural" here means exactly what it means in a juce::Viewport.
    //
    // The two zoom branches are a DIFFERENT preference from the scroll branches: direction there
    // comes from synth::ui::wheelGestureIsUpward (the PHYSICAL gesture, recovered from isReversed
    // XOR the delta's sign — see ScrollPolicy.h), not from the delta's raw sign, so "wheel up zooms
    // in" is the same finger motion regardless of the OS's natural-scrolling setting.
    // zoomScrollInverted_ (setZoomScrollInverted) flips that outcome; it is independent of
    // scrollInverted_, which only ever governs the plain-scroll branches below.
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    // Trackpad pinch: plain = horizontal zoom around the pinch point, Shift = vertical zoom —
    // the same pair TimelinePanelComponent::mouseMagnify binds for the lanes.
    void mouseMagnify(const juce::MouseEvent& e, float scaleFactor) override;
    // Hover-only work: the Split tool's cut-position preview and the Select tool's resize-zone
    // cursor. BOTH are gated on a state change (the snapped cut beat / the hovered note, and the
    // "is the pointer in a resize zone" boolean), so a mouse moving inside one note at one snap
    // division costs zero repaints and zero cursor churn — see the repaint invariant in CLAUDE.md.
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    // Theme switch: the tool cursors are rendered FROM the themed icons, so the cache is dropped
    // and the active tool's cursor re-applied here rather than rebuilt per mouse move.
    void lookAndFeelChanged() override;

    // Panel-scoped Delete/Escape. Returns false (key falls through) when there is nothing to act
    // on, the same TimelineClipLaneArea contract.
    bool keyPressed(const juce::KeyPress& key) override;

    // juce::TooltipClient — the "Q" button's tooltip (the header's buttons are drawn shapes, not
    // child juce::Buttons, so the tooltip is resolved by position).
    juce::String getTooltip() override;
    juce::String getTooltipFor(juce::Point<int> pos) const;

    // Non-owning; may be null. Same degrade-gracefully contract as every other timeline
    // sub-component's setter.
    void setTimelineDoc(synth::TimelineDoc* doc) noexcept { doc_ = doc; }
    synth::TimelineDoc* getTimelineDoc() const noexcept { return doc_; }
    void setUndoManager(AppUndoManager* undoManager) noexcept { undoManager_ = undoManager; }
    AppUndoManager* getUndoManager() const noexcept { return undoManager_; }
    // Only consulted for Snap::Bar's beatsPerBar, same reasoning as
    // TimelineClipLaneArea::setTransport.
    void setTransport(synth::TransportService* transport) noexcept { transport_ = transport; }

    /** The user's keyboard bindings for this surface's OWN keys (nudge, transpose, note navigation,
     *  quantise, snap toggle, scale-panel toggle). Non-owning and may stay null — with no manager installed
     * keyPressed() uses the hardcoded defaults below, which is what every headless test and every embedding that has no
     * settings store gets.
     *
     *  Resolution is strict when a manager IS installed: an action whose binding is unset or invalid
     *  (including an id this ShortcutManager has never heard of) has NO key, rather than quietly
     *  falling back to its default. Mixing the two would mean a user who deliberately cleared a
     *  binding still had the factory key working, which is exactly the bug rebinding exists to fix.
     *
     *  Escape and Delete/Backspace are NOT resolved through here — they are platform conventions
     *  (cancel, delete the selection), not app shortcuts, and every surface in the app answers them
     *  identically. The tool DIGITS are not here either: they belong to the panel (see
     *  setActiveTool). */
    void setShortcutManager(const ShortcutManager* manager) noexcept { shortcuts_ = manager; }
    const ShortcutManager* getShortcutManager() const noexcept { return shortcuts_; }

    /** The app-level "invert my scroll wheel" preference, stacked ON TOP of the OS setting (which
     *  JUCE has already folded into the deltas — see ScrollPolicy.h). false, the default, is
     *  NATURAL: identical to what a juce::Viewport does with the same gesture, on both axes. */
    void setScrollInverted(bool inverted) noexcept { scrollInverted_ = inverted; }
    bool isScrollInverted() const noexcept { return scrollInverted_; }

    /** The app-level "invert my zoom wheel direction" preference — a SEPARATE flag from
     *  scrollInverted_ above, because zoom and plain scroll answer to different conventions: scroll
     *  follows the OS's natural-scrolling setting (already folded into the deltas), while zoom
     *  follows the PHYSICAL gesture (synth::ui::wheelGestureIsUpward) regardless of it. false, the
     *  default, means wheel UP zooms IN — for both the Cmd+wheel (horizontal) and Cmd+Shift+wheel
     *  (vertical) branches, which always agree with each other. */
    void setZoomScrollInverted(bool inverted) noexcept { zoomScrollInverted_ = inverted; }
    bool isZoomScrollInverted() const noexcept { return zoomScrollInverted_; }

    /** Zoom the roll's own horizontal mapping by `factor` (> 1 in, < 1 out) around the CENTRE of the
     *  visible grid, using the same anchored math the Cmd+wheel branch uses — so a menu/keyboard
     *  zoom and a wheel zoom land in the same place, just with a different anchor. Clamped to
     *  TimelineViewState's pixels-per-beat bounds. View-only: no doc mutation, no undo step. A
     *  non-finite or non-positive factor is ignored. */
    void zoomHorizontal(double factor);

    /** The vertical equivalent: scales pixelsPerSemitone_ (clamped to [kMinPixelsPerSemitone,
     *  kMaxPixelsPerSemitone]) while keeping the pitch at the grid's vertical centre put. Same
     *  view-only, no-undo contract. */
    void zoomVertical(double factor);

    // ---- Keys column labels ----

    // AllNotes labels every key row (subject to the row-height floor below); OctavesOnly labels
    // only the C rows — the pre-scale-feature behaviour, kept as a mode rather than removed because
    // a dense keyboard reading is not always what the user wants. See keyLabelFor for the exact
    // per-row decision, including the shared "too short a row for per-key text" floor.
    enum class KeyLabelMode { AllNotes, OctavesOnly };

    void setKeyLabelMode(KeyLabelMode mode) noexcept { keyLabelMode_ = mode; }
    KeyLabelMode getKeyLabelMode() const noexcept { return keyLabelMode_; }

    // Pure: no component, no theme, no LookAndFeel — what paintKeysColumn calls per row and what a
    // test asserts on directly. Empty string means "draw no label at all". Below a 9px row height a
    // per-key label becomes unreadable noise at any zoom worth calling "zoomed out", so BOTH modes
    // fall back to C-only there; at or above it, AllNotes names every row ("C4", "C#4", ...) and
    // OctavesOnly keeps naming only the Cs. Octave numbering matches the rest of the roll:
    // pitch / 12 - 1.
    static juce::String keyLabelFor(int pitch, KeyLabelMode mode, int rowHeightPx);

    // The colour paintKeysColumn draws a row's label in, given THAT row's own key fill (pianoKeyWhite
    // or pianoKeyBlack) — never a single fill shared by both key colours. Factored out of the paint
    // loop's `.contrasting()` call so a test can assert on it directly: the earlier bug was a sharp
    // (black-key) label right-aligned across the FULL column width, which is wider than the black
    // key's own (narrower, flush-left) rect, so a colour chosen to contrast against BLACK ended up
    // drawn, for the portion past the black key's right edge, over the WHITE fill showing through —
    // light-on-light, nearly invisible. Confining the black-key label's drawn rect to the black
    // key's own area (see paintKeysColumn) is what makes every pixel of the label sit over the SAME
    // fill this colour was chosen against.
    static juce::Colour labelColourFor(juce::Colour keyFill) noexcept { return keyFill.contrasting(0.7f); }

    // ---- Note colouring & scale context ----

    // The per-pitch-class colour overrides (NoteColour.h) a note's fill is resolved through. See
    // synth::ui::resolveNoteColour for the precedence (out-of-scale beats an override beats the
    // theme default).
    void setNoteColourOverrides(const synth::ui::NoteColourOverrides& overrides) {
        overrides_ = overrides;
        repaint();
    }
    const synth::ui::NoteColourOverrides& getNoteColourOverrides() const noexcept { return overrides_; }

    // The scale the grid checks notes/rows against. `isInScale` empty (the default) means no scale
    // at all: nothing is ever out-of-scale and no row is ever hidden, regardless of
    // `pitchVisibilityOn`. With a scale installed, `pitchVisibilityOn` decides whether out-of-scale
    // ROWS collapse out of the grid entirely (true) or merely get paintNote's out-of-scale colour
    // while every row still gets drawn (false) — a pitch that has a note in the OPEN clip is always
    // kept visible even when it is out of scale, so an existing note can never become unreachable by
    // turning this on. Rebuilds visiblePitches_ and re-clamps firstVisiblePitch_ immediately.
    void setScaleContext(std::function<bool(int)> isInScale, bool pitchVisibilityOn);

    // ---- Scale assist panel ----

    // Non-owning; may stay null (tests pass null). Hands the pointer straight to the panel (its
    // own user-scale persistence) and restores THIS roll's own "was the panel open" flag under
    // "pianoRollScalePanelVisible" (default false). Per-clip scale memory is deliberately NOT
    // persisted here — see the class comment on clipScaleMemory_.
    void setPropertiesFile(juce::PropertiesFile* props);

    synth::ui::ScaleAssistPanel& getScaleAssistPanel() noexcept { return scalePanel_; }
    const synth::ui::ScaleAssistPanel& getScaleAssistPanel() const noexcept { return scalePanel_; }
    // The public toggle verb the header button, the Ctrl+S surface key and the tests all share —
    // one entry point, so the three can never disagree about what "toggle" means (animation
    // included; see setScalePanelVisible below for the animate/snap split).
    void toggleScalePanel();
    juce::Rectangle<int> getScaleButtonBounds() const noexcept { return scaleButtonBounds_; }

    /** Moves the SELECTED notes (or, with nothing selected, every note in the open clip) to their
     *  nearest in-scale pitch via MusicalScale::snapPitch. ONE undo step for the whole batch, a
     *  no-op pushes nothing, and the selection is left exactly as it was — this only ever moves
     *  pitches, never touches which notes are selected. */
    void quantisePitchesToScale(const synth::MusicalScale& scale);

    /** Replaces the ENTIRE contents of the open clip with a fresh random pattern: `scale` may be
     *  null (every pitch in [minPitch, maxPitch] is a candidate); the grid step is the snap
     *  selector's RAW division (ignoring the on/off toggle), falling back to a sixteenth when that
     *  division is Off/0. ONE undo step removes every existing note and adds the generated ones —
     *  "generate" means "replace", and undo restores the old contents in one step. The generated
     *  notes become the selection. `rng` is caller-owned, which is what makes this
     *  deterministically testable (the panel's Generate button hands it a fresh, default-seeded
     *  juce::Random). */
    void generateRandomNotesIntoClip(const synth::MusicalScale* scale, int minPitch, int maxPitch, juce::Random& rng);

    // ---- Edit tools (Cubase-style; see EditTool.h) ----

    /** The active tool. TimelinePanelComponent owns the choice (one strip drives whichever editor
     *  is currently swapped into the lane rect) and pushes it here; the roll never switches tool by
     *  itself. In particular keyPressed() deliberately does NOT consume the tool digits — they
     *  bubble to the panel, which owns that binding, so the two can never disagree about which
     *  tool is active.
     *
     *  Select keeps the whole pre-existing gesture table (click-select, drag-move, right-edge
     *  resize, Cmd velocity scrub, drag-from-empty marquee, double-click create/delete). The other five
     *  tools REPLACE it: they act on a single click and none of them starts a move/resize/scrub/
     *  marquee, so a mis-aimed drag with the Erase tool can never silently move a note instead.
     *  Switching tool also abandons any gesture already in flight — it belonged to the old tool. */
    void setActiveTool(EditTool tool);
    EditTool getActiveTool() const noexcept { return activeTool_; }

    // ---- Note clipboard ----

    /** One copied note, stored RELATIVE to the earliest note in the copied block rather than in
     *  absolute (or even clip-relative) beats. That is what lets a copy survive being pasted into a
     *  different clip at a different position — the block keeps its internal shape and only its
     *  anchor moves. Every field a note carries is captured, `muted` included: a muted note pastes
     *  back muted, the same way a split or a duplicate carries the flag (see MidiNote::muted). */
    struct ClipboardNote {
        double offsetFromEarliest = 0.0;
        double lengthBeats = 1.0;
        int pitch = 60;
        int velocity = 100;
        int channel = 1;
        bool muted = false;
    };

    /** Captures the current selection into the clipboard. The clipboard is a MEMBER of the roll,
     *  not of a gesture: it deliberately outlives openClip(), so "copy here, open another clip,
     *  paste there" works — which is the whole point of anchoring entries on the earliest note.
     *  @return false (clipboard untouched) when nothing is selected. */
    bool copySelectedNotes();

    /** True when pasteNotesAtPlayhead() would have somewhere to put something: a non-empty
     *  clipboard AND an open clip. The command wiring uses this for its menu-item enablement. */
    bool canPasteNotes() const noexcept;

    /** Pastes the clipboard block into the OPEN clip, anchored at the playhead: the anchor is the
     *  snapped, clip-relative playhead position when that lands inside [0, clip length), and 0.0
     *  otherwise (a playhead parked outside the edited clip still pastes something visible rather
     *  than nothing at all). Notes landing at/after the clip's end are skipped and a note's length
     *  is clamped to the clip's end; see buildPastedNotes for the exact rules. One undo step
     *  however many notes land, and the pasted notes become the selection.
     *  @return false when nothing could be placed. */
    bool pasteNotesAtPlayhead();

    /** Copies the selection to immediately after its own span (span = max end - min start), same
     *  pitches, one undo step, and selects the copies. Does NOT touch the clipboard — duplicating
     *  is not a copy, and stomping a clipboard the user filled deliberately would be a surprise. */
    bool duplicateSelectedNotes();

    /** Copy + delete as ONE undo step (the clipboard is filled first, so a cut is always
     *  pasteable). @return false when nothing is selected. */
    bool cutSelectedNotes();

    /** Selects every note in the open clip. @return false when there is nothing to select. */
    bool selectAllNotes();

    /** `count` back-to-back copies of the selection block, each one span further along, clipped at
     *  the clip's end: placement STOPS at the first block that falls entirely outside the clip
     *  rather than piling every remaining copy onto the last beat. One undo step for the whole
     *  repeat; every created note ends up selected. */
    bool repeatSelectedNotes(int count);

    bool hasNoteSelection() const noexcept { return !selection_.isEmpty(); }

    // ---- Entry/exit (panel API surface: openPianoRoll/closePianoRoll/isPianoRollOpen forward
    // straight to these three) ----

    // Opens the roll for `id`. A no-op (stays/becomes closed) if doc_ is null or `id` does not
    // resolve to a live clip. Clears the note selection, resets any in-flight gesture, centres the
    // pitch scroll on the clip's median note pitch (60 for an empty clip), and frames the clip
    // horizontally: its start sits at the keys column's right edge, zoomed so the whole clip fits
    // (subject to the pixels-per-beat clamps).
    void openClip(synth::ClipId id);
    void closeRoll();
    bool isOpen() const noexcept { return clipId_.isValid(); }
    synth::ClipId getClipId() const noexcept { return clipId_; }

    // Fired when the roll asks to be closed: the back button, Escape with nothing selected, or
    // refreshFromDoc() noticing the edited clip is gone. The owner (TimelinePanelComponent) wires
    // this to its own closePianoRoll(), which also re-shows the clip-lane area. Not fired by a
    // direct closeRoll() call (that IS the close — no need to ask again).
    std::function<void()> onCloseRequested;

    // Fired after toggleSnap() flipped the shared TimelineViewState::snapEnabled — the owner
    // persists the choice and repaints whatever else paints the grid (the lanes behind us).
    std::function<void()> onSnapToggled;

    // Fired whenever the roll's OWN horizontal mapping changed (openClip's framing, wheel
    // zoom/scroll). While the roll is open the panel's ruler mirrors that mapping (see
    // TimelineRulerComponent::setMappingOverride), so it has to repaint on this.
    std::function<void()> onHorizontalViewChanged;

    // The roll's own zoom/scroll — what the panel hands the ruler as its mapping override while
    // the roll is open, so the bar numbers above show the clip's REAL timeline position.
    const TimelineViewState& getRollViewState() const noexcept { return rollView_; }

    // kKeysColumnWidth plus the scale-assist panel's CURRENT animated width — 0 while fully closed,
    // kScalePanelWidth at rest open, anything between while the slide (see "Scale assist panel
    // slide animation" below) is in flight — the ONE seam beatToX/xToBeat/gridRegion and every
    // hit-test below route the grid's left offset through (see the class comment). Public because
    // TimelineRulerComponent's mapping override reads this SAME offset (via
    // TimelinePanelComponent::openPianoRoll and its onHorizontalViewChanged re-issue), so the
    // ruler's ticks/scrub hit-testing track the scale panel's width — including mid-slide — instead
    // of drifting from it.
    int leftGutterWidth() const noexcept {
        return (int)std::llround((double)scalePanelOpenProgress_ * (double)kScalePanelWidth) + kKeysColumnWidth;
    }

    // Flips the shared snap switch (TimelineViewState::snapEnabled), flashes the Q button and
    // fires onSnapToggled. The Q button and the panel-wide Q key both land here.
    void toggleSnap();

    // ---- Follow playhead ----

    /** When on, setPlayheadBeat page-flips the roll's OWN horizontal view the instant the drawn
     *  beat would land outside gridRegion() — the roll chases the transport instead of leaving it
     *  to scroll off the edge. Gated (see setPlayheadBeat) on no drag being in flight and the
     *  edge-auto-scroll timer being idle, so a Move/Resize/Marquee/DrawNew gesture that is already
     *  steering the view (or deliberately not) is never yanked out from under the user. Off by
     *  default: existing embeddings/tests must see no new view movement until this is turned on. */
    void setFollowPlayhead(bool follow) noexcept { followPlayhead_ = follow; }
    bool isFollowPlayhead() const noexcept { return followPlayhead_; }

    // THE refresh seam, called by the panel's TimelineDoc::Listener on every doc mutation (mirrors
    // TimelineClipLaneArea::refreshFromDoc): prunes the note selection of anything the mutation
    // removed. If the EDITED CLIP itself is gone, closes the roll and fires onCloseRequested() so
    // the owner swaps back to the clip lanes. A no-op while closed.
    void refreshFromDoc();

    // ---- The roll's OWN horizontal mapping ----

    // Absolute beat <-> this component's x. x == leftGutterWidth() is the first visible beat: the
    // keys column (plus the scale panel, while open) is a gutter, NOT an overlay painted over the
    // grid's leftmost pixels.
    double beatToX(double absBeat) const noexcept;
    double xToBeat(double x) const noexcept;

    double getPixelsPerBeat() const noexcept { return rollView_.pixelsPerBeat; }
    double getFirstVisibleBeat() const noexcept { return rollView_.firstVisibleBeat; }
    double getPixelsPerSemitone() const noexcept { return pixelsPerSemitone_; }

    // Pins the horizontal mapping directly (pixelsPerBeat clamped to TimelineViewState's zoom
    // bounds, firstVisibleBeat clamped >= 0). openClip() derives both from the clip; this is what
    // a test (or a future "restore my zoom" path) uses to set them explicitly.
    void setHorizontalView(double pixelsPerBeat, double firstVisibleBeat);
    void setPixelsPerSemitone(double pixelsPerSemitone);

    // ---- TimelinePlayheadOverlay::LocalPlayheadClient ----
    // While open, the panel overlay hands the drawn beat here instead of drawing inside this rect
    // (its shared mapping would put the line at the wrong x). Same discipline as the overlay's own
    // refreshLine: a beat whose rounded x did not move requests NOTHING.
    bool isLocalPlayheadActive() const override { return isOpen(); }
    void setPlayheadBeat(double absoluteBeat) override;

    // The last absolute beat the overlay handed over. Exposed because paste targets it (see
    // pasteNotesAtPlayhead) — the roll has no transport of its own to ask.
    double getPlayheadBeat() const noexcept { return playheadBeat_; }

    // The x the local playhead line is drawn at right now, in this component's coordinates.
    int getPlayheadLineX() const noexcept;
    bool hasPlayheadPosition() const noexcept { return hasPlayheadX_; }

    // ---- Test hooks (mirrors TimelineClipLaneArea's getClipRect / isMarqueeActiveForTest) ----
    juce::Rectangle<int> getBackButtonBounds() const noexcept { return backButtonBounds_; }
    juce::Rectangle<int> getQuantiseButtonBounds() const noexcept { return quantiseButtonBounds_; }
    juce::Rectangle<int> getKeysColumnBounds() const noexcept { return keysColumnBounds_; }
    juce::Rectangle<int> getNoteGridBounds() const noexcept { return noteGridBounds_; }
    int getFirstVisiblePitchForTest() const noexcept { return firstVisiblePitch_; }
    bool isMarqueeActiveForTest() const noexcept { return dragMode_ == DragMode::Marquee; }
    NoteSelectionModel& getSelectionForTest() noexcept { return selection_; }

    // ---- Edge auto-scroll test hooks (mirrors TimelineClipLaneArea::tickAutoScrollForTest /
    // isAutoScrollTimerRunningForTest exactly) ----
    // Drives one autoScrollTick() without a real juce::Timer — a headless test run cannot wait on
    // wall-clock ticks, so this is the only way to exercise the timer's EFFECT deterministically.
    void tickAutoScrollForTest() { autoScrollTick(); }
    // The gating half of the timer contract a test pins: started only while a Move/Resize/
    // Marquee/DrawNew drag is live AND the last-known pointer sits inside an edge zone of
    // gridRegion(), stopped the instant either stops being true.
    bool isAutoScrollTimerRunningForTest() const noexcept { return autoScrollTimer_.isTimerRunning(); }

    // The row-mapping state a scale-context test asserts on directly, without going near paint():
    // every pitch that currently gets a row, ascending, and whichever of those is at the top.
    const std::vector<int>& getVisiblePitchesForTest() const noexcept { return visiblePitches_; }

    // The pixel width of the narrower black-key overlay drawn in paintKeysColumn, for the current
    // keys-column width — a pure geometry seam, no pixel-reading required.
    int blackKeyInsetForTest() const noexcept { return blackKeyWidthPx(keysColumnBounds_.getWidth()); }

    // The colour paintNote would resolve `note` to right now (its OWN doc fields — never a mid-drag
    // preview), so a scale-colouring test can assert on the resolved NotePaint instead of reading
    // pixels.
    synth::ui::NotePaint notePaintFor(const synth::MidiNote& note) const;

    // The snap division the grid currently draws its faintest lines at (0.0 for Snap::Off), and how
    // many lines at a given spacing are inside the grid region — both computed from state alone, so
    // a test can assert "the gridlines follow the snap division" without going near paint().
    double getGridDivisionForTest() const noexcept { return currentGridBeats(); }
    int getGridLineCountForTest(double spacingBeats) const noexcept;

    // True while the "Q" button is showing its momentary pressed highlight, and whether it would do
    // anything at all (a grid to snap to AND at least one note in the clip — it paints dimmed
    // otherwise).
    bool isQuantiseFlashingForTest() const noexcept { return quantiseFlash_; }
    bool isQuantiseEnabled() const;

    // The live rect for a note id, using its CURRENT doc geometry (never a mid-drag preview) — the
    // same "what tests use to compute where to synthesize a mouse event" role
    // TimelineClipLaneArea::getClipRect plays. Returns an empty rect if the id does not resolve
    // (doc null, roll closed, or no such note).
    juce::Rectangle<int> getNoteRect(synth::NoteId id) const;

    // Pure pitch<->y mapping for the current scroll position and vertical zoom (no component, no
    // doc) — what a test uses to place a synthetic mouse event at a target pitch row.
    int yForPitch(int pitch) const noexcept;
    int pitchForY(int y) const noexcept;

    // Split-tool hover preview state (the cut beat is CLIP-relative), and the clipboard's depth —
    // all three are pure state a test can assert on without going near paint().
    bool hasSplitPreviewForTest() const noexcept { return hasSplitPreview_; }
    double getSplitPreviewBeatForTest() const noexcept { return splitPreviewBeat_; }
    synth::NoteId getSplitPreviewNoteForTest() const noexcept { return splitPreviewNote_; }
    int getClipboardSizeForTest() const noexcept { return (int)noteClipboard_.size(); }
    // The Draw tool's in-flight preview length (0.0 when no draw gesture is running).
    double getDrawPreviewLengthForTest() const noexcept {
        return dragMode_ == DragMode::DrawNew ? drawLengthBeats_ : 0.0;
    }

    // ---- Scale-panel slide animation test hooks ----
    //
    // Mirrors ModuleLibraryComponent::setSectionProgress: drives the SAME per-frame math the real
    // AnimationDriver callback runs (see setScalePanelVisible), keyed by a normalised progress in
    // [0, 1] (0 = fully closed, 1 = fully open) rather than a width. A headless test has no real
    // VBlank reaching an off-screen component (setScalePanelVisible snaps immediately there — see
    // isShowing()), so this is the only way to exercise the tween's geometry at an intermediate
    // frame deterministically.
    void setScalePanelOpenProgressForTest(float progress);
    float getScalePanelOpenProgressForTest() const noexcept { return scalePanelOpenProgress_; }
    // The tween's captured START point for whichever toggle/restore most recently ran — what a test
    // reads to prove a mid-flight toggle reverses from the CURRENT width rather than jumping to an
    // extreme first (setScalePanelVisible captures this BEFORE deciding whether to animate at all).
    float getScalePanelAnimFromForTest() const noexcept { return scalePanelAnimFrom_; }
    bool isScalePanelAnimatingForTest() const noexcept { return scalePanelAnim_.isRunning(); }
    // The logical (target) open/closed state — what the header button paints as lit/active and
    // what persists, as opposed to scalePanelOpenProgress_'s mid-slide VISUAL value.
    bool isScalePanelTargetVisibleForTest() const noexcept { return scalePanelVisible_; }

    // ---- Header button hover test hooks (Task D chip affordance) ----
    enum class HeaderButtonId { None, Back, Quantise, Scale };
    HeaderButtonId getHoveredHeaderButtonForTest() const noexcept { return hoveredHeaderButton_; }
    bool isHeaderButtonHoveredForTest(HeaderButtonId which) const noexcept { return hoveredHeaderButton_ == which; }

protected:
    // THE paint-count seam for the local playhead line, mirroring
    // TimelinePlayheadOverlay::requestRepaintStrip exactly (a test subclasses and counts). Every
    // repaint the playhead costs here goes through it and nowhere else.
    virtual void requestRepaintStrip(juce::Rectangle<int> strip);

    // The SAME seam for the Split tool's hover preview, kept separate from the playhead's so a
    // test can count the two independently (a hover that repainted the playhead's strip would be
    // a bug, not a rounding difference). Called ONLY when the previewed cut actually moved.
    virtual void requestRepaintPreviewStrip(juce::Rectangle<int> strip);

    // THE paint-count seam for the header buttons' hover wash (Back/Quantise/Scale — see
    // updateHeaderButtonHover), same discipline as the two above: called ONLY when the hovered
    // chip actually changed, once per affected rect (the vacated chip, the newly hovered one),
    // never per mouse move.
    virtual void requestRepaintHeaderButtonStrip(juce::Rectangle<int> strip);

    // THE edge-auto-scroll timer's seam, mirroring TimelineClipLaneArea::autoScrollTick() exactly
    // (a test subclasses and drives it via tickAutoScrollForTest() rather than a real juce::Timer,
    // which a headless run cannot wait on at wall-clock speed). One tick scrolls rollView_/
    // firstVisiblePitch_ by edgeScrollVelocity's worth on whichever axis (or both) the last-known
    // pointer sits inside an edge zone of gridRegion() on, re-derives whichever gesture is in
    // flight from that SAME last-known pointer (an auto-scroll tick has no MouseEvent of its own —
    // the pointer isn't moving, the view is), fires onHorizontalViewChanged when the horizontal
    // mapping moved, and repaints.
    virtual void autoScrollTick();

private:
    enum class DragMode { None, Move, Resize, Marquee, VelocityScrub, DrawNew };

    struct NoteHit {
        synth::NoteId id;
        juce::Rectangle<int> rect;
        bool onRightEdge = false;
    };

    // One dragged/scrubbed note's ORIGIN (pre-gesture) state — every preview/commit computation
    // reads from this, never from the accumulating pointer position (TimelineClipLaneArea's
    // DragOrigin comment: this is what keeps rounding from accumulating frame to frame).
    struct NoteOrigin {
        synth::NoteId id;
        double startBeat = 0.0; // clip-relative
        double lengthBeats = 0.0;
        int pitch = 0;
        int velocity = 100;
    };

    std::optional<NoteHit> hitTestNote(juce::Point<int> pos) const;
    std::vector<std::pair<synth::NoteId, juce::Rectangle<int>>> collectNoteRects() const;
    // Absolute-beat span + pitch -> the note's rect, through the roll's OWN mapping (beatToX).
    juce::Rectangle<int> computeNoteRect(double absStartBeat, double absLengthBeats, int pitch) const;
    double currentBeatsPerBar() const;
    double currentGridBeats() const; // viewState_.divisionBeats(currentBeatsPerBar())
    double snappedBeatAt(double rawBeat) const;
    // Clamps a clip-relative [start, start+length) span into [0, clip->lengthBeats) — notes can
    // only exist inside the clip. TimelineDoc itself has no upper clamp (only startBeat >= 0), so
    // this is the editor's own policy, applied before every doc write.
    void clampToClipWindow(double& start, double& length) const;
    // The grid rect (right of the keys gutter, below the header) — what the local playhead strip
    // and the gridline sweep are clipped to.
    juce::Rectangle<int> gridRegion() const noexcept;

    // ---- Row mapping (visiblePitches_) ----

    // Rebuilds visiblePitches_ from the current scale context (empty function or
    // pitchVisibilityOn_ == false -> every pitch, unfiltered) plus the open clip's note pitches
    // (always kept visible, scale or no scale), then re-clamps firstVisiblePitch_ onto whatever
    // survived. Called from setScaleContext, openClip and refreshFromDoc — see the class comment.
    void rebuildVisiblePitches();
    // The index into visiblePitches_ closest to `pitch` — exact when `pitch` is itself visible
    // (which firstVisiblePitch_ always is, by the invariant above), nearest otherwise. This is the
    // one seam yForPitch/pitchForY and every vertical-scroll/zoom path route pitch<->row through.
    size_t nearestVisibleRowIndex(int pitch) const noexcept;
    // `originPitch` shifted by `rowDelta` VISIBLE ROWS (not semitones) and re-resolved to whatever
    // pitch sits at that row now — the seam a Move drag's preview and its mouseUp commit BOTH
    // route through, so a drag can never land a note on a pitch that is not itself a row (out of
    // scale, with pitch-visibility collapsing the rest). A rowDelta of 0 is the identity; when
    // visiblePitches_ is unfiltered (the common case) this is exactly semitone arithmetic, because
    // row index == pitch there.
    int rowShiftedPitch(int originPitch, long long rowDelta) const noexcept;

    // The colour resolution paintNote and notePaintFor share: builds the Colors value (themed, or a
    // default-constructed fallback with no LookAndFeel installed) and the outOfScale flag, then
    // defers to synth::ui::resolveNoteColour.
    synth::ui::NotePaint resolveNoteColourFor(int pitch, int velocity, bool selected, bool muted) const;

    // Pixel width of the narrower black-key overlay for a keys-column of `columnWidth` px — flush
    // left, about 62% of the column, so the remaining strip reads as the white-key colour showing
    // through (the gap between black keys on a real keyboard viewed side-on).
    static constexpr float kBlackKeyWidthFraction = 0.62f;
    static int blackKeyWidthPx(int columnWidth) noexcept {
        return (int)std::llround((double)columnWidth * (double)kBlackKeyWidthFraction);
    }

    // Effective (possibly mid-drag) clip-relative geometry / velocity for one note — read by
    // paint() and by commit-on-mouseUp, exactly like TimelineClipLaneArea::effectiveGeometryFor.
    struct NoteGeometry {
        double startBeat = 0.0;
        double lengthBeats = 0.0;
        int pitch = 0;
        int velocity = 100;
    };
    NoteGeometry effectiveGeometryFor(const synth::MidiNote& note) const;

    // Double-click on empty grid: adds ONE note, snapped, exactly one snap division long (or
    // kMinNoteLengthBeats when Snap is Off), selected, in one undo step.
    void createNoteAt(juce::Point<int> pos);
    // Where a new note would go for a click at `pos`, with everything both creators share: the
    // one-division length, the "snapping up past the clip's end steps back a division instead of
    // creating nothing" rule, and the clip-window clamp. `floorToGrid` is what separates the two
    // callers — the Select tool's double-click snaps to the NEAREST division (it is aiming at a
    // grid line), while the Draw tool's pencil FLOORS (it is filling the grid cell it is pointing
    // at, which is what every DAW pencil does).
    // @return false when no note could fit (no clip, or no room left inside it).
    bool computeNewNoteAnchor(juce::Point<int> pos, bool floorToGrid, double& startOut, double& lengthOut,
                              int& pitchOut) const;
    // addNote + select + repaint, wrapped in ONE undo step. Shared by createNoteAt and the Draw
    // tool's release.
    void commitNewNote(double startBeat, double lengthBeats, int pitch);
    void beginMoveOrResize(const NoteHit& hit, juce::Point<int> pos);
    void beginVelocityScrub(juce::Point<int> pos);
    void beginMarquee(juce::Point<int> anchor, bool additive);
    void updateMarquee(juce::Point<int> current);
    void endMarquee();

    // ---- Edge auto-scroll (see EdgeAutoScroll.h) ----
    //
    // Re-runs whichever gesture's preview math mouseDrag() runs — Move/Resize/DrawNew's beat math
    // plus Marquee's updateMarquee() — from lastDragPointer_ against the (possibly just-scrolled)
    // rollView_/firstVisiblePitch_. The one thing a real pointer move and an auto-scroll tick
    // share, factored out so they can never drift apart (mirrors TimelineClipLaneArea's own
    // updateDragPreviewFromLastPointer). VelocityScrub's preview is included too — it is a valid
    // dragMode_ to re-derive from, it simply never gets auto-scroll ARMED (see
    // updateAutoScrollArming), so this branch of it is unreachable from a tick in practice.
    void updateDragPreviewFromLastPointer();
    // Arms/disarms the edge-scroll timer for the CURRENT lastDragPointer_/dragMode_: started only
    // while a Move/Resize/Marquee/DrawNew drag is live and the pointer sits inside an edge zone of
    // gridRegion() on EITHER axis, stopped the instant neither holds. Called after every
    // mouseDrag update; mouseUp always disarms unconditionally instead (the drag it would be
    // gating on just ended).
    void updateAutoScrollArming();

    // ---- Tool gestures (everything below acts on a single click; see setActiveTool) ----

    // Routes a mouse-down for any tool other than Select. Returns with the gesture already done
    // (Split/Glue/Erase/Mute act immediately) or with the Draw gesture armed.
    void handleToolMouseDown(juce::Point<int> pos);
    // The snapped, CLIP-relative beat a Split click at this x would cut `note` at, or nullopt when
    // that beat is not strictly inside it — a cut has to leave at least kMinNoteLengthBeats on BOTH
    // sides, otherwise "split" would silently mean "resize to nothing".
    std::optional<double> splitBeatFor(const synth::MidiNote& note, int x) const;
    void performSplit(synth::NoteId id, juce::Point<int> pos);
    // The next note of the SAME pitch that a Glue click on `id` would absorb: the smallest
    // startBeat at or after the clicked note's END. Gaps ARE bridged (the glued note runs from the
    // clicked note's start to the absorbed note's end) — Cubase's behaviour, and the only one that
    // makes gluing a staccato pair into one sustained note possible at all.
    std::optional<synth::NoteId> glueCandidateFor(const synth::MidiNote& note) const;
    void performGlue(synth::NoteId id);
    void performErase(synth::NoteId id);
    void performMuteToggle(synth::NoteId id);

    // ---- Split-tool hover preview ----
    void updateSplitPreview(juce::Point<int> pos);
    void clearSplitPreview();
    juce::Rectangle<int> splitPreviewStrip() const;

    // ---- Tool cursors (built once per theme, never per mouse move) ----
    void rebuildToolCursors();
    juce::MouseCursor cursorForActiveTool();
    void applyToolCursor();
    void updateHoverCursor(juce::Point<int> pos);

    // ---- Header button hover (Back/Quantise/Scale chips — Task D affordance) ----
    // bounds for `which` (empty for None) — the ONE seam updateHeaderButtonHover and paintHeader's
    // hover wash both read, so the gated repaint rect and the painted rect can never drift apart.
    juce::Rectangle<int> headerButtonBoundsFor(HeaderButtonId which) const noexcept;
    // Recomputes which chip (if any) `pos` is over; a no-op when it hasn't changed (the repaint
    // invariant — a mouse hovering the SAME chip, or empty grid, costs nothing). On a real change,
    // repaints the vacated chip's rect and the newly hovered one, through
    // requestRepaintHeaderButtonStrip so a test can count exactly this.
    void updateHeaderButtonHover(juce::Point<int> pos);

    // ---- Clipboard plumbing ----
    // The selection as clipboard entries (offsets relative to the earliest selected note), plus
    // that earliest start and the block's span (max end - min start). Empty when nothing is
    // selected or the ids no longer resolve.
    std::vector<ClipboardNote> captureSelectionEntries(double& earliestStartOut, double& spanBeatsOut) const;
    // Turns one entry block anchored at `anchorBeat` into concrete notes, appended to `out`,
    // applying the clip-window policy: a note starting at/after the clip's end is SKIPPED, a note
    // whose length would overrun the end is clamped to it, and a note with less than
    // kMinNoteLengthBeats of room left is skipped rather than shrunk below the editor's minimum.
    // @return true if the block contributed at least one note — false is the repeat loop's cue to
    // stop placing further blocks.
    bool buildPastedNotes(const std::vector<ClipboardNote>& entries, double anchorBeat,
                          std::vector<synth::MidiNote>& out) const;
    // ONE undo step for the whole batch; the created notes become the selection.
    bool commitPastedNotes(const std::vector<synth::MidiNote>& notes);

    // ---- Arrow-key editing ----
    // One SHARED delta for the whole selection, clamped so the group stays inside the clip window
    // / inside [0, 127] — never per-note clamping, which would silently reshape a chord.
    bool nudgeSelectedNotes(int direction);
    bool transposeSelectedNotes(int semitones);

    // ---- Alt+Left/Right note navigation (selection only — never a mutation, never an undo step) ----
    // Selects the note next to the current selection in the clip's CANONICAL order — (startBeat,
    // then pitch, then id), which is the order TimelineDoc keeps Clip::notes in, so this walks that
    // vector by index rather than re-deriving an order that could drift from the doc's.
    //
    // The anchor is the edge of the selection the walk is heading TOWARDS: forward starts from the
    // selection's LAST note, backward from its FIRST. That is what makes a multi-selection collapse
    // onto the neighbour just outside the block (and makes repeated presses sweep the clip) instead
    // of landing back inside it. Alt is the modifier because plain arrows already nudge, Shift+Up/
    // Down is the octave transpose, and Cmd+arrow carries OS jump-to-boundary meaning.
    //
    // @return false ONLY when there is nothing to navigate from (roll closed, or nothing selected in
    // this clip) — that is the key's fall-through cue. At either END of the clip the selection is
    // kept and true is still returned, the same "the key WAS applicable" contract a fully clamped
    // nudge honours.
    bool selectAdjacentNote(bool forward);
    // Minimal HORIZONTAL scroll so `note` is inside the grid region; a no-op (and no repaint) when
    // it already is. Vertical scroll is deliberately not touched — see the definition.
    void scrollNoteIntoView(const synth::MidiNote& note);

    // ---- Rebindable surface keys ----
    // True when `key` is what the user has bound to `actionId`. With no ShortcutManager installed
    // this is plain equality against `fallback` (the hardcoded default) — juce::KeyPress::operator==
    // unchanged, which is what makes Left, Shift+Left and Alt+Left three different actions while
    // letter keys still compare case-insensitively (so Shift+Q matches a 'q' binding). With one
    // installed the manager is the ONLY source: an unset/unknown/invalid binding matches nothing,
    // and `fallback` is not consulted — see setShortcutManager for why the two are never mixed.
    //
    // That installed-manager path is routed through ShortcutManager::keyPressMatches rather than
    // raw juce::KeyPress::operator==: macOS delivers a Shift-chorded symbol key as the SHIFTED
    // CHARACTER ('!' not '1', '+' not '='), never the base key plus a Shift modifier flag, so exact
    // equality would silently never match a user rebind onto such a chord. keyPressMatches
    // shift-normalizes exactly that case.
    bool matchesAction(const juce::KeyPress& key, const juce::String& actionId, const juce::KeyPress& fallback) const;

    // ---- Dynamic shortcut-hint tooltips (see synth::shortcutHintFor) ----
    // Rebuilt fresh on every call by reading shortcuts_ live, so a rebind is reflected the very
    // next time getTooltipFor() is queried — no cache, no listener needed (unlike
    // TimelinePanelComponent's real juce::Button tooltips, which DO cache and therefore need one).
    juce::String quantiseTooltipText() const;
    juce::String scaleTooltipText() const;

    // ---- Anchored zoom, shared by the wheel, the pinch and the public zoom API ----
    // `anchorGridX` is measured from the GRID's left edge (x - leftGutterWidth()), which is the
    // coordinate rollView_ maps; `anchorY` is a component y.
    void zoomHorizontalAroundX(double factor, double anchorGridX);
    void zoomVerticalAroundY(double factor, double anchorY);

    // The exp() factor for a Cmd/Cmd+Shift wheel zoom, shared by both mouseWheelMove branches so
    // they can never disagree about which way is "in". Sign comes from the PHYSICAL gesture
    // direction (synth::ui::wheelGestureIsUpward) XOR zoomScrollInverted_; magnitude comes from
    // std::abs(dominantWheelDelta(wheel)) * kZoomWheelSensitivity — the exact amount each branch
    // used before, just no longer signed by the raw delta.
    double wheelZoomFactor(const juce::MouseWheelDetails& wheel) const noexcept;

    void performQuantise();
    void flashQuantiseButton();
    void timerCallback() override; // one-shot: ends the quantise flash and stops itself
    void requestClose();

    // ---- Scale assist panel plumbing (toggleScalePanel is public — see the accessors above) ----
    // Shared by the header-button click (animate=true) and setPropertiesFile's restore
    // (animate=false — a persisted restore must never itself play a slide). A no-op (no resize, no
    // repaint, no persist write, no animation) when `visible` already matches the logical target,
    // so restoring the SAME default (closed) on a fresh PropertiesFile costs nothing.
    void setScalePanelVisible(bool visible, bool animate = true);
    // The per-frame tween math (also the test seam's implementation): pins scalePanelOpenProgress_,
    // re-carves the layout and repaints, and re-issues onHorizontalViewChanged — the mapping
    // genuinely moves every frame, and TimelinePanelComponent's ruler override has to track it.
    void applyScalePanelOpenProgress(float progress);
    // Stops any running tween, pins scalePanelOpenProgress_ to its exact target value, and (only
    // when the logical target is closed) hides the child component — called by the animation's
    // own onComplete, by setScalePanelVisible when there is no VBlank to animate with, and by
    // openClip/closeRoll to snap an in-flight slide instantly across a clip switch.
    void finishScalePanelAnimation();
    // Pushes clipScaleMemory_[clipId_] (or the "no clip open" defaults) into setScaleContext —
    // the single place both the panel's onScaleChanged/onPitchVisibilityChanged handlers and
    // openClip's restore route through, so the roll's row-visibility/colouring can never disagree
    // with what the panel is showing.
    void pushScaleContextFromMemory();
    // Restores clipId_'s remembered scale (or "No scale" for a clip never opened before) into the
    // panel AND the roll's own scale context. Called from openClip, after clipId_ is set.
    void restoreScaleMemoryForOpenClip();

    void paintKeysColumn(juce::Graphics& g);
    void paintHeader(juce::Graphics& g);
    void paintGrid(juce::Graphics& g);
    // `lineColour` is the theme token the grid is derived from (Colors::border) and `background` is
    // what sits behind it (Colors::bg0) — both handed to the shared three-level colour policy
    // (synth::ui::gridLineColourFor, in TimelineClipLaneArea.h), which is why the background is a
    // parameter rather than something this function re-reads from the LookAndFeel.
    void paintGridLines(juce::Graphics& g, juce::Colour lineColour, juce::Colour background);
    void paintNote(juce::Graphics& g, const synth::MidiNote& note);
    void paintPlayhead(juce::Graphics& g);
    void paintMarquee(juce::Graphics& g);
    void paintDrawPreview(juce::Graphics& g);
    void paintSplitPreview(juce::Graphics& g);

    // [first, last] multiples of `spacingBeats` visible in the grid region — empty (last < first)
    // when the lines would be closer together than kMinGridLinePixels. paintGridLines and
    // getGridLineCountForTest walk the SAME range, so the seam can never drift from the paint.
    struct LineRange {
        long long first = 0;
        long long last = -1;
        int count() const noexcept { return last < first ? 0 : (int)(last - first + 1); }
    };
    LineRange visibleLineRange(double spacingBeats) const noexcept;

    juce::Rectangle<int> playheadStripFor(int x) const noexcept;

    TimelineViewState& viewState_; // shared: SNAP ONLY (see the class comment)
    // The roll's own zoom/scroll. Reuses TimelineViewState purely for its clamped
    // zoomAroundX/scrollBeats math; its x origin is the GRID's left edge, which beatToX/xToBeat
    // offset by leftGutterWidth().
    TimelineViewState rollView_;
    double pixelsPerSemitone_ = kPixelsPerSemitone;

    synth::TimelineDoc* doc_ = nullptr;
    AppUndoManager* undoManager_ = nullptr;
    synth::TransportService* transport_ = nullptr;
    // Non-owning, may stay null (see setShortcutManager). const because the roll only ever READS
    // bindings — rebinding is the Settings window's job.
    const ShortcutManager* shortcuts_ = nullptr;
    // App-level scroll-direction preference; false == natural == the juce::Viewport convention.
    bool scrollInverted_ = false;
    // App-level ZOOM-direction preference; false == wheel up zooms in. A separate flag from
    // scrollInverted_ — see setZoomScrollInverted.
    bool zoomScrollInverted_ = false;
    // Fractional ROWS left over after the plain-wheel pitch-scroll branch truncates its per-event
    // amount to a whole row (mouseWheelMove). A trackpad's small deltaY scaled by
    // kPitchScrollSemitonesPerWheelUnit rounds to ZERO rows on almost every individual event —
    // without this carry, the gesture never moves at all until a single event happens to clear a
    // whole row on its own ("only sometimes works"). Reset on openClip/closeRoll so a stale
    // sub-row fraction from one clip never surfaces as a surprise extra row on the next.
    double pitchScrollRemainder_ = 0.0;

    synth::ClipId clipId_;
    NoteSelectionModel selection_;

    // firstVisiblePitch_ is the HIGHEST pitch drawn at the grid's top row (y == kHeaderHeight),
    // clamped to [0, 127] — see yForPitch/pitchForY. Named to match the design doc's "scroll
    // position", even though (unlike TimelineViewState::firstVisibleBeat, the beat at x==0) this
    // one is the TOP of the range rather than conceptually its start, because pitch increases
    // upward while beats increase rightward. Always a member of visiblePitches_ — see the class
    // comment and rebuildVisiblePitches.
    int firstVisiblePitch_ = 60;

    // ---- Row mapping / scale context ----
    // Every pitch that currently gets a row, ascending. All 128 when no filter is active — see
    // rebuildVisiblePitches. Populated in the constructor so yForPitch/pitchForY are well-defined
    // even before openClip or setScaleContext is ever called.
    std::vector<int> visiblePitches_;
    // Empty (the default) means "no scale at all" — see setScaleContext.
    std::function<bool(int)> isInScale_;
    bool pitchVisibilityOn_ = false;

    // ---- Note colouring ----
    synth::ui::NoteColourOverrides overrides_;

    // ---- Keys column label density ----
    KeyLabelMode keyLabelMode_ = KeyLabelMode::AllNotes;

    DragMode dragMode_ = DragMode::None;
    synth::NoteId activeNote_;
    juce::Point<int> mouseDownPos_;
    // The BEAT under the pointer at mouseDown (xToBeat(mouseDownPos_.x)), captured alongside the
    // pixel position — every Move/DrawNew drag computes its delta as xToBeat(currentX) -
    // mouseDownBeat_ rather than xToBeat(currentX) - xToBeat(mouseDownPos_.x). The pixel form only
    // agrees with the beat form while the view is static; a mid-drag edge-scroll (or, in
    // principle, any other scroll/zoom) moves rollView_'s firstVisibleBeat, and re-deriving
    // xToBeat(mouseDownPos_.x) at that point would silently reinterpret the anchor at the NEW
    // scroll position instead of the one the gesture actually started at. Same reasoning as
    // TimelineClipLaneArea::mouseDownBeat_.
    double mouseDownBeat_ = 0.0;
    // The pitch under the pointer at mouseDown (pitchForY(mouseDownPos_.y)) — the Move drag's
    // vertical anchor, expressed as a PITCH (not a row index) so it survives rebuildVisiblePitches
    // rebuilding the row set mid-gesture; every read of it goes back through
    // nearestVisibleRowIndex to recover its row. See previewDeltaPitch_ for why the anchor has to
    // be row-space at all.
    int mouseDownPitch_ = 60;
    // The last pointer position mouseDrag() saw, in this component's local coordinates. An
    // auto-scroll tick has no MouseEvent of its own — the pointer isn't moving, the view is — so
    // updateDragPreviewFromLastPointer() re-derives the in-flight preview from this instead of
    // from a synthesized event.
    juce::Point<int> lastDragPointer_;

    // ---- Move preview (one or many notes, one shared snapped beat delta + ROW delta) ----
    std::vector<NoteOrigin> dragNotes_;
    double previewDeltaBeats_ = 0.0;
    // A delta in visiblePitches_ ROW INDICES, not semitones: with pitch-visibility collapsing
    // out-of-scale rows, adding a raw semitone count straight to a note's pitch could land it on a
    // pitch that is not itself a row right now. Every consumer (effectiveGeometryFor's Move
    // branch, the mouseUp commit) resolves it through rowShiftedPitch, never by direct addition.
    // In the common unfiltered case (every pitch visible) this is numerically identical to a
    // semitone delta, because row index == pitch there.
    int previewDeltaPitch_ = 0;

    // ---- Resize preview (always the single grabbed note) ----
    double resizeOriginalLength_ = 0.0;
    double previewLength_ = 0.0;

    // ---- Velocity-scrub preview (one or many notes, one shared delta) ----
    int previewDeltaVelocity_ = 0;

    // ---- Draw-tool preview (a note that does not exist in the doc yet) ----
    double drawStartBeat_ = 0.0; // clip-relative
    double drawLengthBeats_ = 0.0;
    int drawPitch_ = 60;

    // ---- Marquee (any drag that STARTED on empty grid — see mouseDown's comment) ----
    juce::Point<int> marqueeAnchor_;
    juce::Rectangle<int> marqueeRect_;
    bool marqueeAdditive_ = false;
    std::vector<synth::NoteId> marqueeBaseSelection_;

    // Deferred-deselect, the same trick TimelineClipLaneArea::pendingEmptyClick_ (and, before it,
    // GraphEditor's pendingEmptyCanvasClick) uses: a PLAIN press on empty grid is ambiguous at
    // mouse-down time — it becomes a replace-marquee if it ever moves, and a deselect if it does
    // not — so neither is committed until mouseDrag or mouseUp says which one happened. Only the
    // Select tool ever sets it, and setActiveTool clears it with the rest of the in-flight gesture.
    bool pendingEmptyClick_ = false;

    // ---- Local playhead (fed by TimelinePlayheadOverlay; no timer of our own) ----
    double playheadBeat_ = 0.0;
    int playheadLineX_ = 0;
    bool hasPlayheadX_ = false;
    // See setFollowPlayhead. Off by default.
    bool followPlayhead_ = false;

    bool quantiseFlash_ = false;

    // ---- Scale assist panel ----
    //
    // A child component, always present (addChildComponent — so it starts invisible), toggled by
    // the header's "Scale" button and restored from PropertiesFile in setPropertiesFile. Owned by
    // value: it outlives openClip/closeRoll exactly like the note clipboard, so switching clips
    // never rebuilds it.
    synth::ui::ScaleAssistPanel scalePanel_;
    juce::Rectangle<int> scaleButtonBounds_;
    // Non-owning; may stay null (see setPropertiesFile). Only ever used to persist THIS roll's own
    // "was the panel open" flag — the panel persists its OWN user scales through the same pointer,
    // handed to it directly in setPropertiesFile.
    juce::PropertiesFile* propertiesFile_ = nullptr;

    // ---- Scale-panel slide animation (in/out — see setScalePanelVisible) ----
    //
    // The LOGICAL target (what the header button paints lit and what persists), separate from
    // scalePanelOpenProgress_'s mid-slide VISUAL value below — the same split
    // ModuleLibraryComponent's collapsedSections/sectionProgress pair uses for its own fold.
    bool scalePanelVisible_ = false;
    // 0 = fully closed, 1 = fully open — leftGutterWidth() is the ONE reader (see its comment).
    // Animated by scalePanelAnim_'s per-frame callback via applyScalePanelOpenProgress(), or
    // pinned straight to its target by finishScalePanelAnimation() when there is no VBlank to
    // animate with (a headless test, a PropertiesFile restore, or a clip switch mid-slide).
    float scalePanelOpenProgress_ = 0.0f;
    // The tween's own start/end, captured at the moment a toggle/restore is requested — kept as
    // members (not lambda captures alone) so a test can assert the START point without a running
    // VBlank (see getScalePanelAnimFromForTest).
    float scalePanelAnimFrom_ = 0.0f;
    float scalePanelAnimTo_ = 0.0f;
    synth::ui::AnimationDriver scalePanelAnim_;
    // Lazily created on the FIRST real (on-screen) toggle — mirrors ModuleLibraryComponent's own
    // vblankUpdater, which is `this` (a juce::Component) and must therefore not exist before the
    // component does.
    std::optional<juce::VBlankAnimatorUpdater> scalePanelVblankUpdater_;
    // Within the house 160-220 ms spec (docs/layout.md §11), matching animatePanelTransition's own
    // ~190 ms feel for the app's other show/hide sidebars.
    static constexpr double kScalePanelAnimMs = 200.0;

    // Per-clip scale memory: SESSION-ONLY, deliberately never persisted (mirrors rollView_ and
    // firstVisiblePitch_, which are not persisted either) — see setScaleContext's class-comment
    // discussion of why a clip's row/colour context is view state, not document state. A clip id
    // absent from this map has never had a scale chosen for it and reads back as "No scale" /
    // pitch-visibility off, per the class's "a clip never opened starts at No scale" contract.
    struct ClipScaleMemory {
        std::optional<synth::MusicalScale> scale;
        bool pitchVisibilityOn = false;
    };
    std::map<synth::ClipId, ClipScaleMemory> clipScaleMemory_;

    // ---- Edge auto-scroll timer ----
    //
    // A NESTED juce::Timer rather than a second responsibility multiplexed onto the class's own
    // private juce::Timer base (used above for the one-shot quantise flash, timerCallback()) —
    // one juce::Timer answering to two unrelated reasons would need a mode flag in every callback,
    // exactly the kind of "which timer is this tick for" bug a dedicated Timer avoids by
    // construction. autoScrollTick() (the protected virtual seam a test overrides) does the real
    // work; this struct only forwards juce::Timer's callback to it.
    struct AutoScrollTimer final : public juce::Timer {
        explicit AutoScrollTimer(PianoRollComponent& ownerRef)
            : owner(ownerRef) {}
        void timerCallback() override { owner.autoScrollTick(); }
        PianoRollComponent& owner;
    };
    AutoScrollTimer autoScrollTimer_{*this};

    // ---- Edit tool + its cursor cache ----
    EditTool activeTool_ = EditTool::Select;
    // Indexed by (size_t)EditTool. Built once (lazily, and again after a theme switch) because
    // rasterising six icons into six juce::Images on every mouse move would be exactly the kind of
    // per-frame work the repaint invariant exists to prevent.
    std::array<juce::MouseCursor, kAllEditTools.size()> toolCursors_;
    bool toolCursorsBuilt_ = false;
    // Select-tool only: whether the resize-zone cursor is the one currently installed, so a hover
    // that stays inside (or outside) the zone does no work at all.
    bool showingResizeCursor_ = false;

    // ---- Split-tool hover preview (the cut line drawn on the hovered note) ----
    synth::NoteId splitPreviewNote_;
    double splitPreviewBeat_ = 0.0; // clip-relative
    bool hasSplitPreview_ = false;

    // ---- Note clipboard: a MEMBER, so a copy survives switching clips (see copySelectedNotes) ----
    std::vector<ClipboardNote> noteClipboard_;

    juce::Rectangle<int> backButtonBounds_;
    juce::Rectangle<int> quantiseButtonBounds_;
    juce::Rectangle<int> keysColumnBounds_;
    juce::Rectangle<int> noteGridBounds_;

    // Which header chip (if any) the pointer is currently over — see updateHeaderButtonHover.
    HeaderButtonId hoveredHeaderButton_ = HeaderButtonId::None;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollComponent)
};

} // namespace synth::ui
