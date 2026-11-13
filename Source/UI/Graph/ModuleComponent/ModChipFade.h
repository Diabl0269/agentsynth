#pragma once

// The mod hover chip's rows and its shown-fade. Kept apart from ModuleComponentModChip.h so ModRingFades.h (reached
// from every card) does not pull in the chip's source-list header.

#include "UI/Layout/UIAnimation.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace synth::ui {

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

/** The chip's own shown-fade: 220 ms in, 160 ms out, slower than a tooltip's 160/110 because the chip shows the instant
 *  the pointer rests on a knob, with no hover delay to soften its entrance (a plain fade under Reduce Motion, instant
 * with Animations Off). It keeps the last stack's rows through the fade-out so the chip fades with its text, and
 * repaints `card` on every frame. */
class ModChipFade {
public:
    static constexpr double kFadeInMs = 220.0;
    static constexpr double kFadeOutMs = 160.0;

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
