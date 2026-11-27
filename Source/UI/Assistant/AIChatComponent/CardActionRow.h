#pragma once

#include "UI/Theme/Theme.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui::cardactions {

constexpr int kRowHeight = 28;
constexpr int kButtonHeight = 24;
constexpr int kButtonGap = 4;

// What the one placement rule needs of each piece; the rule itself is in CardActionRow.cpp.
struct Row {
    juce::Component* link = nullptr;   // quiet text link or stop button at the far left, or null
    juce::Component* middle = nullptr; // outlined second action left of the primary, or null
    int middleWidth = 0;
    juce::Component* primary = nullptr; // accent-filled action in the right corner, or null
    int primaryWidth = 0;
    int linkWidth = 0; // the link's ideal width, with its side padding
};

/** Sets the bounds of the row's pieces inside `rowRect` (kRowHeight tall); never wraps. */
void layout(juce::Rectangle<int> rowRect, const Row& row);

/** Width of `button`'s text at the button font plus `pad` on each side, at least `minWidth`. */
int fitWidth(const juce::TextButton& button, int pad, int minWidth = 0);

/** Transparent fill with a 1 px theme border, for the second action; call again after a theme change. */
void applyOutlinedStyle(juce::TextButton& button, const synth::theme::Colors& colours);

} // namespace synth::ui::cardactions
