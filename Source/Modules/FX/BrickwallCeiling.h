#pragma once

#include <algorithm>
#include <cmath>
#include <juce_audio_basics/juce_audio_basics.h>

namespace synth {

/**
 * @brief The brickwall stage of LimiterModule (its Ceiling), pulled out so Master's always-on safety
 * limiter runs the very same DSP (docs/modules/fx-modules.md#limiter-module, docs/mixer/mixer.md).
 *
 * One linked gain: down at once to whatever keeps the louder leg at or under the ceiling, back up
 * over the release time. Out of the path (and reset) while the ceiling sits at 0 dBFS and the gain
 * is back at unity. No allocation; safe on the audio thread.
 */
class BrickwallCeiling {
public:
    void reset() noexcept { gain_ = 1.0f; }

    /** Limits `left`/`right` in place. Returns the deepest reduction it took, in dB (>= 0). */
    float process(float* left, float* right, int numSamples, float ceilingDb, float releaseMs,
                  double sampleRate) noexcept {
        const bool active = ceilingDb < -1.0e-4f;
        if (!active && gain_ >= 0.9999f) {
            gain_ = 1.0f;
            return 0.0f;
        }
        // A ceiling returning to 0 dBFS lets the gain drift back to unity rather than snapping.
        const float ceiling = active ? juce::Decibels::decibelsToGain(ceilingDb) * 0.99999f : 1.0e9f;
        const float recovery = 1.0f - std::exp(-1.0f / std::max(1.0f, releaseMs * 0.001f * (float)sampleRate));
        float deepest = 1.0f;
        for (int i = 0; i < numSamples; ++i) {
            const float peak = std::max(std::abs(left[i]), std::abs(right[i]));
            const float needed = peak > ceiling ? ceiling / peak : 1.0f;
            gain_ = std::min(needed, gain_ + (1.0f - gain_) * recovery);
            left[i] *= gain_;
            right[i] *= gain_;
            deepest = std::min(deepest, gain_);
        }
        return -juce::Decibels::gainToDecibels(deepest, -100.0f);
    }

private:
    float gain_ = 1.0f;
};

} // namespace synth
