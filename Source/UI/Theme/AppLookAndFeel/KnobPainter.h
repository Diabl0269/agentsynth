#pragma once

#include "UI/Theme/KnobStyle.h"
#include "UI/Theme/Theme.h"

namespace synth::theme {

// Paints one knob in `style` inside `knobBounds` (the knob is the largest centred square).
// `pos01` is the value 0..1 over the shared 270 degree sweep; `valueColour` colours the value arc.
// The app's rotary sliders and the Settings picker previews both call this, so preview == app.
void paintKnob(juce::Graphics& g, const Theme& theme, KnobStyle style, juce::Rectangle<float> knobBounds, float pos01,
               juce::Colour valueColour);

} // namespace synth::theme
