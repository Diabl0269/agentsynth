// CurveEditorComponent -- the keyboard drag: a held Alt+arrow or Up/Down glides the selection (diagonally while one
// of each is held) and is one gesture (one undo step) from the first real move to the last key-up; a tap or a snapped
// step eases the drawn points. The class is declared in CurveEditorComponent.h.

#include "CurveEditorComponent.h"
#include "UI/Timeline/AutomationLanes/PointSelection/LaneKeyStep.h"
#include <algorithm>
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
        if (keyHold_.anyArrowDown())
            return;     // auto-repeat or a second key: the running hold picks the direction up
        keyHold_.end(); // a hold whose key-up was missed
    }
    if (keyHold_.keyIsDown(keyCode) && beginKeyDrag(keyCode, dx, dy, fine))
        return;
    nudgeSelectedNode(dx, (float)dy, fine);
}

// The step the press made sets the scale of both axes (a fine press is fine on either), so an arrow of the other axis
// held as well steps and glides alike and the point moves diagonally.
bool CurveEditorComponent::beginKeyDrag(int keyCode, double dx, double dy, bool fine) {
    if (cursorIndex() < 0 || cursorIndex() >= model_.getNumNodes() || dragKind_ != DragKind::None)
        return false;
    const auto& node = model_.getNode(cursorIndex());
    const auto steps = keyboardStep();
    const bool time = dx != 0.0;
    const double along = time ? steps.x : steps.y;
    const double scale = along > 0.0 ? (time ? std::abs(dx) : std::abs(dy)) / along : 1.0;
    keyDrag_ = {};
    keyDrag_.start = {node.x, (double)node.y};
    keyDrag_.unit = steps * scale;
    keyDrag_.snapped = snapToGrid_ && grid_.has_value() && !fine;
    // A snapped hold starts at one grid step per kSnappedStepSeconds (never slower than a free one), the cap scaled
    // alike.
    const auto motion = [this](double step, double range) {
        const double paceScale =
            keyDrag_.snapped ? std::max(1.0, step / (lane_key::kSnappedStepSeconds * kStartRangePerSecond * range))
                             : 1.0;
        return KeyAxisMotion{{kStartRangePerSecond * range * paceScale, kMaxRangePerSecond * range * paceScale,
                              lane_key::kTapMs, lane_key::kRampMs},
                             step};
    };
    auto timeMotion = motion(keyDrag_.unit.x, currentGeometry().getVisibleRange());
    timeMotion.joinsWithAltOnly = true;
    const auto valueMotion = motion(keyDrag_.unit.y, 1.0);
    keyHold_.begin(keyCode, time ? timeMotion : valueMotion, time ? valueMotion : timeMotion);
    applyKeyDrag(true);
    return true;
}

// How far the keys have carried the cursor point along `axis`: whole steps when snapped.
double CurveEditorComponent::coveredAlong(KeyAxis axis, double unit) const {
    const double distance = keyHold_.distance(axis);
    if (!keyDrag_.snapped || unit <= 0.0)
        return distance;
    return (distance < 0.0 ? -1.0 : 1.0) * std::floor(std::abs(distance) / unit + 1e-9) * unit;
}

// Moves the group to where the keys have carried the cursor point: relative to the point's place now, so the same
// move path as a press does the clamping, snapping and neighbour limits.
void CurveEditorComponent::applyKeyDrag(bool tapStep) {
    if (cursorIndex() < 0 || cursorIndex() >= model_.getNumNodes())
        return;
    const auto& node = model_.getNode(cursorIndex());
    const double dx = keyDrag_.start.x + coveredAlong(KeyAxis::time, keyDrag_.unit.x) - node.x;
    const double dy = keyDrag_.start.y + coveredAlong(KeyAxis::value, keyDrag_.unit.y) - node.y;
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
