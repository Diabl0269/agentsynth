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

/** Nominal peak of a poly sum (master gain times the sum of each voice's audio peak times its envelope level)
    up to which the voices add at unity; past it `polySumHeadroom` takes the sum back. */
constexpr float kPolySumKnee = kSoftLimitKnee;

/** The gain a poly sum takes when its nominal peak `nominalPeak` (every voice at its audio peak and
    envelope level at once) is above `kPolySumKnee`: (knee / peak) to the power 0.7, so the sum's peaks stay out of
   `softLimit` (clipping would turn a steady chord into a distorted one) while a bigger chord is still no quieter than
   one note. 1 at or below the knee, so one note and quiet chords are untouched. */
inline float polySumHeadroom(float nominalPeak) noexcept {
    return nominalPeak > kPolySumKnee ? std::pow(kPolySumKnee / nominalPeak, 0.7f) : 1.0f;
}

} // namespace synth
