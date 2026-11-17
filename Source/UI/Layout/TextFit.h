#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

/** True when `text` in `font` is wider than `width` pixels, so a single line of it is drawn cut off. */
inline bool textIsCut(const juce::Font& font, const juce::String& text, int width) {
    return juce::GlyphArrangement::getStringWidthInt(font, text) > width;
}

} // namespace synth::ui
