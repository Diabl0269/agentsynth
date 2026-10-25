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

/** Gain each voice is summed at in a poly sum (the VCA's poly mode). Fixed: no voice-count or dynamic
    normalisation, so every note adds the same level and a chord is louder than one note, as on other
    polyphonic synths. About -11 dB, so a three-note chord stays under the knee even at full VCA Gain and
    eight notes at the default Gain stay clean; one poly note is therefore about 11 dB quieter than the
    same note through the mono path. `softLimit` stays the safety net for bigger chords. */
constexpr float kPolyVoiceGain = 0.28f;

} // namespace synth
