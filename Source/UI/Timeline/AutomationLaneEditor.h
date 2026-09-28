#pragma once

#include "Timeline/AutomationShapes.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "TimelineViewState.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <set>
#include <vector>

class AppUndoManager; // Forward declaration (Source/AppUndoManager.h)

namespace synth {
class TransportService; // Forward declaration (Source/Transport/TransportService.h)
}

// AutomationLaneEditor — the automation strip's curve canvas, editing ONE synth::AutomationLane
// at a time.
//
// X is the SHARED TimelineViewState (absolute beats) — the exact same beatToX/xToBeat the clip
// lanes and the piano roll use, so the canvas lines up with the playhead pixel-for-pixel. Y maps
// the lane's own RangeSnapshot [min..max] linearly onto the component's height, top = max
// (valueToY/yToValue).
//
// Non-owning TimelineDoc* / AppUndoManager* / TransportService* setters, null-safe. Every gesture
// previews locally (a handful of `preview*_` members, read back in paint()) and commits to the doc
// exactly ONCE on mouse-up, through AppUndoManager::recordTimelineChange — never during mouseDrag:
// one gesture, one doc mutation, one listener fire, one republish. Panel-scoped Escape: clears
// in-flight tool-drag state and returns true; returns false when idle so the key falls through to
// TimelinePanelComponent, which closes the strip.
//
// Five tools (member `tool_`): Pointer (drag a handle to move it; drag a segment to scrub its
// left point's tension; double-click empty space adds a point), Pencil (freehand drag, thinned via
// synth::AutomationRecorder's RDP helper on mouse-up), Line (drag previews a straight line, commits
// as its two snapped endpoints), Eraser (drag deletes every handle touched, in one mutation), Shape
// (drag stamps one of synth::ShapeKind's waveforms — see setShapeKind() below — across the dragged
// span via synth::generateAutomationShape(); period comes from the shared view-state's current
// snap division). Right-click a segment shows Hold/Linear via the headless
// applySegmentCurveChoice() hook (menus don't run in tests); right-click a handle shows Delete
// point.
namespace synth::ui {

class AutomationLaneEditor : public juce::Component {
public:
    enum class Tool { Pointer, Pencil, Line, Eraser, Shape };

    explicit AutomationLaneEditor(TimelineViewState& viewState);
    ~AutomationLaneEditor() override = default;

    void paint(juce::Graphics& g) override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;

    bool keyPressed(const juce::KeyPress& key) override;

    // Non-owning setters; same null-safety contract as every other timeline sub-component.
    void setTimelineDoc(synth::TimelineDoc* doc) noexcept { doc_ = doc; }
    synth::TimelineDoc* getTimelineDoc() const noexcept { return doc_; }
    void setUndoManager(AppUndoManager* undoManager) noexcept { undoManager_ = undoManager; }
    AppUndoManager* getUndoManager() const noexcept { return undoManager_; }
    void setTransport(synth::TransportService* transport) noexcept { transport_ = transport; }

    // Which lane this canvas shows/edits. Invalid (default-constructed) means "nothing to show" —
    // paint() then draws only the grid backdrop. Resets any in-flight drag.
    void setActiveLane(synth::LaneId id) noexcept {
        laneId_ = id;
        dragMode_ = DragMode::None;
        repaint();
    }
    synth::LaneId getActiveLane() const noexcept { return laneId_; }

    void setTool(Tool tool) noexcept { tool_ = tool; }
    Tool getTool() const noexcept { return tool_; }

    // Which waveform the Shape tool stamps in. Takes effect on the NEXT press; changing it
    // mid-drag does not retroactively change an in-flight preview.
    void setShapeKind(synth::ShapeKind kind) noexcept { shapeKind_ = kind; }
    synth::ShapeKind getShapeKind() const noexcept { return shapeKind_; }

    // Track lane row look: a translucent tint of `tint` over the panel grid instead of the strip's
    // opaque backdrop, the curve drawn in `tint`. Off (the default) is the strip look.
    void setTrackLaneStyle(bool enabled, juce::Colour tint);
    // An accent outline marking the focused lane row. Visual only.
    void setHighlighted(bool highlighted);
    bool isHighlighted() const noexcept { return highlighted_; }

    // ---- Headless hooks (juce::PopupMenu::showMenuAsync doesn't run headlessly) ----

    // Toggles the segment whose LEFT point sits at `leftBeat` to `curve` (a BreakpointCurve value).
    // One recordTimelineChange mutation preserving the point's beat/value/tension. A no-op if
    // `leftBeat` doesn't resolve to a real point in the active lane.
    void applySegmentCurveChoice(double leftBeat, int curve);

    // ---- Test hooks ----

    // The handle's on-screen rect for a live breakpoint at `beat`, or an empty rect if it doesn't
    // resolve — what a test uses to compute where to synthesize a mouse event, mirroring
    // TimelineClipLaneArea::getClipRect / PianoRollComponent::getNoteRect.
    juce::Rectangle<int> getHandleRectForTest(double beat) const;
    bool isDragActiveForTest() const noexcept { return dragMode_ != DragMode::None; }

    // Pure value<->y mapping for the active lane's current range (no drag state involved) — what a
    // test uses to place a synthetic mouse event at a target value.
    double valueToY(double value) const;
    double yToValue(double y) const;

private:
    enum class DragMode { None, MoveHandle, TensionScrub, Pencil, Line, Eraser, Shape };

    struct HandleHit {
        double beat = 0.0;
        double value = 0.0;
        float tension = 0.0f;
        int curve = 0;
    };

    // One raw (beat, value) sample captured by the Pencil tool's freehand drag. A local type
    // (rather than reusing synth::AutomationRecorder::CapturedPoint) so this header stays free of
    // an AutomationRecorder include; the .cpp converts before calling its thinPoints().
    struct PencilSample {
        double beat = 0.0;
        double value = 0.0;
    };

    static constexpr float kHandleRadiusPx = 5.0f;
    static constexpr float kHandleHitRadiusPx = 7.0f;

    std::optional<HandleHit> hitTestHandle(juce::Point<int> pos) const;
    // The index of the LEFT point of the segment whose beat range contains the beat under pixel
    // column `x`, or nullopt if there is no such segment (fewer than 2 points, or `x` lands
    // outside the lane's span altogether).
    std::optional<int> hitTestSegmentLeftIndex(int x) const;

    double currentBeatsPerBar() const;
    double snappedBeatAt(double rawBeat) const;
    double clampValue(double value) const;

    // The Shape tool's period: the shared view-state's current snap division at the lane's
    // beats-per-bar, or 1 beat when snap is Off — see the definition for why a UI gesture never
    // hands the generator a zero period itself.
    double shapePeriodBeats() const;
    // The waveform the current shape drag would commit, from the press/drag beats and values
    // recorded so far — read by both the live preview (paintToolPreview) and mouseUp, so the two
    // can never disagree about what a drag produced.
    std::vector<synth::AutomationLane::Breakpoint> buildShapePoints() const;

    // The beats of every EXISTING breakpoint in [loBeat, hiBeat] (inclusive) — a pure read, handed
    // to TimelineDoc::editBreakpoints' removeBeats list so "replace a span" costs exactly one doc
    // mutation (one revision bump), whatever the span contains.
    std::vector<double> collectBeatsInSpan(double loBeat, double hiBeat) const;

    void paintGridBackdrop(juce::Graphics& g);
    void paintCommittedCurve(juce::Graphics& g, const synth::AutomationLane& lane);
    void paintToolPreview(juce::Graphics& g);
    void paintHandles(juce::Graphics& g, const synth::AutomationLane& lane);

    void showHandleContextMenu(double beat);
    void showSegmentContextMenu(int leftIndex);

    TimelineViewState& viewState_;
    synth::TimelineDoc* doc_ = nullptr;
    AppUndoManager* undoManager_ = nullptr;
    synth::TransportService* transport_ = nullptr;

    synth::LaneId laneId_;
    Tool tool_ = Tool::Pointer;
    synth::ShapeKind shapeKind_ = synth::ShapeKind::Sine;
    bool trackLaneStyle_ = false;
    juce::Colour trackLaneTint_;
    bool highlighted_ = false;

    DragMode dragMode_ = DragMode::None;
    juce::Point<int> mouseDownPos_;

    // ---- MoveHandle preview ----
    double dragOriginalBeat_ = 0.0;
    double dragOriginalValue_ = 0.0;
    float dragOriginalTension_ = 0.0f;
    int dragOriginalCurve_ = 0;
    double previewBeat_ = 0.0;
    double previewValue_ = 0.0;

    // ---- TensionScrub preview ----
    double tensionSegLeftBeat_ = 0.0;
    float tensionOriginal_ = 0.0f;
    float previewTension_ = 0.0f;

    // ---- Pencil preview ----
    std::vector<PencilSample> pencilSamples_;

    // ---- Line preview ----
    double lineStartBeat_ = 0.0;
    double lineStartValue_ = 0.0;
    double lineEndBeat_ = 0.0;
    double lineEndValue_ = 0.0;

    // ---- Eraser preview (beats of the handles touched so far this drag) ----
    std::set<double> erasedBeats_;

    // ---- Shape preview ---- press sets the start beat and one extreme value; drag sets the end
    // beat and the other extreme. Order is not normalised until buildShapePoints() reads them, so
    // painting and hit-testing never disagree with what was actually pressed/dragged.
    double shapeStartBeat_ = 0.0;
    double shapePressValue_ = 0.0;
    double shapeEndBeat_ = 0.0;
    double shapeDragValue_ = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutomationLaneEditor)
};

} // namespace synth::ui
