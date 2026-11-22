// SegmentBendDrag.h -- dragging the segment between two dots so the curve follows the pointer. Pure math shared by the
// curve editor (the half-way helpers) and the automation lanes (a press on the line);
// docs/timeline/automation-points.md.
//
// The lane drag is ABSOLUTE: the curve passes under the pointer. A lane segment is value = s + (e - s) * x^g with
// g = 2^(2t); pressing at fraction xp along the segment and dragging to a row `along` pixels from the start row toward
// the end row (S pixels away) wants x^g == p at xp, with p = along / S, so g = ln p / ln xp and t = log2(g) / 2: one
// closed form, no per-event state, so the curve is where the pointer is on every pixel and retracing the path retraces
// the curve. (The old relative drag moved the half-way point and eased its travel; it bent against a wall at tension
// +1 and could not reach the end row.) The press itself never jumps the curve: `offset_` is the gap between the
// pointer and the curve at the press, kept for the whole drag.
//
// Why +-3: a tension of +-1 is x^4 / x^(1/4), which never gets close to a step, so the last stretch of the drag had
// no curve to show. +-3 (g from 1/64 to 64) lets the curve come within a pixel of the start row or the end row.
//
// Step zone: tension +3 is not yet the step, so a pointer arriving at the start row must still bend the curve into
// it. In the last Z = min(kStepZonePx, S / 4) pixels before the start row the tension is eased linearly from the
// formula's value at p = Z / S up to +3 at the start row itself (continuous at Z, no wall), and the step (hold)
// begins there: at or past the start row the segment is a hold. Hysteresis: once a hold, the pointer must come
// kHoldExitPx back toward the end value to leave it, so a jitter during a click never turns a hold into a curve.
// Past the end row the curve stays at its limit (-3): it cannot pass the end value.
#pragma once

namespace synth::ui {

/** The sign that turns a pixel delta (y down) into a bend delta whose positive direction bulges toward the END value,
 *  so that dragging up makes the curve bulge up: -1 for a rising segment, +1 for a falling one. */
float upwardBendSign(double startValue, double endValue);

/** Fraction of the way from the start value to the end value the lane's curve has reached at the segment's half-way
 *  beat for `tension` (clamped to +-kMaxLaneTension); 0.5 for a straight line. */
double midFractionForTension(float tension);
/** The tension whose half-way fraction is `fraction`, clamped to the range lanes allow. */
float tensionForMidFraction(double fraction);

/** The least and greatest half-way fraction a lane tension reaches (tension +kMaxLaneTension and -kMaxLaneTension). */
double minMidFraction();
double maxMidFraction();

/** The pointer must come this far (pixels toward the end value) past the start row to leave a hold. */
constexpr double kHoldExitPx = 2.0;
/** The last stretch before the start row (at most a quarter of the segment) where the tension eases up to the maximum.
 */
constexpr double kStepZonePx = 8.0;
/** The press fraction along a segment is kept inside this, so a press at an end never divides by a log of 0 or 1. */
constexpr double kMinPressFraction = 0.02;
constexpr double kMaxPressFraction = 0.98;
/** Shift held: the pointer's travel counts for this much. */
constexpr double kFineBendScale = 0.1;

struct LaneBend {
    float tension = 0.0f;
    bool hold = false;
};

/** One lane bend drag: feed it the pointer's y per event; Shift scales each step by kFineBendScale. The segment runs
 *  from `startY` to `endY` pixels, pressed at beat fraction `xp` and row `pressY`. What it returns is what a release
 *  commits: the curve (tension within +-kMaxLaneTension) or the hold. A flat segment (under half a pixel) keeps
 *  `tension` and `wasHold`. `topY`/`bottomY` (the lane's rows) are accepted for the caller's convenience and do not
 *  limit the drag: every row inside the lane is reachable, and rows past it keep the limit. */
class LaneBendDrag {
public:
    LaneBendDrag() = default;
    LaneBendDrag(double startY, double endY, float tension, bool wasHold, double xp, double pressY, double topY,
                 double bottomY);

    /** The state after the pointer moved to `y`. */
    LaneBend moveTo(double y, bool fine);

private:
    double startY_ = 0.0;
    double endY_ = 0.0;
    double xp_ = 0.5;
    float tension_ = 0.0f;
    bool wasHold_ = false;
    bool inHold_ = false;
    double offset_ = 0.0;
    double pressY_ = 0.0;
    double lastY_ = 0.0;
    double travel_ = 0.0;
};

} // namespace synth::ui
