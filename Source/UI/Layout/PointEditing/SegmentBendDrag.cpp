#include "SegmentBendDrag.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
constexpr double kFlatSegmentPx = 0.5;
// The kernel shapes x^gamma with gamma = 2^(2t), so the curve sits at 0.5^gamma of the way at the half-way beat.
double gammaFor(double tension) { return std::exp2(2.0 * std::clamp(tension, -1.0, 1.0)); }
} // namespace

// A rising segment's positive bend pulls the curve toward the end value, which is higher: the pixel delta is negative
// when the pointer goes up, so the sign flips it. A falling segment's end value is lower, so it does not.
float upwardBendSign(double startValue, double endValue) { return endValue >= startValue ? -1.0f : 1.0f; }

double midFractionForTension(float tension) { return std::pow(0.5, gammaFor(tension)); }

double minMidFraction() { return midFractionForTension(1.0f); }
double maxMidFraction() { return midFractionForTension(-1.0f); }

float tensionForMidFraction(double fraction) {
    const double f = std::clamp(fraction, minMidFraction(), maxMidFraction());
    const double gamma = std::log(f) / std::log(0.5);
    return (float)std::clamp(std::log2(gamma) / 2.0, -1.0, 1.0);
}

// The room caps the zone, so a segment starting at the lane's top (or bottom) still holds inside the lane.
double holdOvershootPx(double spanPx, double roomPx) {
    const double wanted = std::max(kHoldOvershootPx, kHoldOvershootShare * std::abs(spanPx));
    return std::max(kMinHoldOvershootPx, std::min(wanted, roomPx));
}

double holdRoomPx(double startY, double endY, double topY, double bottomY) {
    const double extremeY = startY + minMidFraction() * (endY - startY);
    return (endY >= startY ? extremeY - topY : bottomY - extremeY) - kHoldEdgeMarginPx;
}

double laneBendStartFraction(double startY, double endY, float tension, bool wasHold) {
    return laneBendStartFraction(startY, endY, tension, wasHold, holdOvershootPx(endY - startY));
}

double laneBendStartFraction(double startY, double endY, float tension, bool wasHold, double overshootPx) {
    if (!wasHold)
        return midFractionForTension(tension);
    // A hold starts kHoldExitPx inside its zone, so it takes about that far toward the end value to bring the curve
    // back: a jitter during a click never turns a hold into a curve.
    const double span = std::abs(endY - startY);
    return minMidFraction() - (overshootPx + kHoldExitPx) / std::max(span, kFlatSegmentPx);
}

LaneBend laneBendAt(double startY, double endY, double f0, double dyPx, float tension, bool wasHold) {
    return laneBendAt(startY, endY, f0, dyPx, tension, wasHold, holdOvershootPx(endY - startY));
}

// The hold side is the start value's side (below a rising segment's start, above a falling one's: the sign of `span`
// flips with the direction, so both behave alike): tension +1 already hugs the start value. Past the extreme the
// shape keeps moving: `toHold` grows with the pointer across the overshoot, so the curve flattens onto the start value
// and reaches the step exactly where the hold begins, with no dead zone and no flip. Past the end value's
// side the bend just stops at its limit.
LaneBend laneBendAt(double startY, double endY, double f0, double dyPx, float tension, bool wasHold,
                    double overshootPx) {
    const double span = endY - startY;
    if (std::abs(span) < kFlatSegmentPx)
        return {tension, wasHold, wasHold ? 1.0f : 0.0f};
    const double f = f0 + dyPx / span;
    const double pastExtremePx = (minMidFraction() - f) * std::abs(span);
    if (pastExtremePx > overshootPx)
        return {wasHold ? tension : 1.0f, true, 1.0f};
    const float toHold = (float)std::clamp(pastExtremePx / std::max(overshootPx, 1e-9), 0.0, 1.0);
    return {tensionForMidFraction(f), false, toHold};
}

LaneBendDrag::LaneBendDrag(double startY, double endY, float tension, bool wasHold, double pressY, double topY,
                           double bottomY)
    : startY_(startY)
    , endY_(endY)
    , overshootPx_(holdOvershootPx(endY - startY, holdRoomPx(startY, endY, topY, bottomY)))
    , f0_(laneBendStartFraction(startY, endY, tension, wasHold, overshootPx_))
    , tension_(tension)
    , wasHold_(wasHold)
    , lastY_(pressY) {}

LaneBend LaneBendDrag::moveTo(double y, bool fine) {
    travelPx_ += (y - lastY_) * (fine ? kFineBendScale : 1.0);
    lastY_ = y;
    return laneBendAt(startY_, endY_, f0_, travelPx_, tension_, wasHold_, overshootPx_);
}

} // namespace synth::ui
