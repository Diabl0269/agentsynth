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
// The two ends: at or past the start row the segment is a Hold (a step at the right point, flat at the start value), at
// or past the end row a Jump (a step at the left point, flat at the end value). What is drawn is a function of the
// pointer's row alone: no time blend, so a pointer standing still draws a still line. Both ends have a step zone, the
// last Z = min(kStepZonePx, S / 4) pixels before the end row: the curve's average level (its area, 1 / (g + 1)) eases
// linearly from the formula's at the zone's edge to the tension limit's kEndRowSlackPx + kHoldExitPx before the end
// row, so every pixel of the zone moves the drawn line by the same share and the limit is already reached when the step
// takes over (+3 and the Hold differ by 1/65 of the span on average). A pointer within kEndRowSlackPx of a row, moving
// toward it, is on it. Hysteresis: once a step, the pointer must come kHoldExitPx back past that slack to leave it, so
// a jitter on the end row keeps one shape and one bubble.
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

/** The pointer must come this far back past an end row's slack (kEndRowSlackPx) to leave its step (Hold or Jump). */
constexpr double kHoldExitPx = 2.0;
/** A pointer this close to an end row is on it: the end row and the press offset are fractional (up to half a pixel
 *  each) while pointer rows are whole pixels, so the pixel a user sees the dot on may sit just under 1 px short. */
constexpr double kEndRowSlackPx = 1.0;
/** The last stretch before either end row (at most a quarter of the segment) where the curve eases to its limit. */
constexpr double kStepZonePx = 8.0;
/** The press fraction along a segment is kept inside this, so a press at an end never divides by a log of 0 or 1. */
constexpr double kMinPressFraction = 0.02;
constexpr double kMaxPressFraction = 0.98;
/** Shift held: the pointer's travel counts for this much. */
constexpr double kFineBendScale = 0.1;

/** What a bend drag draws and a release commits: the curve, the Hold at the start value or the Jump to the end value.
 */
enum class LaneBendKind { Curve, Hold, Jump };

struct LaneBend {
    float tension = 0.0f;
    bool hold = false;
    bool jump = false;
};

/** One lane bend drag: feed it the pointer's y per event; Shift scales each step by kFineBendScale. The segment runs
 *  from `startY` to `endY` pixels, pressed at beat fraction `xp` and row `pressY`. What it returns is what a release
 *  commits: the curve (tension within +-kMaxLaneTension), the hold or the jump. A flat segment (under half a pixel)
 * keeps `tension` and `was`. `topY`/`bottomY` (the lane's rows) are accepted for the caller's convenience and do not
 *  limit the drag: every row inside the lane is reachable, and rows past it keep the limit. */
class LaneBendDrag {
public:
    LaneBendDrag() = default;
    LaneBendDrag(double startY, double endY, float tension, LaneBendKind was, double xp, double pressY, double topY,
                 double bottomY);

    /** The state after the pointer moved to `y`. */
    LaneBend moveTo(double y, bool fine);

private:
    double startY_ = 0.0;
    double endY_ = 0.0;
    double xp_ = 0.5;
    float tension_ = 0.0f;
    LaneBendKind was_ = LaneBendKind::Curve;
    LaneBendKind kind_ = LaneBendKind::Curve;
    double offset_ = 0.0;
    double pressY_ = 0.0;
    double lastY_ = 0.0;
    double travel_ = 0.0;
};

} // namespace synth::ui
