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

LaneBendDrag::LaneBendDrag(double startY, double endY, float tension, LaneBendKind was, double xp, double pressY,
                           double /*topY*/, double /*bottomY*/)
    : startY_(startY)
    , endY_(endY)
    , xp_(std::clamp(xp, kMinPressFraction, kMaxPressFraction))
    , tension_(tension)
    , was_(was)
    , kind_(was)
    , pressY_(pressY)
    , lastY_(pressY) {
    const double reached = was == LaneBendKind::Hold   ? 0.0
                           : was == LaneBendKind::Jump ? 1.0
                                                       : std::pow(xp_, gammaFor(tension));
    offset_ = startY + (endY - startY) * reached - pressY;
}

namespace {
// The curve's average level over the segment, 0 at the start value and 1 at the end value: the mean of x^g is 1/(g+1).
double areaOf(double tension) { return 1.0 / (gammaFor(tension) + 1.0); }
double tensionOfArea(double area) {
    const double gamma = 1.0 / std::clamp(area, 1e-12, 1.0 - 1e-12) - 1.0;
    return std::clamp(std::log2(gamma) / 2.0, -kMaxT, kMaxT);
}

// The curve's tension `along` pixels from the start row (0 < along < span). Between the zones the curve passes under
// the pointer at xp; inside a zone its area eases linearly from the formula's at the zone's edge to the limit's area
// a step's whole exit distance (its row's slack plus kHoldExitPx) before the end row, and stays at the limit there:
// every pixel of the zone moves the drawn line by the same share, the step, where it begins, differs from the curve by
// the limit's sliver alone, and a step left again hands over to the curve at the limit.
constexpr double kPlateauPx = kEndRowSlackPx + kHoldExitPx;

double curveTension(double along, double span, double xp) {
    const double zone = std::min(kStepZonePx, span / 4.0);
    const double easeLen = zone - kPlateauPx;
    if (along < zone) {
        if (easeLen <= 0.0 || along <= kPlateauPx)
            return kMaxT;
        const double edge = areaOf(tensionThrough(zone / span, xp));
        const double limit = areaOf(kMaxT);
        return tensionOfArea(limit + (edge - limit) * (along - kPlateauPx) / easeLen);
    }
    const double fromEnd = span - along;
    if (fromEnd < zone) {
        if (easeLen <= 0.0 || fromEnd <= kPlateauPx)
            return -kMaxT;
        const double edge = areaOf(tensionThrough((span - zone) / span, xp));
        const double limit = areaOf(-kMaxT);
        return tensionOfArea(limit + (edge - limit) * (fromEnd - kPlateauPx) / easeLen);
    }
    return tensionThrough(along / span, xp);
}
} // namespace

LaneBend LaneBendDrag::moveTo(double y, bool fine) {
    const double step = (y - lastY_) * (fine ? kFineBendScale : 1.0);
    travel_ += step;
    lastY_ = y;
    const double span = std::abs(endY_ - startY_);
    if (span < kFlatSegmentPx)
        return {tension_, was_ == LaneBendKind::Hold, was_ == LaneBendKind::Jump};
    const double row = pressY_ + travel_ + offset_;
    const double along = (row - startY_) * (endY_ >= startY_ ? 1.0 : -1.0);

    // Within the slack a step is entered only on a move toward its row, so a press near a row that moves away from it
    // keeps the curve.
    const double toward = step * (endY_ >= startY_ ? 1.0 : -1.0);
    if (along <= 0.0 || (along <= kEndRowSlackPx && toward < 0.0))
        kind_ = LaneBendKind::Hold;
    else if (along >= span || (along >= span - kEndRowSlackPx && toward > 0.0))
        kind_ = LaneBendKind::Jump;
    else if ((kind_ == LaneBendKind::Hold && along > kPlateauPx) ||
             (kind_ == LaneBendKind::Jump && along < span - kPlateauPx))
        kind_ = LaneBendKind::Curve;

    // A step keeps the tension the segment had when it started as that step; one reached by dragging carries the limit
    // its curve had arrived at, so leaving it again starts from where it went in.
    if (kind_ == LaneBendKind::Hold)
        return {was_ == LaneBendKind::Hold ? tension_ : synth::kMaxLaneTension, true, false};
    if (kind_ == LaneBendKind::Jump)
        return {was_ == LaneBendKind::Jump ? tension_ : -synth::kMaxLaneTension, false, true};
    return {(float)curveTension(along, span, xp_), false, false};
}

} // namespace synth::ui
