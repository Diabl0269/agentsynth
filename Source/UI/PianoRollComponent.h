#pragma once

#include "../Timeline/TimelineDoc.h"
#include "NoteSelectionModel.h"
#include "TimelinePlayheadOverlay.h"
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

protected:
    // THE paint-count seam for the local playhead line, mirroring
    // TimelinePlayheadOverlay::requestRepaintStrip exactly (a test subclasses and counts). Every
    // repaint the playhead costs here goes through it and nowhere else.
    virtual void requestRepaintStrip(juce::Rectangle<int> strip);

private:
    enum class DragMode { None, Move, Resize, Marquee, VelocityScrub };

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
    void beginMoveOrResize(const NoteHit& hit, juce::Point<int> pos);
    void beginVelocityScrub(juce::Point<int> pos);
    void beginMarquee(juce::Point<int> anchor, bool additive);
    void updateMarquee(juce::Point<int> current);
    void endMarquee();

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

    juce::Rectangle<int> backButtonBounds_;
    juce::Rectangle<int> quantiseButtonBounds_;
    juce::Rectangle<int> keysColumnBounds_;
    juce::Rectangle<int> noteGridBounds_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollComponent)
};

} // namespace synth::ui
