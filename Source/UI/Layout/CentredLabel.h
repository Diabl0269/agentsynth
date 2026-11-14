#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

/** Draws `text` in the graphics' current colour so its ink (the glyph outlines, not the line box) is centred on
 *  `centre`, a float point. For numbers inside dots and rings. */
void drawLabelCentredOn(juce::Graphics& g, const juce::String& text, const juce::Font& font, juce::Point<float> centre);

} // namespace synth::ui
