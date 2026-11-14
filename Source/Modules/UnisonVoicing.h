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

/** What a new Oscillator or Wavetable starts with, so raising Unison is heard at once: a +/-35 cent spread with the
    outer voices at 78 % of the centre and fully panned. They match the measured reference stack at its own defaults. */
constexpr float kDefaultUnisonDetuneCents = 35.0f;
constexpr float kDefaultUnisonBlend = 0.75f;
constexpr float kDefaultUnisonWidth = 1.0f;
/** Rand as a 0..1 fraction: every unison voice starts each note at a random phase, as the measured reference does. */
constexpr float kDefaultUnisonRand = 1.0f;

/** A stored module that has no Detune, Blend, Width or Rand key was saved before those defaults: give it the values it
    sounded with then (Detune 0, Blend 100 %, Width 0, Rand 0). `stores(key)` says whether the saved data holds that
   key. */
template <typename StoresKey>
void restoreLegacyUnisonDefaults(const StoresKey& stores, juce::AudioParameterFloat& detune,
                                 juce::AudioParameterFloat& blend, juce::AudioParameterFloat& width,
                                 juce::AudioParameterFloat& rand) {
    if (!stores("detune"))
        detune.setValueNotifyingHost(detune.convertTo0to1(0.0f));
    if (!stores("blend"))
        blend.setValueNotifyingHost(blend.convertTo0to1(1.0f));
    if (!stores("width"))
        width.setValueNotifyingHost(width.convertTo0to1(0.0f));
    if (!stores("randomPhase"))
        rand.setValueNotifyingHost(0.0f);
}

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

/** How much of the stack's in-phase start is still left to trim out, `samples` after the note's restart (1 at the
    start, 0 once the voices have drifted apart; slightly negative where they cancel, which boosts the low band back up,
   never below -0.3). Voices restarted at the same phase (Rand 0, no spread) add coherently, so the divisor above, built
   for uncorrelated voices, leaves a low note several dB louder than the same note at Rand 100 until the detune has
   pulled them apart. For `count` equally spaced voices the correlation of their fundamentals is the Dirichlet kernel of
   their beat, `(sin^2(N t / 2) / sin^2(t / 2) - N) / (N (N - 1))`, with `t` the phase the neighbouring voices have
   drifted apart. It is followed only up to half a beat period, where the voices would come back into phase again.
   `weight` is 1 for an in-phase start and falls to 0 as Rand or Spread scatter the phases (`unisonCoherenceWeight`);
   `baseDt` is the phase increment per sample before detune and `ratioSpan` the highest voice's frequency ratio minus
   the lowest. The first 20 ms are left alone, which keeps the in-phase bump of the attack that the reference stack has,
   and the divisor takes over across the next 20 ms. */
inline float unisonStartCoherence(float samples, float sampleRate, float baseDt, float ratioSpan, int count,
                                  float weight) {
    if (weight <= 0.0f || count < 2)
        return 0.0f;
    const float pi = juce::MathConstants<float>::pi;
    const float theta = 2.0f * pi * samples * baseDt * ratioSpan / (float)(count - 1);
    if (theta >= pi)
        return 0.0f;
    const float n = (float)count;
    const float s = std::sin(0.5f * theta);
    const float beat = theta < 1e-4f ? 1.0f : (std::pow(std::sin(0.5f * n * theta) / s, 2.0f) - n) / (n * (n - 1.0f));
    const float fadeOut = std::clamp((pi - theta) / (0.2f * pi), 0.0f, 1.0f);
    const float ramp = std::clamp((samples / sampleRate - 0.02f) * 50.0f, 0.0f, 1.0f);
    // The low harmonics of a higher note drift apart within the first tens of ms anyway, so the trim fades out with
    // pitch: whole at 0 Hz, 0.45 at 220 Hz and gone from 400 Hz.
    const float pitchShare = std::clamp(1.0f - baseDt * sampleRate / 400.0f, 0.0f, 1.0f);
    return weight * beat * ramp * fadeOut * pitchShare;
}

/** Cut-off of the band `LowBandTrim` acts on: the in-phase start only holds for the low harmonics, because harmonic k
    drifts apart k times as fast as the fundamental. */
constexpr float kCoherentBandHz = 500.0f;

/** Turns the stack's low band (below `kCoherentBandHz`) down by `gain` (1 = untouched) while the in-phase start lasts,
    leaving the highs alone, since those were never in phase. One per stack, so a ghost keeps the trim its stack had
    and the sum stays continuous through a restart. */
struct LowBandTrim {
    float gain = 1.0f;
    float low[2]{};

    /** `k` is the one-pole coefficient `lowBandCoefficient`. */
    void apply(float& left, float& right, float k) {
        low[0] += k * (left - low[0]);
        low[1] += k * (right - low[1]);
        left += (gain - 1.0f) * low[0];
        right += (gain - 1.0f) * low[1];
    }
};

inline float lowBandCoefficient(double sampleRate) {
    return std::min(1.0f, 2.0f * juce::MathConstants<float>::pi * kCoherentBandHz / (float)sampleRate);
}

/** 1 when every voice restarts at the same phase, 0 once Rand or Spread (both 0..1) place them apart. */
inline float unisonCoherenceWeight(float randomAmount, float spread = 0.0f) {
    const float apart = std::clamp(randomAmount, 0.0f, 1.0f);
    return (1.0f - apart) * (1.0f - apart) * (1.0f - std::clamp(spread, 0.0f, 1.0f));
}

/** Start phase (0..1) of a voice restarting on a note: `startPhase` plus `randomAmount` of a cycle times `draw` (0..1).
 */
inline float unisonRestartPhase(float startPhase, float randomAmount, float draw) {
    const float p = startPhase + randomAmount * draw;
    return p - std::floor(p);
}

/** The same with a new draw from `rng`. No draw is made at 0, so the stream only advances when it is used. */
inline float unisonRestartPhase(float startPhase, float randomAmount, juce::Random& rng) {
    return unisonRestartPhase(startPhase, randomAmount, randomAmount > 0.0f ? rng.nextFloat() : 0.0f);
}

} // namespace synth
