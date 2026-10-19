// PitchGlide.h -- constant-time portamento for one voice, in the pitch (semitone) domain.
// Header-only and free of JUCE types so it can be unit-tested and used from the audio thread:
// no allocation, no locks, no state shared between voices.
#pragma once

#include <algorithm>
#include <cmath>

namespace synth {

/** How the slide time relates to the interval. `Always` takes the set time whatever the interval;
    `Scaled` takes the set time per octave (a semitone takes a twelfth of it). */
enum class GlideTime { Always, Scaled };

/** The shape of the slide in pitch. `Linear` moves at a constant rate; `Exponential` approaches the target
    like an RC filter (fast start, slow settle) and still lands, exactly, by the slide time. */
enum class GlideCurve { Linear, Exponential };

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
        decay_ = 1.0f;
    }

    /** Once per block. The first target a voice ever sees, or a glide time of 0, takes effect at
        once; otherwise a changed target starts a slide taking `glideSeconds` from the current pitch. */
    void retarget(float targetHz, float glideSeconds, double sampleRate, GlideTime time = GlideTime::Always,
                  GlideCurve curve = GlideCurve::Linear) noexcept {
        if (!(targetHz > 0.0f))
            return;
        curve_ = curve;
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
        const float distance = std::abs(offsetSemitones_);
        if (curve == GlideCurve::Exponential && distance <= kSnapSemitones) {
            offsetSemitones_ = 0.0f;
            return;
        }
        const float seconds = time == GlideTime::Scaled ? glideSeconds * distance / 12.0f : glideSeconds;
        const float samples = std::max(1.0f, seconds * (float)sampleRate);
        stepSemitones_ = distance / samples;
        // The factor that brings the offset down to the snap distance in exactly `samples` steps.
        decay_ = std::exp(-std::log(distance / kSnapSemitones) / samples);
    }

    bool isGliding() const noexcept { return offsetSemitones_ != 0.0f; }

    /** The frequency for the next sample. */
    float nextFrequency() noexcept {
        if (offsetSemitones_ == 0.0f)
            return targetHz_;
        if (curve_ == GlideCurve::Exponential) {
            offsetSemitones_ *= decay_;
            if (std::abs(offsetSemitones_) <= kSnapSemitones)
                offsetSemitones_ = 0.0f;
        } else if (std::abs(offsetSemitones_) <= stepSemitones_)
            offsetSemitones_ = 0.0f;
        else
            offsetSemitones_ -= std::copysign(stepSemitones_, offsetSemitones_);
        return targetHz_ * std::exp2(offsetSemitones_ / 12.0f);
    }

private:
    float targetHz_ = 0.0f;
    float offsetSemitones_ = 0.0f;
    float stepSemitones_ = 0.0f;
    float decay_ = 1.0f;
    GlideCurve curve_ = GlideCurve::Linear;
    bool primed_ = false;
    static constexpr float kSnapSemitones = 0.005f; // half a cent: inaudible, then the slide ends exactly
};

} // namespace synth
