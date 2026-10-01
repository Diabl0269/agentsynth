// PitchGlide.h -- constant-time portamento for one voice, in the pitch (semitone) domain.
// Header-only and free of JUCE types so it can be unit-tested and used from the audio thread:
// no allocation, no locks, no state shared between voices.
#pragma once

#include <algorithm>
#include <cmath>

namespace synth {

/** Slides a voice's frequency to each new target over a fixed time. The state is an OFFSET in
    semitones from the current target that runs down to 0, so a new target that arrives mid-slide
    continues from wherever the pitch actually is instead of snapping. */
class PitchGlide {
public:
    void reset() noexcept {
        targetHz_ = 0.0f;
        offsetSemitones_ = 0.0f;
        stepSemitones_ = 0.0f;
        primed_ = false;
    }

    /** Once per block. The first target a voice ever sees, or a glide time of 0, takes effect at
        once; otherwise a changed target starts a slide taking `glideSeconds` from the current pitch. */
    void retarget(float targetHz, float glideSeconds, double sampleRate) noexcept {
        if (!(targetHz > 0.0f))
            return;
        if (!primed_ || glideSeconds <= 0.0f) {
            targetHz_ = targetHz;
            offsetSemitones_ = 0.0f;
            stepSemitones_ = 0.0f;
            primed_ = true;
            return;
        }
        if (targetHz == targetHz_)
            return;
        offsetSemitones_ += 12.0f * std::log2(targetHz_ / targetHz);
        targetHz_ = targetHz;
        stepSemitones_ = std::abs(offsetSemitones_) / (glideSeconds * (float)sampleRate);
    }

    bool isGliding() const noexcept { return offsetSemitones_ != 0.0f; }

    /** The frequency for the next sample. */
    float nextFrequency() noexcept {
        if (offsetSemitones_ == 0.0f)
            return targetHz_;
        if (std::abs(offsetSemitones_) <= stepSemitones_)
            offsetSemitones_ = 0.0f;
        else
            offsetSemitones_ -= std::copysign(stepSemitones_, offsetSemitones_);
        return targetHz_ * std::exp2(offsetSemitones_ / 12.0f);
    }

private:
    float targetHz_ = 0.0f;
    float offsetSemitones_ = 0.0f;
    float stepSemitones_ = 0.0f;
    bool primed_ = false;
};

} // namespace synth
