#pragma once

// Where the Download button sits in a pack row. One place, so what is painted and what a click hits cannot differ.

#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

constexpr int kSoundRowSideMargin = 12;
constexpr int kPackButtonWidth = 66;
constexpr int kPackButtonHeight = 22;

/** The button inside a row occupying `row`, at its right edge. */
inline juce::Rectangle<int> packButtonIn(juce::Rectangle<int> row) {
    return {row.getRight() - kSoundRowSideMargin - kPackButtonWidth, row.getCentreY() - kPackButtonHeight / 2,
            kPackButtonWidth, kPackButtonHeight};
}

} // namespace synth::ui
