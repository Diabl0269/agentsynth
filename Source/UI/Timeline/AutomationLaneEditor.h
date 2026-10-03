#pragma once

#include "AutomationLanes/LaneShapes/AutomationLaneShapeGesture.h"
#include "EditTool.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "TimelineViewState.h"
#include "UI/Timeline/AutomationLanes/PointReadout/PointValueBubble.h"
#include "UI/Timeline/AutomationLanes/PointReadout/PointValueField.h"
#include "UI/Timeline/AutomationLanes/PointSelection/LanePointEdits.h"
#include "UI/Timeline/AutomationLanes/PointSelection/LanePointGlide.h"
#include "UI/Timeline/AutomationLanes/PointSelection/LanePointSelection.h"
#include "UI/Timeline/AutomationLanes/PointSelection/LanePointStretch.h"
#include <cstdint>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <set>
#include <vector>

class AppUndoManager; // Forward declaration (Source/AppUndoManager.h)

namespace synth {
class TransportService; // Forward declaration (Source/Transport/TransportService.h)
}

// AutomationLaneEditor — the curve canvas of one automation lane row (folded out under its track in
// the timeline panel), editing ONE synth::AutomationLane at a time.
//
// X is the SHARED TimelineViewState (absolute beats) — the exact same beatToX/xToBeat the clip
// lanes and the piano roll use, so the canvas lines up with the playhead pixel-for-pixel. Y maps
// the lane's own RangeSnapshot [min..max] linearly onto the component's height, top = max
// (valueToY/yToValue).
//
// Non-owning TimelineDoc* / AppUndoManager* / TransportService* setters, null-safe. Every gesture
// previews locally (a handful of `preview*_` members, read back in paint()) and commits to the doc
// exactly ONCE on mouse-up, through AppUndoManager::recordTimelineChange — never during mouseDrag:
// one gesture, one doc mutation, one listener fire, one republish. Escape clears in-flight tool-drag
// state and returns true; returns false when idle so the key falls through to the panel.
//
// Four tools (member `tool_`), normally chosen by the timeline's edit tool (setEditTool): Pointer (drag a handle to
// move it; drag a segment to scrub its left point's tension; double-click empty space adds a point), Pencil (freehand
// drag, thinned via synth::AutomationRecorder's RDP helper on mouse-up), Line (drag previews a straight line, commits
// as its two snapped endpoints), Eraser (drag deletes every handle touched, in one mutation).
// Pointer tool: the point under the pointer, or being dragged, shows a value bubble above it (text from
// valueToText); a point shows the grab cursor; pressing the flat line of a lane with NO points and dragging
// vertically sets the lane's constant value, committed once on mouse-up.
// Points select like clips and notes (LanePointSelection): click selects, Shift/Cmd-click toggles, a plain drag on
// empty space draws a box, Escape clears; dragging a selected point moves the whole selection, Delete/Backspace
// removes it, arrows nudge it (Alt+Left/Right steps the keyboard cursor point), each one undo step. Cmd+A/C/X/V reach
// it as the app's edit commands (MainComponent routes them to the focused lane editor).
// With two or more points selected a stretch box with four edge handles (LanePointStretch) surrounds them: the side
// handles scale the selection's beats about the opposite edge (the edge pushes the next unselected points along), the
// top and bottom ones scale its values; Alt+Shift+Left/Right/Up/Down do the same from the keyboard. The drag previews
// and commits once on mouse-up, Escape cancels.
// Double-clicking a point (or Return with the keyboard cursor on one) opens a PointValueField beside it: type a value,
// Return sets that one point's value (clamped to the lane's range, one undo step), Escape cancels.
// Right-click a segment shows Hold/Linear via the headless applySegmentCurveChoice() hook (menus
// don't run in tests); right-click a handle shows Delete point.
namespace synth::ui {

class AutomationLaneEditor
    : public juce::Component
    , public juce::SettableTooltipClient {
public:
    enum class Tool { Pointer, Pencil, Line, Eraser };

    explicit AutomationLaneEditor(TimelineViewState& viewState);
    ~AutomationLaneEditor() override = default;

    void paint(juce::Graphics& g) override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(juce::Component::FocusChangeType cause) override;
    void focusLost(juce::Component::FocusChangeType cause) override;

    /** Text for a lane value in the parameter's own units ("-6.0 dB"); may be null or return empty, which falls
     *  back to the plain number. */
    std::function<juce::String(double)> valueToText;

    /** The lane value a typed text stands for, in the lane's own units, or nullopt when it is not a value; may be
     *  null, which parses a plain number. */
    std::function<std::optional<double>(const juce::String&)> textToValue;
    /** The lane's parameter name for the value field's accessible title; may be null or return empty. */
    std::function<juce::String()> laneLabel;

    /** Fired when this editor takes keyboard focus; may be null. */
    std::function<void()> onFocused;

    // Non-owning setters; same null-safety contract as every other timeline sub-component.
    void setTimelineDoc(synth::TimelineDoc* doc) noexcept {
        doc_ = doc;
        syncedRevision_ = -1;
    }
    synth::TimelineDoc* getTimelineDoc() const noexcept { return doc_; }
    void setUndoManager(AppUndoManager* undoManager) noexcept { undoManager_ = undoManager; }
    AppUndoManager* getUndoManager() const noexcept { return undoManager_; }
    void setTransport(synth::TransportService* transport) noexcept { transport_ = transport; }
    synth::TransportService* getTransport() const noexcept { return transport_; }

    // Which lane this canvas shows/edits. Invalid (default-constructed) means "nothing to show" —
    // paint() then draws only the grid backdrop. Resets any in-flight drag.
    void setActiveLane(synth::LaneId id);
    synth::LaneId getActiveLane() const noexcept { return laneId_; }

    // Picks the tool directly and stops following the timeline's edit tool.
    void setTool(Tool tool);
    Tool getTool() const noexcept { return tool_; }
    // Follows the timeline's edit tool (automationToolFor), re-read at every mouse-down for Shift.
    void setEditTool(EditTool tool);
    // What the Draw tool puts down (AutomationLaneShapeGesture): the pen, a line, or a stamped shape.
    void setDrawShape(DrawShape shape) noexcept;
    DrawShape getDrawShape() const noexcept { return shapeGesture_.getDrawShape(); }
    // The shared lane range (non-owning, may be null) and this lane's shape/range gestures.
    void setLaneRange(LaneRangeSelection* range) noexcept { shapeGesture_.setLaneRange(range); }
    AutomationLaneShapeGesture& getShapeGesture() noexcept { return shapeGesture_; }
    // The curve's and points' colour; unset = the theme's modWire. Drawn pushed to a readable contrast on the lane
    // background.
    void setCurveColour(juce::Colour colour);
    // The colour the curve and points are painted in right now (see setCurveColour).
    juce::Colour getResolvedCurveColour() const;

    // The grab hand over a point (Pointer tool), the pen while the Draw tool is the active tool (the plain arrow
    // otherwise), like the piano roll's velocity strip.
    juce::MouseCursor getMouseCursor() override;
    void lookAndFeelChanged() override;
    // The stretch handles' hint while the pointer is over one, the editor's own hint elsewhere.
    juce::String getTooltip() override;

    // ---- Point selection (AutomationLaneEditorPoints.cpp) ----
    LanePointSelection& getPointSelection() noexcept { return selection_; }
    const LanePointSelection& getPointSelection() const noexcept { return selection_; }
    // Fired after the selection or the keyboard cursor changed; may be null.
    std::function<void()> onSelectionChanged;
    // Re-reads the lane after any doc change: drops selected points that are gone and glides removals/additions.
    void laneDocChanged();
    // The clipboard Cmd+C/X/V use (non-owning, may be null); the lane pool owns it so it outlives this editor.
    void setClipboard(LanePointClipboard* clipboard) noexcept { clipboard_ = clipboard; }
    // The app's edit commands, each false when it had nothing to act on.
    bool selectAllPoints();
    bool copySelectedPoints();
    bool cutSelectedPoints();
    bool canPastePoints() const;
    // Pastes at the transport position (snapped), or at `beat` (snapped); the pasted points become the selection.
    bool pasteAtPlayhead();
    bool pasteAtBeat(double beat);
    bool deleteSelectedPoints();

    // ---- Stretch box (AutomationLaneEditorStretch.cpp) ----
    // The padded box around the selection (empty unless two or more points are selected under the Pointer tool).
    juce::Rectangle<float> getStretchBoxForTest() const { return stretchBox(); }
    juce::Rectangle<float> getStretchHandleRectForTest(StretchHandle handle) const;
    const LanePointStretch& getStretchForTest() const noexcept { return stretch_; }

    // ---- Headless hooks (juce::PopupMenu::showMenuAsync doesn't run headlessly) ----

    // Toggles the segment whose LEFT point sits at `leftBeat` to `curve` (a BreakpointCurve value).
    // One recordTimelineChange mutation preserving the point's beat/value/tension. A no-op if
    // `leftBeat` doesn't resolve to a real point in the active lane.
    void applySegmentCurveChoice(double leftBeat, int curve);

    // ---- Test hooks ----

    // The typed-value field beside a point.
    PointValueField& getValueFieldForTest() noexcept { return valueField_; }

    // The value bubble over the hovered or dragged point.
    PointValueBubble& getPointBubbleForTest() noexcept { return bubble_; }

    // The handle's on-screen rect for a live breakpoint at `beat`, or an empty rect if it doesn't
    // resolve — what a test uses to compute where to synthesize a mouse event, mirroring
    // TimelineClipLaneArea::getClipRect / PianoRollComponent::getNoteRect.
    juce::Rectangle<int> getHandleRectForTest(double beat) const;
    // How many point handles are drawn at the current zoom (crowded ones are left out).
    int visibleHandleCountForTest() const;
    bool isDragActiveForTest() const noexcept { return dragMode_ != DragMode::None || shapeGesture_.isActive(); }

    // Pure value<->y mapping for the active lane's current range (no drag state involved) — what a
    // test uses to place a synthetic mouse event at a target value.
    double valueToY(double value) const;
    double yToValue(double y) const;

private:
    enum class DragMode { None, MoveHandle, TensionScrub, Pencil, Line, Eraser, LaneConstant, Stretch };

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
    // True when `pos` is within a handle radius of the curve itself (where a press scrubs a segment's tension).
    bool onCurve(juce::Point<int> pos) const;
    // The index of the LEFT point of the segment whose beat range contains the beat under pixel
    // column `x`, or nullopt if there is no such segment (fewer than 2 points, or `x` lands
    // outside the lane's span altogether).
    std::optional<int> hitTestSegmentLeftIndex(int x) const;

    double currentBeatsPerBar() const;
    double snappedBeatAt(double rawBeat) const;
    double clampValue(double value) const;

    // The beats of every EXISTING breakpoint in [loBeat, hiBeat] (inclusive) — a pure read, handed
    // to TimelineDoc::editBreakpoints' removeBeats list so "replace a span" costs exactly one doc
    // mutation (one revision bump), whatever the span contains.
    std::vector<double> collectBeatsInSpan(double loBeat, double hiBeat) const;

    void paintGridBackdrop(juce::Graphics& g);
    void paintCommittedCurve(juce::Graphics& g, const synth::AutomationLane& lane);
    void paintToolPreview(juce::Graphics& g);
    void paintHandles(juce::Graphics& g, const synth::AutomationLane& lane);
    void strokeCurve(juce::Graphics& g, const std::vector<LaneBreakpoint>& points, const synth::AutomationLane& lane,
                     juce::Colour colour, float thickness) const;
    std::vector<juce::Point<float>> handleScreenPositions(const synth::AutomationLane& lane) const;

    // Hover/bubble: follows the point under `pos` (none = hidden), and floats over (beat, value) with its text.
    void updateHover(juce::Point<int> pos);
    void showBubbleAt(double beat, double value);
    juce::String valueText(double value) const;
    // True when `pos` is on the flat line of the active lane while it has no points.
    bool onFlatLine(juce::Point<int> pos) const;

    // ---- Selection glue (AutomationLaneEditorPoints.cpp) ----
    const std::vector<LaneBreakpoint>* lanePoints() const;
    LanePointMapper pointMapper() const;
    LaneEditTarget editTarget() const;
    void grabPoint(const HandleHit& hit);
    std::vector<LaneBreakpoint> dragMovedPoints() const;
    void commitPointMove();
    double gridStepBeats() const;
    bool nudgePoints(double beatDirection, double valueDirection, bool coarse);
    bool stepCursor(int direction);
    bool handleSelectionKey(const juce::KeyPress& key);
    void selectionChanged();
    void syncToDoc();
    void selectOnly(const LaneBreakpoint& point);
    juce::String describePoints() const;
    void refreshDescription();

    // ---- Stretch (AutomationLaneEditorStretch.cpp) ----
    bool stretchBoxVisible() const;
    juce::Rectangle<float> stretchBox() const;
    StretchHandle stretchHandleAt(juce::Point<int> pos) const;
    bool beginStretchAt(juce::Point<int> pos);
    void dragStretch(juce::Point<int> pos);
    void commitStretch();
    void cancelStretch();
    bool stretchKey(const juce::KeyPress& key);
    void commitStretchResult(const std::vector<LaneBreakpoint>& original, const StretchResult& result);
    void paintStretchBox(juce::Graphics& g);
    void trackStretchHover(juce::Point<int> pos);
    void refreshStretchBox();
    std::optional<juce::MouseCursor> stretchCursor() const;
    double pushGapBeats() const;

    // ---- Typed value (AutomationLaneEditorValueField.cpp) ----
    bool openValueField(double beat);
    bool openValueFieldOnCursor();
    std::optional<double> parseTypedValue(const juce::String& text) const;
    void commitTypedValue(double beat, double value);
    void closeValueFieldIfPointGone();

    void showHandleContextMenu(double beat);
    void showSegmentContextMenu(int leftIndex);

    TimelineViewState& viewState_;
    synth::TimelineDoc* doc_ = nullptr;
    AppUndoManager* undoManager_ = nullptr;
    synth::TransportService* transport_ = nullptr;

    synth::LaneId laneId_;
    Tool tool_ = Tool::Pointer;
    std::optional<EditTool> editTool_; // set = tool_ follows it (see setEditTool)
    std::optional<juce::Colour> curveColour_;
    juce::MouseCursor penCursor_;
    bool penCursorBuilt_ = false;

    DragMode dragMode_ = DragMode::None;
    juce::Point<int> mouseDownPos_;

    // ---- MoveHandle preview: the grabbed point's beat/value, and every selected point as it was when grabbed ----
    std::vector<LaneBreakpoint> dragPoints_;
    double dragOriginalBeat_ = 0.0;
    double dragOriginalValue_ = 0.0;
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
    std::optional<double> hoveredBeat_; // the handle under the pointer, always drawn

    AutomationLaneShapeGesture shapeGesture_{*this, viewState_};
    PointValueBubble bubble_{*this};
    PointValueField valueField_{*this};
    std::optional<double> valueFieldBeat_; // the point the field is editing
    LanePointSelection selection_{*this};
    LanePointStretch stretch_{*this};
    StretchHandle hoverHandle_ = StretchHandle::None;
    double stretchGrabBeat_ = 0.0;
    double stretchGrabValue_ = 0.0;
    LanePointGlide glide_{*this};
    LanePointClipboard* clipboard_ = nullptr;
    std::int64_t syncedRevision_ = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutomationLaneEditor)
};

} // namespace synth::ui
