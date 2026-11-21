// SegmentBendDrag.h -- dragging the segment between two dots so the curve follows the pointer. Pure math shared by the
// curve editor (a bend handle) and the automation lanes (a press on the line); docs/timeline/automation-points.md.
#pragma once

#include <limits>

namespace synth::ui {

/** The sign that turns a pixel delta (y down) into a bend delta whose positive direction bulges toward the END value,
 *  so that dragging up makes the curve bulge up: -1 for a rising segment, +1 for a falling one. */
float upwardBendSign(double startValue, double endValue);

/** Fraction of the way from the start value to the end value the lane's curve has reached at the segment's half-way
 *  beat for `tension` (clamped to [-1, 1]); 0.5 for a straight line. */
double midFractionForTension(float tension);
/** The tension whose half-way fraction is `fraction`, clamped to the range lanes allow. */
float tensionForMidFraction(double fraction);

/** The least and greatest half-way fraction a lane tension reaches (tension +1 and -1). */
double minMidFraction();
double maxMidFraction();

/** The least a drag goes past the extreme (in pixels, toward the start value) before the segment becomes a hold; a
 *  tall segment asks for more (`kHoldOvershootShare` of its height), so the step is never one small nudge away. */
constexpr double kHoldOvershootPx = 24.0;
constexpr double kHoldOvershootShare = 0.35;
/** How far back toward the end value a hold must be dragged to become the steepest curve again. */
constexpr double kHoldExitPx = 11.0;

/** The least overshoot even a segment with no room past its extreme keeps, so the step is never a one-pixel flip. */
constexpr double kMinHoldOvershootPx = 8.0;
constexpr double kHoldEdgeMarginPx = 2.0;

/** The overshoot a segment `spanPx` tall needs before it becomes a hold, at most `roomPx` (how far the lane reaches
 * past the extreme on the start value's side) so a segment starting at the lane's top or bottom holds inside the lane;
 * never under kMinHoldOvershootPx. */
double holdOvershootPx(double spanPx, double roomPx = std::numeric_limits<double>::infinity());
/** How far a lane drawn from `topY` to `bottomY` reaches past the tension +1 extreme of the segment from `startY` to
 *  `endY`, on the start value's side (up for a falling segment, down for a rising one), less kHoldEdgeMarginPx so the
 *  hold begins on a row the pointer can still press inside the lane. */
double holdRoomPx(double startY, double endY, double topY, double bottomY);
/** The bend travel a pointer `travelPx` past its press gives on the end value's side of a lane that leaves `roomPx`
 *  before its edge, when the bend can still go `needPx` there: 1:1 at the press, eased so the bend reaches its limit at
 *  the lane's edge and every pixel on the way moves it (faster than the pointer when the room is short). `k` is
 *  endTravelEaseFor(needPx, roomPx). Past the edge it goes on 1:1. */
double endTravelEaseFor(double needPx, double roomPx);
double laneEndTravelPx(double travelPx, double needPx, double roomPx, double k);
/** Shift held: the pointer's travel counts for this much. */
constexpr double kFineBendScale = 0.1;

struct LaneBend {
    float tension = 0.0f;
    bool hold = false;
    /** How far the drawn shape has gone from the tension +1 curve toward the step: 0 outside the overshoot, rising
     *  with the pointer across it, 1 once it is a hold. Draw-only: a release short of the hold commits the curve. */
    float toHold = 0.0f;
};

/** Where the lane's curve bends after the pointer travelled `dyPx` (y down) from a press at half-way fraction `f0`,
 *  on a segment drawn from `startY` to `endY` pixels, with the hold `overshootPx` past the extreme (default
 *  holdOvershootPx of the span). A flat segment (under half a pixel) keeps `tension` and `wasHold`. */
LaneBend laneBendAt(double startY, double endY, double f0, double dyPx, float tension, bool wasHold);
LaneBend laneBendAt(double startY, double endY, double f0, double dyPx, float tension, bool wasHold,
                    double overshootPx);

/** The half-way fraction a press starts from: the segment's own, or deep enough in the hold zone that it takes about
 * `kHoldExitPx` to leave it. */
double laneBendStartFraction(double startY, double endY, float tension, bool wasHold);
double laneBendStartFraction(double startY, double endY, float tension, bool wasHold, double overshootPx);

/** One lane bend drag: feed it the pointer's y per event; Shift scales each step by kFineBendScale. The lane spans
 *  `topY`..`bottomY`, which caps the overshoot (holdRoomPx); the default is a lane with room to spare. */
class LaneBendDrag {
public:
    LaneBendDrag() = default;
    LaneBendDrag(double startY, double endY, float tension, bool wasHold, double pressY,
                 double topY = -std::numeric_limits<double>::infinity(),
                 double bottomY = std::numeric_limits<double>::infinity());

    /** The pixels past the extreme this drag's hold needs. */
    double overshootPx() const noexcept { return overshootPx_; }

    /** The state after the pointer moved to `y`. */
    LaneBend moveTo(double y, bool fine);

private:
    double startY_ = 0.0;
    double endY_ = 0.0;
    double overshootPx_ = kHoldOvershootPx;
    double f0_ = 0.5;
    float tension_ = 0.0f;
    bool wasHold_ = false;
    double lastY_ = 0.0;
    double travelPx_ = 0.0;
    double endNeedPx_ = 0.0;
    double endRoomPx_ = std::numeric_limits<double>::infinity();
    double endEase_ = 0.0;
};

} // namespace synth::ui
