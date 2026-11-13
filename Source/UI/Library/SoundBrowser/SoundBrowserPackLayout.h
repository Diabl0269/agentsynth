#pragma once

// Where the Download button sits in a pack row. One place, so what is painted and what a click hits cannot differ.

#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

constexpr int kSoundRowSideMargin = 12;
constexpr int kSoundRowIconSize = 14;
constexpr int kSoundRowIconGap = 8;
/** Where a row's name starts: past the icon cell and the gap after it. */
constexpr int kSoundRowTextLeft = kSoundRowSideMargin + kSoundRowIconSize + kSoundRowIconGap;
/** The alpha of a greyed row (a pack not downloaded) and of a folder that is not there. */
constexpr float kGreyedAlpha = 0.55f;
constexpr int kPackButtonWidth = 66;
constexpr int kPackButtonHeight = 22;

/** The icon cell of a row occupying `row`: 14 px square at the side margin, centred vertically. */
inline juce::Rectangle<float> rowIconCell(juce::Rectangle<int> row) {
    return {(float)(row.getX() + kSoundRowSideMargin), (float)row.getCentreY() - kSoundRowIconSize / 2.0f,
            (float)kSoundRowIconSize, (float)kSoundRowIconSize};
}

/** The button inside a row occupying `row`, at its right edge. */
inline juce::Rectangle<int> packButtonIn(juce::Rectangle<int> row) {
    return {row.getRight() - kSoundRowSideMargin - kPackButtonWidth, row.getCentreY() - kPackButtonHeight / 2,
            kPackButtonWidth, kPackButtonHeight};
}

} // namespace synth::ui
