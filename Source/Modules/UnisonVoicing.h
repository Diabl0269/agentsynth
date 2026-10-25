// UnisonVoicing.h -- the unison maths the Oscillator and the Wavetable share: where each voice sits in
// the detune spread, how it pans across L/R, how loud Blend leaves it, and the restart phase of a voice
// on a new note. Pure functions, header-only, safe on the audio thread.
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

/** Which side voice `u` of `count` is panned to when Width is up: +1 right, -1 left, 0 for the single
    middle voice of an odd count, which stays centred. The other voices, taken in detune order, alternate
    right, left, right, ..., so each mirrored pair lands on opposite sides and the stack stays balanced. */
inline int unisonPanSide(int u, int count) {
    const bool oddCentre = (count & 1) == 1 && u == (count - 1) / 2;
    if (oddCentre)
        return 0;
    const int rank = ((count & 1) == 1 && u > (count - 1) / 2) ? u - 1 : u;
    return (rank & 1) == 0 ? 1 : -1;
}

/** Leg gains of voice `u` at Width `width` (0..1): centre voices stay at unity on both legs; at Width 0
    every voice does. The leg a voice moves towards rises linearly to 2.5 at Width 1 and the far leg falls
    to 0 as (1 - width)^1.2. The shape is fitted to a measured reference stack (side/mid level within 11 %
    for 3, 5 and 7 voices over Blend and Width); `unisonPowerNormaliser` takes the extra level back out. */
inline void unisonPanGains(int u, int count, float width, float& gainL, float& gainR) {
    const float w = std::clamp(width, 0.0f, 1.0f);
    const int side = unisonPanSide(u, count);
    const float near = 1.0f + 1.5f * w;
    const float far = std::pow(1.0f - w, 1.2f);
    gainL = side > 0 ? far : (side < 0 ? near : 1.0f);
    gainR = side > 0 ? near : (side < 0 ? far : 1.0f);
}

/** Whether voice `u` is nearest the root pitch: the middle voice for an odd count, the two middle
    voices for an even one. Blend leaves these at full level. Integer maths, so the pair is exact. */
inline bool isUnisonCentreVoice(int u, int count) { return std::abs(2 * u - (count - 1)) == ((count - 1) & 1); }

/** Level of a voice away from the root under Blend, against 1 for the voices nearest the root: the ratio
    runs from 0 at Blend 0 through 0.37 at half to 1.33 at Blend 1, so the outer voices overtake the centre
    near Blend 0.87 (the curve is fitted to a measured reference stack). */
inline float unisonOuterGain(float blend) {
    const float b = std::clamp(blend, 0.0f, 1.0f);
    return 1.33f * b * (0.12f + 0.88f * b);
}

/** Level of voice `u` under Blend: 1 for the voices nearest the root, `unisonOuterGain` for the rest. */
inline float unisonBlendGain(int u, int count, float blend) {
    return isUnisonCentreVoice(u, count) ? 1.0f : unisonOuterGain(blend);
}

/** Mean of the two legs' power for one voice's final gains. Divide the stack by the root of its sum over
    all voices: uncorrelated voices then keep the loudness of one voice at any Unison, Blend and Width. */
inline float unisonLegPower(float gainL, float gainR) { return 0.5f * (gainL * gainL + gainR * gainR); }

/** Start phase (0..1) of a voice restarting on a note: `startPhase` plus up to `randomAmount` of a
    cycle drawn from `rng`. No random draw is made at 0, so the stream only advances when it is used. */
inline float unisonRestartPhase(float startPhase, float randomAmount, juce::Random& rng) {
    float p = startPhase;
    if (randomAmount > 0.0f)
        p += randomAmount * rng.nextFloat();
    return p - std::floor(p);
}

} // namespace synth
