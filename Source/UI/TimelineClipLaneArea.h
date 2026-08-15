#pragma once

#include "../Timeline/TimelineDoc.h"
#include "ClipSelectionModel.h"
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

// TimelineClipLaneArea — TL5-7: the clip-lane region of the timeline panel (below the ruler,
// filling TimelinePanelComponent::getLanesBounds() minus the ruler strip), with drag/trim/split/
// duplicate and marquee selection.
//
// Deliberately NOT a TimelineDoc::Listener itself: TimelinePanelComponent is already the doc's one
// listener (TL5-3), and its timelineChanged() routes a refreshFromDoc() call in here — the same
// "one listener, several owners react" shape the header column uses. setTimelineDoc() only stores
// the pointer and runs that same refresh once, so constructing this directly against a doc (as the
// tests below do, with no panel at all) is also fully functional.
//
// Non-owning refs/pointers, same null-safety contract as every other timeline sub-component:
//   - TimelineViewState& — owned by TimelinePanelComponent (or a test), shared by reference so
//     beat<->pixel mapping agrees with the ruler and the grid.
//   - ClipSelectionModel& — owned by TimelinePanelComponent (getClipSelection()), same reasoning.
//   - TimelineDoc* / AppUndoManager* / TransportService* — setters, may be null (a
//     SYNTH_ENABLE_TIMELINE=OFF build, or before MainComponent finishes wiring); every interaction
//     that needs one degrades to "read but don't mutate" or "mutate without an undo step" rather
//     than crashing.
//
// Rendering, one paint() pass per repaint (never a per-tick timer — see CLAUDE.md): every track in
// doc order gets one row (Metrics::timelineTrackRowHeight, shared with TimelineTrackHeaderComponent
// so header rows and clip rows never drift apart — see that header's kRowHeight comment), each clip
// on it a rounded rect in its track's resolved colour, a name when wide enough, and a thin
// pitch-mapped note preview when wider still. The panel paints the bar/beat grid directly
// (TimelinePanelComponent::paint(), unchanged) and adds this component as a child positioned over
// exactly that same rect — JUCE paints a parent before its children, so clips land above the grid
// for free; playhead_ is added AFTER this component in the panel's constructor, so it stays
// topmost. That is the ONE relocation this task makes: no second place ever paints the grid.
//
// Interactions mirror two existing idioms rather than inventing new ones:
//   - The deferred-empty-click trick (GraphEditor.cpp's pendingEmptyCanvasClick, see the comment
//     above GraphEditor::mouseDown/mouseUp) for "click empty lane space deselects, but a press that
//     turns into a drag must not" — here a plain (non-Shift) drag-from-empty becomes a marquee
//     (there is no drag-to-pan in the lane area; scrolling is wheel-only per TL5-2).
//   - Every edit previews locally (a member offset/length, read back in paint() via
//     effectiveGeometryFor()) and commits to the doc exactly once on mouseUp, through
//     AppUndoManager::recordTimelineChange — so a multi-clip move or a multi-clip Delete is ONE
//     undo step however many clips it touches, the same contract GraphEditor::deleteSelection()
//     and dragSelectionBy()/finalizeSelectionDrag() keep for modules.
namespace synth::ui {

class TimelineClipLaneArea : public juce::Component {
public:
    TimelineClipLaneArea(TimelineViewState& viewState, ClipSelectionModel& selection);
    ~TimelineClipLaneArea() override = default;

    void paint(juce::Graphics& g) override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    // TL5-8: double-clicking a clip opens the piano roll for it. Fires onClipDoubleClicked (if
    // set) with the hit clip's id; a double-click on empty lane space is a no-op.
    void mouseDoubleClick(const juce::MouseEvent& e) override;

    // Non-owning callback; may be unset. TimelinePanelComponent wires this to
    // PianoRollComponent::openClip via its own openPianoRoll(ClipId).
    std::function<void(synth::ClipId)> onClipDoubleClicked;

    // Panel-scoped Delete/Escape (see GraphEditor's identical idiom). Grabs focus on mouseDown, so
    // pressing Delete right after a click lands here rather than on whichever panel had focus
    // before. Returns false when there is nothing to act on so the key falls through — TL5-10
    // formalises cross-panel key arbitration; this is only the local half of it.
    bool keyPressed(const juce::KeyPress& key) override;

    // Non-owning; may be null. Runs one refresh (see class comment) — the same thing
    // TimelinePanelComponent::timelineChanged() calls on every subsequent doc notification.
    void setTimelineDoc(synth::TimelineDoc* doc);
    synth::TimelineDoc* getTimelineDoc() const noexcept { return doc_; }

    // Non-owning; may be null (no undo manager -> mutations apply directly, uncommitted to any
    // undo stack — the same degrade-gracefully contract TimelineTrackHeaderComponent::performEdit
    // uses when built without a host).
    void setUndoManager(AppUndoManager* undoManager) noexcept { undoManager_ = undoManager; }
    AppUndoManager* getUndoManager() const noexcept { return undoManager_; }

    // Non-owning; may be null. Only consulted for its current time signature (beatsPerBar, for
    // Snap::Bar) — the same reasoning TimelinePanelComponent::paint()'s grid loop already uses.
    void setTransport(synth::TransportService* transport) noexcept { transport_ = transport; }

    // Re-derives the doc-backed truth: prunes the selection of any clip id that no longer exists
    // (synth::ui::ClipSelectionModel::retainOnly) and repaints. THE refresh seam — called once by
    // setTimelineDoc() and, thereafter, by TimelinePanelComponent::timelineChanged() on every
    // effective doc mutation. No timer anywhere in this class.
    void refreshFromDoc();

    // ---- Context-menu hook (TL5-3's "showMenuAsync doesn't run headless" idiom) ----
    enum class ClipContextChoice { SplitAtPointer, Duplicate, Delete };

    // Applies one context-menu choice. `pointerBeat` is in absolute (doc) beats, UNSNAPPED — the
    // split case snaps it internally against the current view-state snap + beatsPerBar, exactly
    // like showClipContextMenu()'s own enablement check, so a test driving this directly observes
    // the same snapping the real right-click menu would. One recordTimelineChange mutation, same
    // as every other doc-mutating gesture here.
    void applyClipContextChoice(synth::ClipId id, ClipContextChoice choice, double pointerBeat);

    // ---- Pure geometry (no doc, no component state) — what GeometryMapsBeatsAndRows tests ----
    // The clip rect for a known view state / track row / beat span. `rowHeight` is passed in
    // rather than read from a theme so this stays callable with no LookAndFeel installed at all.
    static juce::Rectangle<int> computeClipRect(const TimelineViewState& viewState, int trackIndex, double startBeat,
                                                double lengthBeats, int rowHeight);

    // The row height this instance currently lays out at: themed Metrics::timelineTrackRowHeight
    // with TimelineTrackHeaderComponent::kRowHeight as the headless fallback (see that constant's
    // comment) — the same dynamic_cast<AppLookAndFeel*> pattern every other timeline component uses.
    int getRowHeight() const;

    // The live rect for a clip id, using its CURRENT doc geometry (never a mid-drag preview) —
    // what tests use to compute where to synthesize a mouse event. Returns an empty rect if the id
    // does not resolve (doc null, or no such clip).
    juce::Rectangle<int> getClipRect(synth::ClipId id) const;

    bool isMarqueeActiveForTest() const noexcept { return dragMode_ == DragMode::Marquee; }

private:
    enum class DragMode { None, Move, ResizeLeft, ResizeRight, Marquee };

    struct ClipHit {
        synth::ClipId id;
        juce::Rectangle<int> rect;
        enum class Zone { Body, LeftEdge, RightEdge } zone = Zone::Body;
    };

    struct Geometry {
        double start = 0.0;
        double length = 0.0;
    };

    // One dragged clip's ORIGIN (pre-drag) geometry — every preview/commit computation reads from
    // this, never from the accumulating pointer position, so rounding never accumulates frame to
    // frame (same reasoning as GraphEditor::dragSelectionBy's comment).
    struct DragOrigin {
        synth::ClipId id;
        double originalStart = 0.0;
        double lengthBeats = 0.0;
    };

    std::optional<ClipHit> hitTestClip(juce::Point<int> pos) const;
    std::vector<std::pair<synth::ClipId, juce::Rectangle<int>>> collectClipRects() const;
    Geometry effectiveGeometryFor(const synth::Clip& clip) const;
    double currentBeatsPerBar() const;
    double snappedBeatAt(double rawBeat) const;

    void beginMarquee(juce::Point<int> anchor, bool additive);
    void updateMarquee(juce::Point<int> current);
    void endMarquee();

    void showClipContextMenu(synth::ClipId id, juce::Point<int> localPos);
    void paintClip(juce::Graphics& g, const synth::Clip& clip, const synth::Track& track, int trackIndex,
                   int rowHeight);
    void paintMarquee(juce::Graphics& g);

    TimelineViewState& viewState_;
    ClipSelectionModel& selection_;
    synth::TimelineDoc* doc_ = nullptr;
    AppUndoManager* undoManager_ = nullptr;
    synth::TransportService* transport_ = nullptr;

    DragMode dragMode_ = DragMode::None;
    synth::ClipId activeClip_;
    juce::Point<int> mouseDownPos_;

    // Deferred-deselect (see class comment): a plain press on empty lane space that never becomes
    // a drag clears the selection on mouseUp; one that DOES move promotes to a marquee instead.
    bool pendingEmptyClick_ = false;

    // ---- Move preview (one or many clips, one shared snapped delta) ----
    std::vector<DragOrigin> dragClips_;
    double previewDeltaBeats_ = 0.0;

    // ---- Resize preview (always the single grabbed clip, even inside a wider selection) ----
    double resizeOriginalStart_ = 0.0;
    double resizeOriginalLength_ = 0.0;
    double previewStart_ = 0.0;  // left-edge trim only (end stays fixed)
    double previewLength_ = 0.0; // both edges

    // ---- Marquee ----
    juce::Point<int> marqueeAnchor_;
    juce::Rectangle<int> marqueeRect_;
    bool marqueeAdditive_ = false;
    std::vector<synth::ClipId> marqueeBaseSelection_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimelineClipLaneArea)
};

} // namespace synth::ui
