// SoftLimit.h -- the shared output soft limiter: identity up to the knee, then a tanh curve that
// rounds off just below full scale. Header-only, safe on the audio thread.
// docs/development/gain-staging.md.
#pragma once

#include <cmath>

namespace synth {

/** Level up to which `softLimit` leaves a sample untouched. */
constexpr float kSoftLimitKnee = 0.9f;

/** Passes anything at or below the knee unchanged and rounds larger magnitudes towards 0.98, so a sum
    of voices stays below full scale. Odd, monotonic and continuous at the knee. */
inline float softLimit(float x) noexcept {
    const float mag = std::fabs(x);
    if (mag <= kSoftLimitKnee)
        return x;
    return std::copysign(kSoftLimitKnee + 0.08f * std::tanh((mag - kSoftLimitKnee) * 12.5f), x);
}

} // namespace synth
