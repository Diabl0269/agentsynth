// SegmentBendDrag.h -- dragging the segment between two dots so the curve follows the pointer. Pure math shared by the
// curve editor (a bend handle) and the automation lanes (a press on the line); docs/timeline/automation-points.md.
#pragma once

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

/** How far past the extreme (in pixels, toward the start value) a drag goes before the segment becomes a hold. */
constexpr double kHoldOvershootPx = 12.0;
/** Shift held: the pointer's travel counts for this much. */
constexpr double kFineBendScale = 0.1;

struct LaneBend {
    float tension = 0.0f;
    bool hold = false;
};

/** Where the lane's curve bends after the pointer travelled `dyPx` (y down) from a press at half-way fraction `f0`,
 *  on a segment drawn from `startY` to `endY` pixels. A flat segment (under half a pixel) keeps `tension` and
 * `wasHold`. */
LaneBend laneBendAt(double startY, double endY, double f0, double dyPx, float tension, bool wasHold);

/** The half-way fraction a press starts from: the segment's own, or deep enough in the hold zone that it takes about 12
 * px to leave it. */
double laneBendStartFraction(double startY, double endY, float tension, bool wasHold);

/** One lane bend drag: feed it the pointer's y per event; Shift scales each step by kFineBendScale. */
class LaneBendDrag {
public:
    LaneBendDrag() = default;
    LaneBendDrag(double startY, double endY, float tension, bool wasHold, double pressY);

    /** The state after the pointer moved to `y`. */
    LaneBend moveTo(double y, bool fine);

private:
    double startY_ = 0.0;
    double endY_ = 0.0;
    double f0_ = 0.5;
    float tension_ = 0.0f;
    bool wasHold_ = false;
    double lastY_ = 0.0;
    double travelPx_ = 0.0;
};

} // namespace synth::ui
