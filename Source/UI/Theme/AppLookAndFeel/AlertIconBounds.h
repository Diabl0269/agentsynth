#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

/** Side of the square an alert box's icon is drawn in. */
constexpr int kAlertIconSize = 36;

/** Where AppLookAndFeel::drawAlertBox draws the title and message, from the window's top-left corner. */
constexpr int kAlertTextLeft = 81;
constexpr int kAlertTextTop = 30;

/** Clear space between the icon's right edge and the text. */
constexpr int kAlertIconGap = 18;

/** The vertical span, in window coordinates, of the first line (the title) of an alert's `text`. */
juce::Range<float> alertTitleLineSpan(const juce::TextLayout& text);

/** The icon's square: left of the text, centred on the title line, never outside `windowBounds`. */
juce::Rectangle<int> alertIconBounds(const juce::Rectangle<int>& windowBounds, const juce::TextLayout& text);

/** Warning colour for a warning, accent otherwise, made readable (3:1) against `background`. */
juce::Colour alertIconColour(juce::MessageBoxIconType type, juce::Colour accent, juce::Colour warning,
                             juce::Colour background);

} // namespace synth::ui
