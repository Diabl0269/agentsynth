// RestartFade.h -- the curve a retriggered oscillator uses to hand over from its old sound to its new one.
// Header-only, pure, safe on the audio thread. docs/modules/oscillator-voice.md#phase-and-rand.
#pragma once

#include <cmath>
#include <juce_core/juce_core.h>

namespace synth {

/** Length in samples of the hand-over after a restart. */
constexpr int kRestartFadeSamples = 64;

/** How far a fade of `kRestartFadeSamples` has run, 0..1, with `remaining` samples of it left. A raised
    cosine, so the sum of old and new has no corner where the fade starts or ends (a straight ramp leaves a
    kink there, which is what a short tick is made of). */
inline float restartFadeIn(int remaining) {
    const float t = 1.0f - (float)remaining / (float)kRestartFadeSamples;
    return 0.5f - 0.5f * std::cos(juce::MathConstants<float>::pi * t);
}

} // namespace synth
