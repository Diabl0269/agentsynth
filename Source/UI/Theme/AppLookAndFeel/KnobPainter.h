#pragma once

#include "UI/Theme/KnobStyle.h"
#include "UI/Theme/Theme.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::theme {

// The Neon style glows in every theme: the bloom strength is the theme's glow, but never below this
// floor, and it grows with the value (faint near zero, strong at full).
inline constexpr float kNeonMinGlow = 0.6f;
inline float neonGlowStrength(const Theme& theme, float pos01) noexcept {
    const float glow = theme.treatment.glow > kNeonMinGlow ? theme.treatment.glow : kNeonMinGlow;
    const float p = pos01 < 0.0f ? 0.0f : (pos01 > 1.0f ? 1.0f : pos01);
    return glow * (0.3f + 0.7f * p);
}

// Analog prints its scale numbers only on a knob at least this wide: never at card size (54 px) or below.
inline constexpr float kAnalogNumbersMinSize = 55.0f;

// Paints one knob in `style` inside `knobBounds` (the knob is the largest centred square).
// `pos01` is the value 0..1 over the shared 270 degree sweep; `valueColour` colours the value arc.
// `origin01` is where the value arc starts: 0 for an ordinary knob, 0.5 for a bipolar one (drawn from
// 12 o'clock out to either side). The app's rotary sliders and the Settings picker previews both call
// this, so preview == app.
void paintKnob(juce::Graphics& g, const Theme& theme, KnobStyle style, juce::Rectangle<float> knobBounds, float pos01,
               juce::Colour valueColour, float origin01 = 0.0f);

// Where a rotary slider's value arc starts: 12 o'clock (its proportion of zero) when its range is
// symmetric round zero (pan, pitch, octave, fine tune), else 0.
float knobOriginFor(const juce::Slider& slider);

} // namespace synth::theme
