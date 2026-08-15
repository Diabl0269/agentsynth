#pragma once

#include "../Timeline/TimelineDoc.h"
#include "NoteSelectionModel.h"
#include "TimelineViewState.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <utility>
#include <vector>

class AppUndoManager; // Forward declaration (Source/AppUndoManager.h)

namespace synth {
class TransportService; // Forward declaration (Source/Transport/TransportService.h)
}

// PianoRollComponent — TL5-8: the minimal piano-roll editor for ONE clip, shown INSIDE the
// timeline panel's lanes region (no separate window). TimelinePanelComponent swaps this in for
// synth::ui::TimelineClipLaneArea (same rect, same z-order slot, below the playhead overlay)
// when a clip is double-clicked, and swaps back on the back button, Escape-with-nothing-selected,
// or the edited clip disappearing from the doc.
//
// Reuses TimelineClipLaneArea's idioms rather than inventing new ones (see that class's header
// comment, which this one mirrors):
//   - Non-owning refs/pointers with the same null-safety contract: TimelineViewState& (shared with
//     the ruler/lanes/playhead so beat<->pixel mapping never drifts), TimelineDoc* / AppUndoManager*
//     / TransportService* setters that may be null and degrade to "read but don't mutate".
//   - Every edit previews locally (a member offset/length/velocity delta, read back in paint()
//     through effectiveXFor() helpers) and commits to the doc exactly once on mouse-up, through
//     AppUndoManager::recordTimelineChange — so a multi-note move/resize/velocity-scrub/delete is
//     ONE undo step however many notes it touches.
//   - Panel-scoped Delete/Escape (grabs focus on mouseDown), returning false when there is nothing
//     to act on so the key falls through.
//
// Coordinate system — the ONE piece of this class that is NOT a straight copy of the clip-lane
// idiom, and worth reading closely: note x positions are computed with viewState_.beatToX(beat)
// UNMODIFIED — the exact same call TimelineClipLaneArea makes for a clip, in the exact same
// component-local coordinate frame (this component occupies exactly the rect TimelineClipLaneArea
// did). That is what makes "the playhead lines up": TimelinePlayheadOverlay is untouched by this
// task — same bounds, same beatToX(beat) call — so a note at a given absolute beat and the
// playhead line at that same beat land on the same pixel. The 44 px "keys column" is therefore NOT
// a reserved left margin that shifts the grid's origin (a conventional piano roll's keys column
// would); it is a fixed opaque strip painted OVER the leftmost 44 px of that same coordinate frame
// (mouseDown/mouseDrag ignore x < kKeysColumnWidth, ceding that sliver to the keys column). The
// trade-off: whatever beat currently sits at the scrolled-to x==0 is visually covered by the keys
// column rather than pushed rightward. Documented here and in docs/layout.md §16 as a deliberate
// v1 choice, not an oversight.
//
// Notes are clip-relative in the doc (MidiNote::startBeat); every doc read/write here converts to
// absolute beats via clip->startBeat and back, exactly like TimelineClipLaneArea::paintClip's note
// preview does.
namespace synth::ui {

class PianoRollComponent : public juce::Component {
public:
    // Fixed geometry (TL5-8 design: "reuse existing tokens; add none unless needed" — these are
    // new, piano-roll-only constants, not reused from anywhere, so they stay local rather than
    // joining Theme::Metrics).
    static constexpr int kKeysColumnWidth = 44;
    static constexpr int kHeaderHeight = 20;
    static constexpr double kPixelsPerSemitone = 10.0;
    // The floor under a drawn/resized note's length when Snap is Off — TimelineClipLaneArea's
    // kMinClipLengthBeats reasoning, reused: TimelineViewState's own finest grid unit.
    static constexpr double kMinNoteLengthBeats = 0.0625;
    // Resize handle width at a note's right edge, in px — TimelineClipLaneArea's kEdgeZonePx is a
    // 6 px BAND either side of an edge shared by move+resize; the piano-roll spec calls for a
    // plain "<= 5 px from the right edge" zone (no left-edge resize in v1).
    static constexpr int kResizeZonePx = 5;

    explicit PianoRollComponent(TimelineViewState& viewState);
    ~PianoRollComponent() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    // Panel-scoped Delete/Escape. Returns false (key falls through) when there is nothing to act
    // on, the same TimelineClipLaneArea contract.
    bool keyPressed(const juce::KeyPress& key) override;

    // Non-owning; may be null. Same degrade-gracefully contract as every other timeline
    // sub-component's setter.
    void setTimelineDoc(synth::TimelineDoc* doc) noexcept { doc_ = doc; }
    synth::TimelineDoc* getTimelineDoc() const noexcept { return doc_; }
    void setUndoManager(AppUndoManager* undoManager) noexcept { undoManager_ = undoManager; }
    AppUndoManager* getUndoManager() const noexcept { return undoManager_; }
    // Only consulted for Snap::Bar's beatsPerBar, same reasoning as
    // TimelineClipLaneArea::setTransport.
    void setTransport(synth::TransportService* transport) noexcept { transport_ = transport; }

    // ---- Entry/exit (panel API surface: openPianoRoll/closePianoRoll/isPianoRollOpen forward
    // straight to these three) ----

    // Opens the roll for `id`. A no-op (stays/becomes closed) if doc_ is null or `id` does not
    // resolve to a live clip. Clears the note selection, resets any in-flight gesture, and centres
    // the pitch scroll on the clip's median note pitch (60 for an empty clip).
    void openClip(synth::ClipId id);
    void closeRoll();
    bool isOpen() const noexcept { return clipId_.isValid(); }
    synth::ClipId getClipId() const noexcept { return clipId_; }

    // Fired when the roll asks to be closed: the back button, Escape with nothing selected, or
    // refreshFromDoc() noticing the edited clip is gone. The owner (TimelinePanelComponent) wires
    // this to its own closePianoRoll(), which also re-shows the clip-lane area. Not fired by a
    // direct closeRoll() call (that IS the close — no need to ask again).
    std::function<void()> onCloseRequested;

    // THE refresh seam, called by the panel's TimelineDoc::Listener on every doc mutation (mirrors
    // TimelineClipLaneArea::refreshFromDoc): prunes the note selection of anything the mutation
    // removed. If the EDITED CLIP itself is gone, closes the roll and fires onCloseRequested() so
    // the owner swaps back to the clip lanes. A no-op while closed.
    void refreshFromDoc();

    // ---- Test hooks (mirrors TimelineClipLaneArea's getClipRect / isMarqueeActiveForTest) ----
    juce::Rectangle<int> getBackButtonBounds() const noexcept { return backButtonBounds_; }
    juce::Rectangle<int> getQuantiseButtonBounds() const noexcept { return quantiseButtonBounds_; }
    juce::Rectangle<int> getKeysColumnBounds() const noexcept { return keysColumnBounds_; }
    juce::Rectangle<int> getNoteGridBounds() const noexcept { return noteGridBounds_; }
    int getFirstVisiblePitchForTest() const noexcept { return firstVisiblePitch_; }
    bool isMarqueeActiveForTest() const noexcept { return dragMode_ == DragMode::Marquee; }
    NoteSelectionModel& getSelectionForTest() noexcept { return selection_; }

    // The live rect for a note id, using its CURRENT doc geometry (never a mid-drag preview) — the
    // same "what tests use to compute where to synthesize a mouse event" role
    // TimelineClipLaneArea::getClipRect plays. Returns an empty rect if the id does not resolve
    // (doc null, roll closed, or no such note).
    juce::Rectangle<int> getNoteRect(synth::NoteId id) const;

    // Pure pitch<->y mapping for the current scroll position (no component, no doc) — what a test
    // uses to place a synthetic mouse event at a target pitch row.
    int yForPitch(int pitch) const noexcept;
    int pitchForY(int y) const noexcept;

private:
    enum class DragMode { None, Draw, Move, Resize, Marquee, VelocityScrub };

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
    // Absolute-beat span + pitch -> the note's rect in this component's own coordinate frame — see
    // the class comment on why this is viewState_.beatToX(absBeat) UNMODIFIED (no keys-column
    // offset).
    juce::Rectangle<int> computeNoteRect(double absStartBeat, double absLengthBeats, int pitch) const;
    double currentBeatsPerBar() const;
    double currentGridBeats() const; // viewState_.divisionBeats(currentBeatsPerBar())
    double snappedBeatAt(double rawBeat) const;
    // Clamps a clip-relative [start, start+length) span into [0, clip->lengthBeats) — "notes can
    // only exist inside the clip" (TL5-8 design). TimelineDoc itself has no upper clamp (only
    // startBeat >= 0), so this is the editor's own policy, applied before every doc write.
    void clampToClipWindow(double& start, double& length) const;

    // Effective (possibly mid-drag) clip-relative geometry / velocity for one note — read by
    // paint() and by commit-on-mouseUp, exactly like TimelineClipLaneArea::effectiveGeometryFor.
    struct NoteGeometry {
        double startBeat = 0.0;
        double lengthBeats = 0.0;
        int pitch = 0;
        int velocity = 100;
    };
    NoteGeometry effectiveGeometryFor(const synth::MidiNote& note) const;

    void beginDraw(juce::Point<int> pos);
    void beginMoveOrResize(const NoteHit& hit, juce::Point<int> pos);
    void beginVelocityScrub(juce::Point<int> pos);
    void beginMarquee(juce::Point<int> anchor, bool additive);
    void updateMarquee(juce::Point<int> current);
    void endMarquee();

    void performQuantise();
    void requestClose();

    void paintKeysColumn(juce::Graphics& g);
    void paintHeader(juce::Graphics& g);
    void paintGrid(juce::Graphics& g);
    void paintNote(juce::Graphics& g, const synth::MidiNote& note);
    void paintMarquee(juce::Graphics& g);

    TimelineViewState& viewState_;
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

    // ---- Draw preview (pencil-by-default gesture; see class comment) ----
    double drawStartBeat_ = 0.0; // clip-relative, fixed anchor
    int drawPitch_ = 60;
    double drawLength_ = kMinNoteLengthBeats;

    // ---- Move preview (one or many notes, one shared snapped beat delta + semitone delta) ----
    std::vector<NoteOrigin> dragNotes_;
    double previewDeltaBeats_ = 0.0;
    int previewDeltaPitch_ = 0;

    // ---- Resize preview (always the single grabbed note) ----
    double resizeOriginalLength_ = 0.0;
    double previewLength_ = 0.0;

    // ---- Velocity-scrub preview (one or many notes, one shared delta) ----
    int previewDeltaVelocity_ = 0;

    // ---- Marquee (Shift+drag on empty grid only — see mouseDown's comment) ----
    juce::Point<int> marqueeAnchor_;
    juce::Rectangle<int> marqueeRect_;
    bool marqueeAdditive_ = false;
    std::vector<synth::NoteId> marqueeBaseSelection_;

    juce::Rectangle<int> backButtonBounds_;
    juce::Rectangle<int> quantiseButtonBounds_;
    juce::Rectangle<int> keysColumnBounds_;
    juce::Rectangle<int> noteGridBounds_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollComponent)
};

} // namespace synth::ui
