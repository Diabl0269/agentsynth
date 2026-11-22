// AutomationLaneEditor -- the segment bend drag: a press on the line between two dots bends that segment so the curve
// passes under the pointer on every row, and dragging to the start row flattens it into a hold (a step). The class is
// declared in AutomationLaneEditor.h; the maths (absolute drag, step zone, tension range +-3) is SegmentBendDrag.h.

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
    // The press's place along the segment's beats: the drag keeps the curve under the pointer at exactly that beat.
    const double beatSpan = right.beat - left.beat;
    const double xp = beatSpan > 0.0 ? (viewState_.xToBeat((double)pos.x) - left.beat) / beatSpan : 0.5;
    bendDrag_ = std::make_unique<LaneBendDrag>(valueToY(left.value), valueToY(right.value), left.tension, bendWasHold_,
                                               xp, (double)pos.y, 0.0, (double)getHeight());
    bendSettling_ = false;
    holdBlend_->reset(previewHold_);
}

// A hold keeps the tension it had (the maximum when a curve becomes one); the bubble says "Hold" while the step is what
// plays. What the pointer drew is exactly what a release commits: the curve at its clamped tension, or the hold.
// Crossing into or out of the hold the blend eases the drawn line between the two over time (at once under Reduce
// Motion), so the switch never pops, and it always eases toward the shape that is committed.
void AutomationLaneEditor::dragBend(const juce::MouseEvent& e) {
    const auto bend = bendDrag_->moveTo((double)e.getPosition().y, e.mods.isShiftDown());
    previewTension_ = bend.tension;
    previewHold_ = bend.hold;
    holdBlend_->setHold(bend.hold);
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

float AutomationLaneEditor::bendToHoldForTest() const noexcept { return holdBlend_->amount(); }

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

// While the blend glides across the switch the segment is drawn as its curve flattened `amount` of the way onto the
// step's start value: one line that morphs into the step and back. At rest only the shape that will be committed is
// drawn.
void AutomationLaneEditor::paintBendPreview(juce::Graphics& g, const std::vector<LaneBreakpoint>& pts,
                                            const synth::AutomationLane& lane, juce::Colour colour) {
    const float amount = holdBlend_->amount();
    if (amount <= 0.0f || (amount >= 1.0f && previewHold_)) {
        strokeCurve(g, pts, lane, colour, 2.0f);
        return;
    }
    auto asCurve = pts;
    applyBendPreview(asCurve, false);
    const BendMorph morph{tensionSegLeftBeat_, amount};
    strokeCurve(g, asCurve, lane, colour, 2.0f, &morph);
}

// One undo step writes the tension and the curve kind together; a release back where it began writes nothing. The
// release commits exactly the last drawn shape (previewTension_, previewHold_): nothing is decided at the release.
void AutomationLaneEditor::commitBend() {
    // Unchanged only when the last drawn frame is the shape the segment had: same kind, same tension.
    const bool unchanged = previewHold_ == bendWasHold_ && std::abs(previewTension_ - tensionOriginal_) <= 1e-6f;
    bubble_.hide();
    // Let go while the blend is still gliding: it finishes onto the shape that commits (the one it was heading for).
    bendSettling_ = holdBlend_->isRunning();
    if (bendSettling_)
        holdBlend_->setHold(previewHold_);
    else
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
