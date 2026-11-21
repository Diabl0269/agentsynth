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

namespace {
// The least share of the even rate (need / room) the eased travel keeps at the lane's edge, so the last rows still move
// the curve by a visible amount instead of an exponential tail too small to draw.
constexpr double kEndTravelFloorShare = 0.15;
double endTravelFloor(double needPx, double roomPx) { return kEndTravelFloorShare * needPx / roomPx; }
} // namespace

// The end value's side has no hold, so a 1:1 bend stopped dead at tension -1 and the rest of the lane did nothing. The
// travel is eased instead: g(d) = a d + (need - a room) (1 - e^(-k d / room)) / (1 - e^(-k)), with a the floor rate and
// k chosen so g starts at slope 1; g reaches `need` exactly at the edge. A short room scales linearly instead, so the
// limit is still reached inside the lane.
double endTravelEaseFor(double needPx, double roomPx) {
    if (!(needPx > 0.0) || !std::isfinite(roomPx) || roomPx <= needPx * (1.0 + 1e-6))
        return 0.0;
    const double a = endTravelFloor(needPx, roomPx);
    const double ratio = (1.0 - a) * roomPx / (needPx - a * roomPx);
    double lo = 1e-9, hi = std::max(4.0, 2.0 * ratio + 4.0);
    for (int i = 0; i < 80; ++i) {
        const double mid = 0.5 * (lo + hi);
        (mid / -std::expm1(-mid) < ratio ? lo : hi) = mid;
    }
    return 0.5 * (lo + hi);
}

double laneEndTravelPx(double travelPx, double needPx, double roomPx, double k) {
    if (travelPx <= 0.0 || !(needPx > 0.0) || !std::isfinite(roomPx) || roomPx <= 0.0)
        return travelPx;
    if (travelPx >= roomPx)
        return needPx + (travelPx - roomPx);
    if (k <= 0.0)
        return travelPx * std::max(1.0, needPx / roomPx);
    const double a = endTravelFloor(needPx, roomPx);
    return a * travelPx + (needPx - a * roomPx) * std::expm1(-k * travelPx / roomPx) / std::expm1(-k);
}

LaneBendDrag::LaneBendDrag(double startY, double endY, float tension, bool wasHold, double pressY, double topY,
                           double bottomY)
    : startY_(startY)
    , endY_(endY)
    , overshootPx_(holdOvershootPx(endY - startY, holdRoomPx(startY, endY, topY, bottomY)))
    , f0_(laneBendStartFraction(startY, endY, tension, wasHold, overshootPx_))
    , tension_(tension)
    , wasHold_(wasHold)
    , lastY_(pressY) {
    // The end side reaches tension -1 at the lane's last row that way (down for a falling segment, up for a rising).
    endNeedPx_ = std::max(0.0, (maxMidFraction() - f0_) * std::abs(endY - startY));
    endRoomPx_ = endY >= startY ? bottomY - 1.0 - pressY : pressY - topY;
    endEase_ = endTravelEaseFor(endNeedPx_, endRoomPx_);
}

LaneBend LaneBendDrag::moveTo(double y, bool fine) {
    travelPx_ += (y - lastY_) * (fine ? kFineBendScale : 1.0);
    lastY_ = y;
    const double towardEnd = endY_ >= startY_ ? 1.0 : -1.0;
    const double along = travelPx_ * towardEnd;
    const double eased = along > 0.0 ? laneEndTravelPx(along, endNeedPx_, endRoomPx_, endEase_) : along;
    return laneBendAt(startY_, endY_, f0_, eased * towardEnd, tension_, wasHold_, overshootPx_);
}

} // namespace synth::ui
