#pragma once

// MorphMapPalette.h -- the colour a shape wears wherever it appears (its dot on the morph map, its chip in the
// shape editor), chosen by its index from theme tokens (or the colour the user picked for it) so the two always agree.
#include "UI/Theme/Theme.h"
#include <juce_graphics/juce_graphics.h>
#include <optional>

namespace synth::ui {

inline juce::Colour morphShapeColour(const synth::theme::Colors& colors, int shapeIndex) {
    const juce::Colour palette[] = {colors.accent,    colors.hueAmber, colors.hueGreen, colors.hueRose,
                                    colors.hueViolet, colors.warning,  colors.success,  colors.modRingNegative};
    const int count = (int)std::size(palette);
    return palette[((shapeIndex % count) + count) % count];
}

/** The colour a shape wears: the one the user gave it (an ARGB), else its palette colour by index. */
inline juce::Colour morphShapeColour(const synth::theme::Colors& colors, int shapeIndex,
                                     const std::optional<juce::uint32>& chosen) {
    return chosen.has_value() ? juce::Colour(*chosen) : morphShapeColour(colors, shapeIndex);
}

} // namespace synth::ui
