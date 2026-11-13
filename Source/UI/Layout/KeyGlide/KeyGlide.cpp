// KeyGlide.cpp -- the shared key-hold motion model (see the header).
#include "KeyGlide.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

bool KeyGlide::press(double nowMs) noexcept {
    if (held_)
        return false;
    held_ = true;
    pressedAtMs_ = nowMs;
    lastDistance_ = 0.0;
    return true;
}

bool KeyGlide::isGliding(double nowMs) const noexcept { return held_ && nowMs - pressedAtMs_ >= config_.tapMs; }

/** The same curve as synth::CursorGlide (an ease-in from the start speed to the cap). */
double KeyGlide::velocityAt(double nowMs) const noexcept {
    if (!isGliding(nowMs))
        return 0.0;
    const double u = std::min(1.0, (nowMs - pressedAtMs_ - config_.tapMs) / config_.rampMs);
    return config_.startUnitsPerSecond + (config_.maxUnitsPerSecond - config_.startUnitsPerSecond) * u * u * u;
}

/** The exact integral of velocityAt, so the motion does not depend on the frame rate (CursorGlide::positionAt's
    form: the cubic's integral is a quartic, then a straight line at the cap). */
double KeyGlide::distanceAt(double nowMs) const noexcept {
    if (!isGliding(nowMs))
        return 0.0;
    const double t = (nowMs - pressedAtMs_ - config_.tapMs) / 1000.0;
    const double ramp = config_.rampMs / 1000.0;
    const double v0 = config_.startUnitsPerSecond;
    const double rise = config_.maxUnitsPerSecond - v0;
    return t <= ramp ? v0 * t + rise * ramp * std::pow(t / ramp, 4.0) / 4.0
                     : v0 * ramp + rise * ramp / 4.0 + config_.maxUnitsPerSecond * (t - ramp);
}

/** The distance is accumulated in steps rather than read once from the press, so the speed factor can change
    mid-hold without the position jumping. */
double KeyGlide::advance(double nowMs, double speedFactor) noexcept {
    if (!held_)
        return 0.0;
    const double distance = distanceAt(nowMs);
    const double step = (distance - lastDistance_) * speedFactor;
    lastDistance_ = distance;
    return step;
}

} // namespace synth::ui
