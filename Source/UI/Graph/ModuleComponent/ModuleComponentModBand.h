#pragma once

// FRO287: pure geometry for the modulation-ring depth band -- the reachable range a knob's
// attached modulation could push it across, drawn as a band under the live ring
// (docs/modules/modulation.md#modulation-rings-on-knobs). Split into a free function so the
// start/end-norm math is unit-testable with no ModuleComponent/LookAndFeel involved
// (Tests/UI/Graph/ModuleComponent/ModuleComponentModBandTests.cpp).

#include <juce_core/juce_core.h>

namespace synth::ui {

/** [startNorm, endNorm] the depth band should cover, already clamped to 0..1. */
struct ModDepthBandRange {
    float startNorm = 0.0f;
    float endNorm = 0.0f;
};

/** baseNorm: the knob's own current 0..1 position. amount: the attenuverter's "amount" (or 1.0 for
 *  a DirectCV/PolyBus routing), real range -1..1. bipolar: ModulationDisplayInfo::sourceBipolar --
 *  true swings the band both sides of baseNorm (an LFO); false only rises from it, following amount's
 *  own sign the way a unipolar envelope's CV does (an unplugged/negative amount still reads as
 *  "downward reach", never wrapped to the bipolar case). */
inline ModDepthBandRange modDepthBandRange(float baseNorm, float amount, bool bipolar) {
    float lo, hi;
    if (bipolar) {
        const float span = std::abs(amount);
        lo = baseNorm - span;
        hi = baseNorm + span;
    } else if (amount >= 0.0f) {
        lo = baseNorm;
        hi = baseNorm + amount;
    } else {
        lo = baseNorm + amount;
        hi = baseNorm;
    }
    return {juce::jlimit(0.0f, 1.0f, lo), juce::jlimit(0.0f, 1.0f, hi)};
}

/** Which ring colour token the band should borrow: positive except the unipolar-negative-amount
 *  case (a unipolar source dialled to pull DOWN from rest), matching drawModulationRing's own
 *  positive/negative split. */
inline bool modDepthBandUsesNegativeColour(float amount, bool bipolar) { return !bipolar && amount < 0.0f; }

} // namespace synth::ui
