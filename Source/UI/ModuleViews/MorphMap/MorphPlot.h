#pragma once

// MorphPlot.h -- what every dark square plot with a ring cursor shares: the filter's morph map and the Macros card's XY
// pad paint the plot, the 4x4 grid and the cursor, and turn a pointer position into 0..1 coordinates, through these
// helpers, so the two can never drift apart.
#include "UI/Theme/Theme.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui::morphplot {

inline constexpr float kCornerRadius = 6.0f;
inline constexpr float kCursorRadius = 6.5f;

/** The dark rounded plate with its border, and the plot's 4x4 grid (three lines each way) inside `plot`. */
void paintPlot(juce::Graphics& g, juce::Rectangle<float> plate, juce::Rectangle<float> plot,
               const synth::theme::Colors& colors);

/** The ring cursor: a dark disc with a bright ring, centred on `centre`, at `alpha` (0..1). */
void paintCursor(juce::Graphics& g, juce::Point<float> centre, float alpha, const synth::theme::Colors& colors);

/** `pixels` in `plot` as 0..1 coordinates (x to the right, y downwards), clamped; the middle for an empty plot. */
juce::Point<float> toUnit(juce::Rectangle<float> plot, juce::Point<float> pixels);

/** The inverse of toUnit, unclamped. */
juce::Point<float> toPixels(juce::Rectangle<float> plot, juce::Point<float> unit);

} // namespace synth::ui::morphplot
