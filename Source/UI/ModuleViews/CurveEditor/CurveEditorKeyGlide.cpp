// CurveEditorComponent -- the keyboard drag: a held Alt+arrow or Up/Down glides the selection and is one gesture
// (one undo step) from the first real move to the key-up; a tap or a snapped step eases the drawn points. The class is
// declared in CurveEditorComponent.h.

#include "CurveEditorComponent.h"
#include "UI/Timeline/AutomationLanes/PointSelection/LaneKeyStep.h"
#include <cmath>

namespace synth::ui {

namespace {
// A fifth of the step when Shift asks for a fine move (the same divisor a tap uses).
constexpr double kFineSpeedFactor = 0.2;
// Of the axis range per second: where the glide starts and its cap, as the morph map and the automation lane's
// value glide have it.
constexpr double kStartRangePerSecond = 0.15;
constexpr double kMaxRangePerSecond = 1.2;
} // namespace

std::vector<juce::Point<float>> CurveEditorComponent::selectionPositions() const {
    std::vector<juce::Point<float>> positions;
    const auto geometry = currentGeometry();
    for (const int i : selection_.selected())
        if (i >= 0 && i < model_.getNumNodes())
            positions.push_back(geometry.nodePosition(i));
    return positions;
}

double CurveEditorComponent::keyDragSpeedFactor() const {
    const auto& host = keyHold_.host;
    return host.isShiftDown && host.isShiftDown() ? kFineSpeedFactor : 1.0;
}

// A press is one step; if the key is physically down the step becomes the start of a hold instead, whose frames carry
// on. A press whose key is not down (a synthesised event) is the plain step.
void CurveEditorComponent::pressNudge(int keyCode, double dx, double dy, bool fine) {
    if (keyHold_.active()) {
        if (keyHold_.keyIsDown(keyHold_.keyCode()))
            return;     // auto-repeat or a second key: the running hold picks the direction up
        keyHold_.end(); // a hold whose key-up was missed
    }
    if (keyHold_.keyIsDown(keyCode) && beginKeyDrag(keyCode, dx, dy, fine))
        return;
    nudgeSelectedNode(dx, (float)dy, fine);
}

bool CurveEditorComponent::beginKeyDrag(int keyCode, double dx, double dy, bool fine) {
    if (cursorIndex() < 0 || cursorIndex() >= model_.getNumNodes() || dragKind_ != DragKind::None)
        return false;
    const auto& node = model_.getNode(cursorIndex());
    const bool time = dx != 0.0;
    const double step = time ? std::abs(dx) : std::abs(dy);
    const double range = time ? currentGeometry().getVisibleRange() : 1.0;
    keyDrag_ = {};
    keyDrag_.time = time;
    keyDrag_.sign = (time ? dx : dy) < 0.0 ? -1 : 1;
    keyDrag_.start = time ? node.x : (double)node.y;
    keyDrag_.unit = step;
    keyDrag_.snapped = snapToGrid_ && grid_.has_value() && !fine;
    const KeyGlideConfig config{kStartRangePerSecond * range, kMaxRangePerSecond * range, lane_key::kTapMs,
                                lane_key::kRampMs};
    keyHold_.begin(keyCode, config, step);
    applyKeyDrag(true);
    return true;
}

// Moves the group to where the key has carried the cursor point: relative to the point's place now, so the same
// move path as a press does the clamping, snapping and neighbour limits.
void CurveEditorComponent::applyKeyDrag(bool tapStep) {
    if (cursorIndex() < 0 || cursorIndex() >= model_.getNumNodes())
        return;
    const double distance = keyHold_.distance();
    const double covered = keyDrag_.snapped ? std::floor(distance / keyDrag_.unit + 1e-9) * keyDrag_.unit : distance;
    const double target = keyDrag_.start + keyDrag_.sign * covered;
    const auto& node = model_.getNode(cursorIndex());
    const double dx = keyDrag_.time ? target - node.x : 0.0;
    const double dy = keyDrag_.time ? 0.0 : target - node.y;
    const bool fine = keyHold_.host.isShiftDown && keyHold_.host.isShiftDown();
    moveSelectionBy(dx, (float)dy, fine, true, tapStep || keyDrag_.snapped);
}

void CurveEditorComponent::endKeyDrag() {
    if (keyDrag_.gestureOpen)
        endGesture();
    keyDrag_.gestureOpen = false;
}

bool CurveEditorComponent::keyStateChanged(bool) {
    keyHold_.checkKeyUp();
    return false;
}

} // namespace synth::ui
