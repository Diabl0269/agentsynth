// RestartFade.h -- the curve a retriggered oscillator uses to hand over from its old sound to its new one.
// Header-only, pure, safe on the audio thread. docs/modules/oscillator-voice.md#phase-and-rand.
#pragma once

#include <cmath>
#include <juce_core/juce_core.h>

namespace synth {

/** Length in samples of the hand-over after a restart. */
constexpr int kRestartFadeSamples = 64;

/** Length in samples of the hand-over when restarts come at audio rate, each one a cycle of oscillator sync: long
    enough to leave no step, short enough that the synced wave keeps its height and its bright overtones. */
constexpr int kSyncFadeSamples = 8;

/** How far a fade of `length` samples has run, 0..1, with `remaining` samples of it left (a fraction when the fade
    began between two samples). A raised cosine, so the sum of old and new has no corner where the fade starts or
    ends (a straight ramp leaves a kink there, which is what a short tick is made of). */
inline float restartFadeIn(float remaining, int length = kRestartFadeSamples) {
    const float t = 1.0f - remaining / (float)length;
    return 0.5f - 0.5f * std::cos(juce::MathConstants<float>::pi * t);
}

} // namespace synth
