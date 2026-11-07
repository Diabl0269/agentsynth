#pragma once

// The stack of "<source> · <+NN%>" chips drawn under a knob's value box, one row per modulation source on it,
// while its routing is hover-correlated or a mod dot readout is up on it
// (docs/modules/modulation.md#modulation-rings-on-knobs). Split into free
// functions so the text formatting and the row list are unit-testable with no ModuleComponent/LookAndFeel involved
// (Tests/UI/Graph/ModuleComponent/ModuleComponentModChipTests.cpp) -- same pattern as
// ModuleComponentModBand.h's modDepthBandRange.

#include "UI/Graph/ModDot/KnobModSources.h"
#include "UI/Layout/UIAnimation.h"
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <vector>

class GraphEditor;
class ModuleBase;
struct ModulationTarget;

namespace synth::ui {

/** Whether `card` draws the chip stack now: its routing is hover-correlated, or a mod dot readout is up on it. */
bool modHoverChipShows(GraphEditor& editor, juce::AudioProcessorGraph::NodeID card);

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

/** One row of the chip stack. */
struct ModChipRow {
    juce::String text;
    bool bold = false;    // the source the mod dot's drag adjusts
    bool positive = true; // swatch colour: modRingPositive, else modRingNegative
    bool isMore = false;  // the trailing "+N more" row (no swatch)
};

/** What a card paints under one knob: the rows, which of the card's sliders they hang under (-1: none) and the
 *  opacity (a dot readout's fade; 1 while the pointer rests on the knob). */
struct ModChipStack {
    std::vector<ModChipRow> rows;
    int sliderIndex = -1;
    float alpha = 1.0f;
};

/** The rows for `sources` (bypassed and unnamed ones skipped), at most `maxRows` sources and then one
 *  "+N more" row. The `chosen` source is bold and always listed: past the cap it takes the last slot. */
std::vector<ModChipRow> modHoverChipRows(const std::vector<KnobModSource>& sources,
                                         juce::AudioProcessorGraph::NodeID chosen, int maxRows = 4);

/** Where each row of a stack goes, in card coordinates: `widths` are the rows' widths, centred on `knob` (the control
 *  they hang under) and kept inside `card`. The stack sits below the knob; when that would run past the card's bottom
 *  edge it sits above the knob, and when the card is too short for that too it is pushed up to fit. */
std::vector<juce::Rectangle<float>> modChipRowRects(juce::Rectangle<float> knob, const std::vector<float>& widths,
                                                    juce::Rectangle<float> card);

/** The rows of `stack` as `card` paints them, in card coordinates, hung under `knob` (the stack's slider's bounds):
 *  `modChipRowRects` with the rows' widths in the theme's chip font; empty with no rows or no app look and feel. */
std::vector<juce::Rectangle<float>> modHoverChipRects(const juce::Component& card, const ModChipStack& stack,
                                                      juce::Rectangle<int> knob);

/** The stack `card` paints now: empty unless its routing is hover-correlated or a dot readout is up on it. A dragged
 *  source shows the readout's live amount. `mod` is the card's module; `sliderFor` maps one of its modulation targets
 * to the index of the slider the rows hang under (-1: none). */
ModChipStack modChipStack(GraphEditor& editor, juce::AudioProcessorGraph::NodeID card, ModuleBase* mod,
                          const std::function<int(const ModulationTarget&)>& sliderFor);

/** The chip's own shown-fade: 160 ms in, 110 ms out (a plain fade under Reduce Motion, instant with Animations Off).
 *  It keeps the last stack's rows through the fade-out so the chip fades with its text, and repaints `card` on every
 *  frame. */
class ModChipFade {
public:
    static constexpr double kFadeInMs = 160.0;
    static constexpr double kFadeOutMs = 110.0;

    explicit ModChipFade(juce::Component& card);

    /** Takes what the card should show now (`now`, as modChipStack gives it) and returns what to paint: `now` at the
     *  fade's opacity while it shows, the last rows fading out once it does not. Rows are empty once faded out. */
    ModChipStack follow(const ModChipStack& now);

    float alpha() const noexcept { return fade_.alpha(); }
    /** Test seam: advances the running fade to `ms` after its start. */
    void stepForTest(double ms) { fade_.stepForTest(ms); }

private:
    ShownFade fade_;
    ModChipStack last_;
    float lastNowAlpha_ = 1.0f;
    bool shown_ = false;
};

} // namespace synth::ui
