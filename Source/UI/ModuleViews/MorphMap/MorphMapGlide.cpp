// MorphMapGlide.cpp -- the morph map's arrow-hold motion model (see the header).
#include "MorphMapGlide.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

bool MorphMapGlide::press(double nowMs, GlidePoint start) noexcept {
    if (held_)
        return false;
    held_ = true;
    pressedAtMs_ = nowMs;
    lastDistance_ = 0.0;
    position_ = {std::clamp(start.x, 0.0, 1.0), std::clamp(start.y, 0.0, 1.0)};
    return true;
}

bool MorphMapGlide::isGliding(double nowMs) const noexcept {
    return held_ && nowMs - pressedAtMs_ >= morph_glide::kTapMs;
}

/** The same curve as synth::CursorGlide (an ease-in from the start speed to the cap), with this map's speeds. */
double MorphMapGlide::velocityAt(double nowMs) const noexcept {
    if (!isGliding(nowMs))
        return 0.0;
    const double u = std::min(1.0, (nowMs - pressedAtMs_ - morph_glide::kTapMs) / morph_glide::kRampMs);
    return morph_glide::kStartUnitsPerSecond +
           (morph_glide::kMaxUnitsPerSecond - morph_glide::kStartUnitsPerSecond) * u * u * u;
}

/** The exact integral of velocityAt, so the motion does not depend on the frame rate (CursorGlide::positionAt's
    form: the cubic's integral is a quartic, then a straight line at the cap). */
double MorphMapGlide::distanceAt(double nowMs) const noexcept {
    if (!isGliding(nowMs))
        return 0.0;
    const double t = (nowMs - pressedAtMs_ - morph_glide::kTapMs) / 1000.0;
    const double ramp = morph_glide::kRampMs / 1000.0;
    const double v0 = morph_glide::kStartUnitsPerSecond;
    const double rise = morph_glide::kMaxUnitsPerSecond - v0;
    return t <= ramp ? v0 * t + rise * ramp * std::pow(t / ramp, 4.0) / 4.0
                     : v0 * ramp + rise * ramp / 4.0 + morph_glide::kMaxUnitsPerSecond * (t - ramp);
}

/** The distance is accumulated in steps rather than read once from the press, so the direction and the Shift factor
    can change mid-hold without the position jumping. A diagonal splits the step so it is no faster than a straight
    one. */
GlidePoint MorphMapGlide::advance(double nowMs, int dx, int dy, double speedFactor) noexcept {
    if (!held_)
        return position_;
    const double distance = distanceAt(nowMs);
    double step = (distance - lastDistance_) * speedFactor;
    lastDistance_ = distance;
    if (dx != 0 && dy != 0)
        step *= 0.70710678118654752;
    position_.x = std::clamp(position_.x + (double)dx * step, 0.0, 1.0);
    position_.y = std::clamp(position_.y + (double)dy * step, 0.0, 1.0);
    return position_;
}

GlidePoint MorphMapGlide::release() noexcept {
    held_ = false;
    return position_;
}

} // namespace synth::ui
