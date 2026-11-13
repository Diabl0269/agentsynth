// MorphMapGlide.cpp -- the morph map's arrow-hold motion model: KeyGlide's ramp plus a clamped 2D position.
#include "MorphMapGlide.h"
#include <algorithm>

namespace synth::ui {

bool MorphMapGlide::press(double nowMs, GlidePoint start) noexcept {
    if (!glide_.press(nowMs))
        return false;
    position_ = {std::clamp(start.x, 0.0, 1.0), std::clamp(start.y, 0.0, 1.0)};
    return true;
}

bool MorphMapGlide::isGliding(double nowMs) const noexcept { return glide_.isGliding(nowMs); }

double MorphMapGlide::velocityAt(double nowMs) const noexcept { return glide_.velocityAt(nowMs); }

/** A diagonal splits the step so it is no faster than a straight one. */
GlidePoint MorphMapGlide::advance(double nowMs, int dx, int dy, double speedFactor) noexcept {
    if (!glide_.isHeld())
        return position_;
    double step = glide_.advance(nowMs, speedFactor);
    if (dx != 0 && dy != 0)
        step *= 0.70710678118654752;
    position_.x = std::clamp(position_.x + (double)dx * step, 0.0, 1.0);
    position_.y = std::clamp(position_.y + (double)dy * step, 0.0, 1.0);
    return position_;
}

GlidePoint MorphMapGlide::release() noexcept {
    glide_.release();
    return position_;
}

} // namespace synth::ui
