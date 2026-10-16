#pragma once

// MorphMapPalette.h -- the colour a shape wears wherever it appears (its dot on the morph map, its chip in the
// shape editor), chosen by its index from theme tokens so the two always agree.
#include "UI/Theme/Theme.h"
#include <juce_graphics/juce_graphics.h>

namespace synth::ui {

inline juce::Colour morphShapeColour(const synth::theme::Colors& colors, int shapeIndex) {
    const juce::Colour palette[] = {colors.accent,    colors.hueAmber, colors.hueGreen, colors.hueRose,
                                    colors.hueViolet, colors.warning,  colors.success,  colors.modRingNegative};
    const int count = (int)std::size(palette);
    return palette[((shapeIndex % count) + count) % count];
}

} // namespace synth::ui
