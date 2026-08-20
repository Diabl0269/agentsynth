#pragma once

#include "../Timeline/TimelineDoc.h"
#include "EditTool.h"
#include "NoteSelectionModel.h"
#include "TimelinePlayheadOverlay.h"
#include "TimelineViewState.h"
#include <array>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <utility>
#include <vector>

class AppUndoManager; // Forward declaration (Source/AppUndoManager.h)

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
// kKeysColumnWidth is the first visible beat, so the keys column is a real GUTTER and the first bar
// of a clip is reachable rather than hidden under an opaque strip. The shared TimelineViewState is
// still consulted for ONE thing: the snap division (snapBeat/divisionBeats), so the roll's grid,
// its snapped edits and the panel's snap selector never disagree.
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

    static constexpr const char* kQuantiseTooltip =
        "Snap to grid on/off (Q) \xE2\x80\x94 Shift+click quantizes the selected notes to the grid "
        "(or all notes when nothing is selected)";

    explicit PianoRollComponent(TimelineViewState& viewState);
    ~PianoRollComponent() override = default;

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

    // ---- Edit tools (Cubase-style; see EditTool.h) ----

    /** The active tool. TimelinePanelComponent owns the choice (one strip drives whichever editor
     *  is currently swapped into the lane rect) and pushes it here; the roll never switches tool by
     *  itself. In particular keyPressed() deliberately does NOT consume the tool digits — they
     *  bubble to the panel, which owns that binding, so the two can never disagree about which
     *  tool is active.
     *
     *  Select keeps the whole pre-existing gesture table (click-select, drag-move, right-edge
     *  resize, Cmd velocity scrub, Shift marquee, double-click create/delete). The other five
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

    // Flips the shared snap switch (TimelineViewState::snapEnabled), flashes the Q button and
    // fires onSnapToggled. The Q button and the panel-wide Q key both land here.
    void toggleSnap();

    // THE refresh seam, called by the panel's TimelineDoc::Listener on every doc mutation (mirrors
    // TimelineClipLaneArea::refreshFromDoc): prunes the note selection of anything the mutation
    // removed. If the EDITED CLIP itself is gone, closes the roll and fires onCloseRequested() so
    // the owner swaps back to the clip lanes. A no-op while closed.
    void refreshFromDoc();

    // ---- The roll's OWN horizontal mapping ----

    // Absolute beat <-> this component's x. x == kKeysColumnWidth is the first visible beat: the
    // keys column is a gutter, NOT an overlay painted over the grid's leftmost pixels.
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

protected:
    // THE paint-count seam for the local playhead line, mirroring
    // TimelinePlayheadOverlay::requestRepaintStrip exactly (a test subclasses and counts). Every
    // repaint the playhead costs here goes through it and nowhere else.
    virtual void requestRepaintStrip(juce::Rectangle<int> strip);

    // The SAME seam for the Split tool's hover preview, kept separate from the playhead's so a
    // test can count the two independently (a hover that repainted the playhead's strip would be
    // a bug, not a rounding difference). Called ONLY when the previewed cut actually moved.
    virtual void requestRepaintPreviewStrip(juce::Rectangle<int> strip);

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

    void performQuantise();
    void flashQuantiseButton();
    void timerCallback() override; // one-shot: ends the quantise flash and stops itself
    void requestClose();

    void paintKeysColumn(juce::Graphics& g);
    void paintHeader(juce::Graphics& g);
    void paintGrid(juce::Graphics& g);
    void paintGridLines(juce::Graphics& g, juce::Colour lineColour);
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
    // offset by kKeysColumnWidth.
    TimelineViewState rollView_;
    double pixelsPerSemitone_ = kPixelsPerSemitone;

    synth::TimelineDoc* doc_ = nullptr;
    AppUndoManager* undoManager_ = nullptr;
    synth::TransportService* transport_ = nullptr;

    synth::ClipId clipId_;
    NoteSelectionModel selection_;

    // firstVisiblePitch_ is the HIGHEST pitch drawn at the grid's top row (y == kHeaderHeight),
    // clamped to [0, 127] — see yForPitch/pitchForY. Named to match the design doc's "scroll
    // position", even though (unlike TimelineViewState::firstVisibleBeat, the beat at x==0) this
    // one is the TOP of the range rather than conceptually its start, because pitch increases
    // upward while beats increase rightward.
    int firstVisiblePitch_ = 60;

    DragMode dragMode_ = DragMode::None;
    synth::NoteId activeNote_;
    juce::Point<int> mouseDownPos_;

    // ---- Move preview (one or many notes, one shared snapped beat delta + semitone delta) ----
    std::vector<NoteOrigin> dragNotes_;
    double previewDeltaBeats_ = 0.0;
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

    // ---- Marquee (Shift+drag on empty grid only — see mouseDown's comment) ----
    juce::Point<int> marqueeAnchor_;
    juce::Rectangle<int> marqueeRect_;
    bool marqueeAdditive_ = false;
    std::vector<synth::NoteId> marqueeBaseSelection_;

    // ---- Local playhead (fed by TimelinePlayheadOverlay; no timer of our own) ----
    double playheadBeat_ = 0.0;
    int playheadLineX_ = 0;
    bool hasPlayheadX_ = false;

    bool quantiseFlash_ = false;

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollComponent)
};

} // namespace synth::ui
