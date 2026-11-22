#include "SegmentBendDrag.h"
#include "Timeline/AutomationKernel.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
constexpr double kFlatSegmentPx = 0.5;
constexpr double kMaxT = (double)synth::kMaxLaneTension;
// The kernel shapes x^gamma with gamma = 2^(2t), so the curve sits at 0.5^gamma of the way at the half-way beat.
double gammaFor(double tension) { return std::exp2(2.0 * std::clamp(tension, -kMaxT, kMaxT)); }

// The tension whose curve passes the fraction `p` (0..1, exclusive) of the way to the end value at beat fraction xp.
double tensionThrough(double p, double xp) {
    return std::clamp(std::log2(std::log(p) / std::log(xp)) / 2.0, -kMaxT, kMaxT);
}
} // namespace

// A rising segment's positive bend pulls the curve toward the end value, which is higher: the pixel delta is negative
// when the pointer goes up, so the sign flips it. A falling segment's end value is lower, so it does not.
float upwardBendSign(double startValue, double endValue) { return endValue >= startValue ? -1.0f : 1.0f; }

double midFractionForTension(float tension) { return std::pow(0.5, gammaFor(tension)); }

double minMidFraction() { return midFractionForTension(synth::kMaxLaneTension); }
double maxMidFraction() { return midFractionForTension(-synth::kMaxLaneTension); }

float tensionForMidFraction(double fraction) {
    const double f = std::clamp(fraction, minMidFraction(), maxMidFraction());
    const double gamma = std::log(f) / std::log(0.5);
    return (float)std::clamp(std::log2(gamma) / 2.0, -kMaxT, kMaxT);
}

LaneBendDrag::LaneBendDrag(double startY, double endY, float tension, bool wasHold, double xp, double pressY,
                           double /*topY*/, double /*bottomY*/)
    : startY_(startY)
    , endY_(endY)
    , xp_(std::clamp(xp, kMinPressFraction, kMaxPressFraction))
    , tension_(tension)
    , wasHold_(wasHold)
    , inHold_(wasHold)
    , pressY_(pressY)
    , lastY_(pressY) {
    const double curveRow = startY + (endY - startY) * (wasHold ? 0.0 : std::pow(xp_, gammaFor(tension)));
    offset_ = curveRow - pressY;
}

LaneBend LaneBendDrag::moveTo(double y, bool fine) {
    travel_ += (y - lastY_) * (fine ? kFineBendScale : 1.0);
    lastY_ = y;
    const double span = std::abs(endY_ - startY_);
    if (span < kFlatSegmentPx)
        return {tension_, wasHold_};
    const double row = pressY_ + travel_ + offset_;
    const double along = (row - startY_) * (endY_ >= startY_ ? 1.0 : -1.0);

    if (along <= 0.0)
        inHold_ = true;
    else if (along > kHoldExitPx)
        inHold_ = false;
    if (inHold_)
        return {wasHold_ ? tension_ : synth::kMaxLaneTension, true};

    if (along >= span)
        return {-synth::kMaxLaneTension, false};
    const double zone = std::min(kStepZonePx, span / 4.0);
    double t = tensionThrough(std::max(along, zone) / span, xp_);
    if (along < zone)
        t += (kMaxT - t) * (1.0 - along / zone);
    return {(float)t, false};
}

} // namespace synth::ui
