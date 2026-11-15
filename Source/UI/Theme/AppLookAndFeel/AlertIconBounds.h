#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

/** Side of the square (the lit disc) an alert box's icon is drawn in. */
constexpr int kAlertIconSize = 64;

/** The icon column JUCE builds an alert for: its text starts here (80 px of icon space plus the 1 px border). */
constexpr int kJuceAlertTextLeft = 81;

/** Where AppLookAndFeel::drawAlertBox draws the title and message, from the window's top-left corner. The icon
    column is one token: this left edge and the room added to the window (`kAlertColumnGrowth`) move together. */
constexpr int kAlertTextLeft = 109;
constexpr int kAlertTextTop = 30;

/** How much wider than JUCE's own layout an alert with an icon is made, so the text keeps its right margin. */
constexpr int kAlertColumnGrowth = kAlertTextLeft - kJuceAlertTextLeft;

/** Clear space between the icon's right edge and the text. */
constexpr int kAlertIconGap = 18;

/** The vertical span, in window coordinates, of the first line (the title) of an alert's `text`. */
juce::Range<float> alertTitleLineSpan(const juce::TextLayout& text);

/** The y, from the window's top, of the icon's centre: the gap just under the title line, so the disc reaches down
    past the title toward the first message line (macOS and Logic do the same). */
int alertIconCentreY(const juce::TextLayout& text);

/** The lowest y the icon can reach for a one-line title, before the real layout is known (window sizing). */
constexpr int kAlertIconBottomEstimate = kAlertTextTop + 17 - 1 + kAlertIconSize / 2;

/** The icon's square: left of the text, centred on the gap under the title line, never outside `windowBounds`. */
juce::Rectangle<int> alertIconBounds(const juce::Rectangle<int>& windowBounds, const juce::TextLayout& text);

/** Makes a freshly built alert fit its icon: JUCE lays it out for an 80 px icon column and a short title-height icon,
    so the window grows by `kAlertColumnGrowth` (its children shift half of it, staying centred) and, when the text is
    short, taller (the buttons move down) until the disc clears them. No icon: untouched. */
void makeRoomForAlertIcon(juce::AlertWindow& alert, juce::MessageBoxIconType iconType);

/** Warning colour for a warning, accent otherwise, made readable (3:1) against `background`. */
juce::Colour alertIconColour(juce::MessageBoxIconType type, juce::Colour accent, juce::Colour warning,
                             juce::Colour background);

} // namespace synth::ui
