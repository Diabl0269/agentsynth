// AutomationLaneEditor -- the segment bend drag: a press on the line between two dots bends that segment so the curve
// passes under the pointer on every row; dragging to the start row flattens it into a Hold (a step at its right point),
// dragging to the end row into a Jump (a step at its left point). The class is declared in AutomationLaneEditor.h; the
// maths (absolute drag, step zones, tension range +-3) is SegmentBendDrag.h.

#include "AppUndoManager.h"
#include "AutomationLaneEditor.h"
#include "UI/Layout/PointEditing/SegmentBendDrag.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
constexpr int kHoldCurve = static_cast<int>(synth::BreakpointCurve::Hold);
constexpr int kJumpCurve = static_cast<int>(synth::BreakpointCurve::Jump);
constexpr int kLinearCurve = static_cast<int>(synth::BreakpointCurve::Linear);
constexpr const char* kHoldHint = "Hold";
constexpr const char* kJumpHint = "Jump";
} // namespace

// The press records the segment as it is and where its ends are on screen; the doc is not touched until release.
void AutomationLaneEditor::beginBendDrag(int segLeftIndex, juce::Point<int> pos) {
    const auto* lane = doc_->getLane(laneId_);
    const auto& left = lane->points[(size_t)segLeftIndex];
    const auto& right = lane->points[(size_t)segLeftIndex + 1];
    dragMode_ = DragMode::TensionScrub;
    tensionSegLeftBeat_ = left.beat;
    tensionOriginal_ = left.tension;
    previewTension_ = left.tension;
    previewHold_ = left.curve == kHoldCurve;
    previewJump_ = left.curve == kJumpCurve;
    bendOriginalCurve_ = previewHold_ || previewJump_ ? kLinearCurve : left.curve;
    const auto was = previewHold_ ? LaneBendKind::Hold : previewJump_ ? LaneBendKind::Jump : LaneBendKind::Curve;
    // The press's place along the segment's beats: the drag keeps the curve under the pointer at exactly that beat.
    const double beatSpan = right.beat - left.beat;
    const double xp = beatSpan > 0.0 ? (viewState_.xToBeat((double)pos.x) - left.beat) / beatSpan : 0.5;
    bendDrag_ = std::make_unique<LaneBendDrag>(valueToY(left.value), valueToY(right.value), left.tension, was, xp,
                                               (double)pos.y, 0.0, (double)getHeight());
}

// What the pointer's row draws is exactly what a release commits: the curve at its clamped tension, the hold or the
// jump. Nothing eases over time while the drag runs: a pointer that stands still draws a still line, and the step zones
// in LaneBendDrag make crossing into a step a change of one pixel's share. The bubble names the step while it lasts;
// its state comes from the same hysteresis as the step, so a jitter on the end row never flips it.
void AutomationLaneEditor::dragBend(const juce::MouseEvent& e) {
    const auto bend = bendDrag_->moveTo((double)e.getPosition().y, e.mods.isShiftDown());
    previewTension_ = bend.tension;
    previewHold_ = bend.hold;
    previewJump_ = bend.jump;
    showBendStepBubble();
}

// A Hold's bubble sits on the start value, a Jump's on the end value: each on the flat it names.
void AutomationLaneEditor::showBendStepBubble() {
    const auto* lane = doc_->getLane(laneId_);
    const auto it = std::find_if(lane->points.begin(), lane->points.end(),
                                 [this](const LaneBreakpoint& p) { return p.beat == tensionSegLeftBeat_; });
    if ((previewHold_ || previewJump_) && it != lane->points.end() && it + 1 != lane->points.end()) {
        const double midBeat = (it->beat + (it + 1)->beat) * 0.5;
        const double value = previewHold_ ? it->value : (it + 1)->value;
        bubble_.show({(float)viewState_.beatToX(midBeat), (float)valueToY(value)},
                     previewHold_ ? kHoldHint : kJumpHint);
    } else {
        bubble_.hide();
    }
}

void AutomationLaneEditor::applyBendPreview(std::vector<LaneBreakpoint>& pts) const {
    for (auto& p : pts)
        if (p.beat == tensionSegLeftBeat_) {
            p.tension = previewTension_;
            p.curve = previewHold_ ? kHoldCurve : previewJump_ ? kJumpCurve : bendOriginalCurve_;
        }
}

// The first segment as a user sees it: a step, or a curve bulging up, bulging down or straight.
juce::String AutomationLaneEditor::firstSegmentBend() const {
    const auto* points = lanePoints();
    if (points == nullptr || points->size() < 2)
        return "straight";
    const auto& left = (*points)[0];
    if (left.curve == kHoldCurve)
        return "step";
    if (left.curve == kJumpCurve)
        return "jump";
    const double toward = midFractionForTension(left.tension) - 0.5;
    if (std::abs(toward) < 1e-3)
        return "straight";
    const bool rising = (*points)[1].value >= left.value;
    return (toward > 0.0) == rising ? "up" : "down";
}

std::vector<LaneBreakpoint> AutomationLaneEditor::getBendPreviewPointsForTest() const {
    const auto* points = lanePoints();
    std::vector<LaneBreakpoint> pts = points != nullptr ? *points : std::vector<LaneBreakpoint>{};
    applyBendPreview(pts);
    return pts;
}

juce::String AutomationLaneEditor::firstSegmentRows() const {
    const auto* points = lanePoints();
    if (points == nullptr || points->size() < 2)
        return {};
    return juce::String((int)std::lround(valueToY((*points)[0].value))) + " " +
           juce::String((int)std::lround(valueToY((*points)[1].value)));
}

// One undo step writes the tension and the curve kind together; a release back where it began writes nothing. The
// release commits exactly the last drawn shape (previewTension_, previewHold_): nothing is decided at the release.
void AutomationLaneEditor::commitBend() {
    // Unchanged only when the last drawn frame is the shape the segment had: same kind, same tension.
    const int curve = previewHold_ ? kHoldCurve : previewJump_ ? kJumpCurve : bendOriginalCurve_;
    int originalCurve = curve;
    if (const auto* lane = doc_->getLane(laneId_))
        for (const auto& bp : lane->points)
            if (bp.beat == tensionSegLeftBeat_)
                originalCurve = bp.curve;
    const bool unchanged = curve == originalCurve && std::abs(previewTension_ - tensionOriginal_) <= 1e-6f;
    bubble_.hide();
    if (unchanged)
        return;
    const double beat = tensionSegLeftBeat_;
    const float tension = previewTension_;
    const auto laneId = laneId_;
    double value = 0.0;
    if (const auto* lane = doc_->getLane(laneId_))
        for (const auto& bp : lane->points)
            if (bp.beat == beat)
                value = bp.value;
    auto mutate = [this, laneId, beat, value, tension, curve] {
        doc_->addBreakpoint(laneId, beat, value, tension, curve);
    };
    if (undoManager_)
        undoManager_->recordTimelineChange(*doc_, mutate);
    else
        mutate();
}

} // namespace synth::ui
