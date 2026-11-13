// AutomationLaneEditor -- the segment bend drag: a press on the line between two dots bends that segment so its
// half-way point follows the pointer, and dragging past the start value's extreme turns it into a hold. The class is
// declared in AutomationLaneEditor.h; the maths is SegmentBendDrag.h.

#include "AppUndoManager.h"
#include "AutomationLaneEditor.h"
#include "UI/Layout/PointEditing/SegmentBendDrag.h"
#include "UI/Timeline/AutomationLanes/PointSelection/LaneHoldBlend.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
constexpr int kHoldCurve = static_cast<int>(synth::BreakpointCurve::Hold);
constexpr int kLinearCurve = static_cast<int>(synth::BreakpointCurve::Linear);
constexpr const char* kHoldHint = "Hold";
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
    bendWasHold_ = left.curve == kHoldCurve;
    bendOriginalCurve_ = bendWasHold_ ? kLinearCurve : left.curve;
    previewHold_ = bendWasHold_;
    bendDrag_ = std::make_unique<LaneBendDrag>(valueToY(left.value), valueToY(right.value), left.tension, bendWasHold_,
                                               (double)pos.y);
    holdBlend_->reset(previewHold_);
}

// A hold keeps the tension it had (+1 when a curve becomes one); the bubble says "Hold" while the step is what plays.
void AutomationLaneEditor::dragBend(const juce::MouseEvent& e) {
    const auto bend = bendDrag_->moveTo((double)e.getPosition().y, e.mods.isShiftDown());
    previewTension_ = bend.tension;
    if (bend.hold != previewHold_) {
        previewHold_ = bend.hold;
        holdBlend_->setHold(previewHold_);
    }
    const auto* lane = doc_->getLane(laneId_);
    const auto it = std::find_if(lane->points.begin(), lane->points.end(),
                                 [this](const LaneBreakpoint& p) { return p.beat == tensionSegLeftBeat_; });
    if (previewHold_ && it != lane->points.end() && it + 1 != lane->points.end()) {
        const double midBeat = (it->beat + (it + 1)->beat) * 0.5;
        bubble_.show({(float)viewState_.beatToX(midBeat), (float)valueToY(it->value)}, kHoldHint);
    } else {
        bubble_.hide();
    }
}

void AutomationLaneEditor::applyBendPreview(std::vector<LaneBreakpoint>& pts, bool asHold) const {
    for (auto& p : pts)
        if (p.beat == tensionSegLeftBeat_) {
            p.tension = previewTension_;
            p.curve = asHold ? kHoldCurve : bendOriginalCurve_;
        }
}

void AutomationLaneEditor::forceBendBlendAnimateForTest(bool force) noexcept { holdBlend_->forceAnimateForTest(force); }

bool AutomationLaneEditor::isBendBlendRunningForTest() const noexcept { return holdBlend_->isRunning(); }

// The first segment as a user sees it: a step, or a curve bulging up, bulging down or straight.
juce::String AutomationLaneEditor::firstSegmentBend() const {
    const auto* points = lanePoints();
    if (points == nullptr || points->size() < 2)
        return "straight";
    const auto& left = (*points)[0];
    if (left.curve == kHoldCurve)
        return "step";
    const double toward = midFractionForTension(left.tension) - 0.5;
    if (std::abs(toward) < 1e-3)
        return "straight";
    const bool rising = (*points)[1].value >= left.value;
    return (toward > 0.0) == rising ? "up" : "down";
}

std::vector<LaneBreakpoint> AutomationLaneEditor::getBendPreviewPointsForTest() const {
    const auto* points = lanePoints();
    std::vector<LaneBreakpoint> pts = points != nullptr ? *points : std::vector<LaneBreakpoint>{};
    applyBendPreview(pts, previewHold_);
    return pts;
}

// While the blend runs both shapes are drawn at their weights, so the switch between a curve and its step is a
// cross-fade; at rest only the shape that will be committed is drawn.
void AutomationLaneEditor::paintBendPreview(juce::Graphics& g, const std::vector<LaneBreakpoint>& pts,
                                            const synth::AutomationLane& lane, juce::Colour colour) {
    if (!holdBlend_->isRunning()) {
        strokeCurve(g, pts, lane, colour, 2.0f);
        return;
    }
    auto asCurve = pts;
    auto asStep = pts;
    applyBendPreview(asCurve, false);
    applyBendPreview(asStep, true);
    strokeCurve(g, asCurve, lane, colour.withMultipliedAlpha(1.0f - holdBlend_->amount()), 2.0f);
    strokeCurve(g, asStep, lane, colour.withMultipliedAlpha(holdBlend_->amount()), 2.0f);
}

// One undo step writes the tension and the curve kind together; a release back where it began writes nothing.
void AutomationLaneEditor::commitBend() {
    const bool unchanged =
        previewHold_ == bendWasHold_ && (previewHold_ || std::abs(previewTension_ - tensionOriginal_) <= 1e-6f);
    bubble_.hide();
    holdBlend_->reset(false);
    if (unchanged)
        return;
    const double beat = tensionSegLeftBeat_;
    const float tension = previewTension_;
    const int curve = previewHold_ ? kHoldCurve : bendOriginalCurve_;
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
