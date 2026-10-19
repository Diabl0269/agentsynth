// UnisonVoicing.h -- the unison maths the Oscillator and the Wavetable share: where each voice sits in
// the detune spread, how it pans across L/R, which voices Blend keeps at full level, and the restart
// phase of a voice on a new note. Pure functions, header-only, safe on the audio thread.
// docs/modules/wavetable.md#unison-stacking-and-the-sub, docs/modules/oscillator-voice.md.
#pragma once

#include "ModuleBase.h"

#include <algorithm>
#include <cmath>
#include <juce_core/juce_core.h>

namespace synth {

constexpr int kMaxUnisonVoices = 8;

/** Position of unison voice `u` of `count` in the detune spread: -1 (lowest) .. +1 (highest), 0 for a
    single voice. Detune cents are multiplied by this. */
inline float unisonOffsetFraction(int u, int count) { return (count > 1) ? (2.0f * u / (count - 1) - 1.0f) : 0.0f; }

/** Frequency ratio of voice `u` for a total detune of `detuneCents` (the spread is symmetric about the root). */
inline float unisonDetuneRatio(float detuneCents, int u, int count) {
    return std::pow(2.0f, detuneCents * unisonOffsetFraction(u, count) / 1200.0f);
}

/** Balance placement of voice `u` across L/R: width 0 collapses to centre, width 1 puts the outer
    voices hard left and right. Uses the shared ModuleBase balance law, so centre is unity on both legs. */
inline void unisonPanGains(int u, int count, float width, float& gainL, float& gainR) {
    ModuleBase::panGains(width * unisonOffsetFraction(u, count), gainL, gainR);
}

/** Whether voice `u` is nearest the root pitch: the middle voice for an odd count, the two middle
    voices for an even one. Blend leaves these at full level. Integer maths, so the pair is exact. */
inline bool isUnisonCentreVoice(int u, int count) { return std::abs(2 * u - (count - 1)) == ((count - 1) & 1); }

/** Level of voice `u` under Blend: 1 for the voices nearest the root, `blend` for the rest. Normalise
    the stack by the sum of these gains; at blend 1 that is exactly the unisonCount average. */
inline float unisonBlendGain(int u, int count, float blend) { return isUnisonCentreVoice(u, count) ? 1.0f : blend; }

/** Start phase (0..1) of a voice restarting on a note: `startPhase` plus up to `randomAmount` of a
    cycle drawn from `rng`. No random draw is made at 0, so the stream only advances when it is used. */
inline float unisonRestartPhase(float startPhase, float randomAmount, juce::Random& rng) {
    float p = startPhase;
    if (randomAmount > 0.0f)
        p += randomAmount * rng.nextFloat();
    return p - std::floor(p);
}

} // namespace synth
