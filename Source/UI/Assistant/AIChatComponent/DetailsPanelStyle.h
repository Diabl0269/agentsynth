#pragma once

#include "UI/Theme/Theme.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth {

// The look of the read-only panel under a card's Details (the edit-plan card and the built song card share it, so the
// two read alike): 12 px text in the theme's text colour on a faint inset, a hairline border, even padding and open
// line spacing.
namespace detailspanel {

inline constexpr float kFontSize = 12.0f;
inline constexpr float kLineSpacing = 1.25f;
inline constexpr int kIndentX = 10;
inline constexpr int kIndentY = 8;
inline constexpr int kMinHeight = 56;

/** Gives `panel` the shared look from the theme's tokens; call it again after a theme change. */
void style(juce::TextEditor& panel, const synth::theme::Colors& colours);

/** The height a panel needs for `lines` wrapped at `width`, never over `maxHeight` and never under kMinHeight. */
int heightFor(const juce::StringArray& lines, int width, int maxHeight);

} // namespace detailspanel

} // namespace synth
