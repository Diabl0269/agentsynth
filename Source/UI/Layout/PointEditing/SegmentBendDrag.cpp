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

double laneBendStartFraction(double startY, double endY, float tension, bool wasHold) {
    if (!wasHold)
        return midFractionForTension(tension);
    // A hold starts one pixel short of twice the overshoot inside its zone, so it takes about 12 px toward the end
    // value to bring the curve back: a jitter during a click never turns a hold into a curve.
    const double span = std::abs(endY - startY);
    return minMidFraction() - (2.0 * kHoldOvershootPx - 1.0) / std::max(span, kFlatSegmentPx);
}

// The hold side is the start value's side only: tension +1 already hugs the start value, so the switch is continuous.
// Past the end value's side the bend just stops at its limit.
LaneBend laneBendAt(double startY, double endY, double f0, double dyPx, float tension, bool wasHold) {
    const double span = endY - startY;
    if (std::abs(span) < kFlatSegmentPx)
        return {tension, wasHold};
    const double f = f0 + dyPx / span;
    if (f < minMidFraction() - kHoldOvershootPx / std::abs(span))
        return {wasHold ? tension : 1.0f, true};
    return {tensionForMidFraction(f), false};
}

LaneBendDrag::LaneBendDrag(double startY, double endY, float tension, bool wasHold, double pressY)
    : startY_(startY)
    , endY_(endY)
    , f0_(laneBendStartFraction(startY, endY, tension, wasHold))
    , tension_(tension)
    , wasHold_(wasHold)
    , lastY_(pressY) {}

LaneBend LaneBendDrag::moveTo(double y, bool fine) {
    travelPx_ += (y - lastY_) * (fine ? kFineBendScale : 1.0);
    lastY_ = y;
    return laneBendAt(startY_, endY_, f0_, travelPx_, tension_, wasHold_);
}

} // namespace synth::ui
