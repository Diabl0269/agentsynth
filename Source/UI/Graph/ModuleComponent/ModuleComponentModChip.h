#pragma once

// FRO288: the small "<source> · <+NN%>" chip drawn under a knob's value box while its routing is
// hover-correlated (docs/modules/modulation.md#modulation-rings-on-knobs). Split into a free
// function so the text formatting is unit-testable with no ModuleComponent/LookAndFeel involved
// (Tests/UI/Graph/ModuleComponent/ModuleComponentModChipTests.cpp) -- same pattern as
// ModuleComponentModBand.h's modDepthBandRange.

#include <juce_core/juce_core.h>

namespace synth::ui {

/** "<sourceName> <middle-dot> <signed percent>%", e.g. "LFO 1 \xC2\xB7 +63%" / "Env 2 \xC2\xB7 -40%".
 *  amount is the attenuverter's "amount" (or 1.0 for a DirectCV/PolyBus routing), real range -1..1.
 *  The percent is round(amount*100): positive gets an explicit "+" (negative already carries its
 *  own "-"; zero gets neither). juce::String::fromUTF8 avoids the non-ASCII-literal guard -- see
 *  scripts/tests/check-nonascii-literals.test.sh. */
inline juce::String formatModHoverChipText(const juce::String& sourceName, float amount) {
    const int pct = juce::roundToInt(amount * 100.0f);
    const juce::String pctStr = pct > 0 ? ("+" + juce::String(pct)) : juce::String(pct);
    static const juce::String kMiddleDot = juce::String::fromUTF8("\xC2\xB7");
    return sourceName + " " + kMiddleDot + " " + pctStr + "%";
}

} // namespace synth::ui
