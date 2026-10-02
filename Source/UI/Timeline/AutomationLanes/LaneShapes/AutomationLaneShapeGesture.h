#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Timeline/AutomationLanes/LaneShapes/DrawShape.h"
#include "UI/Timeline/AutomationLanes/LaneShapes/LaneRangeSelection.h"
#include "UI/Timeline/EditTool.h"
#include "UI/Timeline/TimelineViewState.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <vector>

namespace synth::ui {

class AutomationLaneEditor;

// The shape half of one automation lane editor: the Draw tool's box stamp (Sine/Triangle/Saw/Square),
// the Range tool's lane-range drag, and the verbs that act on a lane range (stamp a shape, delete the
// points). Owned by value by its AutomationLaneEditor, which forwards its mouse/key/paint calls here
// first. Every edit is ONE TimelineDoc::editBreakpoints call in ONE undo step. Message thread only.
class AutomationLaneShapeGesture : private juce::Timer {
public:
    AutomationLaneShapeGesture(AutomationLaneEditor& editor, TimelineViewState& viewState);
    ~AutomationLaneShapeGesture() override;

    void setDrawShape(DrawShape shape) noexcept { shape_ = shape; }
    DrawShape getDrawShape() const noexcept { return shape_; }
    /** The shared lane range; non-owning, may be null (no lane ranges then). */
    void setLaneRange(LaneRangeSelection* range) noexcept { laneRange_ = range; }

    // ---- Editor hooks: each returns true when it took the event ----
    /** `followedTool` is the timeline edit tool the editor follows, nullopt when a tool was set directly. */
    bool mouseDown(const juce::MouseEvent& e, std::optional<EditTool> followedTool);
    bool mouseDrag(const juce::MouseEvent& e);
    bool mouseUp(const juce::MouseEvent& e);
    bool keyPressed(const juce::KeyPress& key);
    void paintUnderCurve(juce::Graphics& g);
    void paintOverCurve(juce::Graphics& g);
    /** Drops any gesture in flight, changing nothing. */
    void cancel();
    bool isActive() const noexcept { return mode_ != Mode::None; }

    // ---- Lane-range verbs (no-ops unless the range sits on this editor's lane) ----
    /** Stamps `shape` over the range at the lane's full height; Line ramps between the curve's values at the edges. */
    bool stampOverRange(DrawShape shape);
    /** Removes every point inside the range. */
    bool deleteRangePoints();

    /** The box drag's live chip ("4 cycles · 1 bar"), empty when no box is being drawn. */
    juce::String getChipText() const;
    /** The last refusal shown on the lane ("Too many points..."), empty once it has faded. */
    juce::String getStatusText() const { return statusText_; }

private:
    enum class Mode { None, Box, Range };

    void timerCallback() override;
    double snappedBeatAt(int x) const;
    double clampValue(double value) const;
    double cycleBeats() const;
    double beatsPerBar() const;
    bool commitReplace(double startBeat, double endBeat, std::vector<synth::AutomationLane::Breakpoint> points);
    bool commitStamp(DrawShape shape, double startBeat, double endBeat, double lo, double hi);
    bool refuse(const juce::String& message);
    bool rangeIsOnThisLane() const;
    juce::Rectangle<float> boxRect() const;
    void paintBoxPreview(juce::Graphics& g, juce::Colour accent);
    void paintChip(juce::Graphics& g, const juce::String& text, juce::Rectangle<float> anchor, juce::Colour colour);

    AutomationLaneEditor& editor_;
    TimelineViewState& viewState_;
    LaneRangeSelection* laneRange_ = nullptr;
    DrawShape shape_ = DrawShape::Free;
    Mode mode_ = Mode::None;

    double boxStartBeat_ = 0.0;
    double boxEndBeat_ = 0.0;
    double boxStartValue_ = 0.0;
    double boxEndValue_ = 0.0;
    juce::String statusText_;
};

} // namespace synth::ui
