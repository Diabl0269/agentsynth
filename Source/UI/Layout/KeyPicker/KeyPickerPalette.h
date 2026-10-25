#pragma once

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui::keypicker {

struct Palette {
    juce::Colour panel, border, field, text, muted, accent, whiteKey, blackKey;
    float radius = 6.0f;
};

// The picker's colours from the theme `comp` paints with (themeOf: the default theme outside any AppLookAndFeel).
inline Palette paletteFor(const juce::Component& comp) {
    const auto& theme = synth::theme::themeOf(comp);
    const auto& c = theme.colors;
    return {c.surface,
            c.border,
            c.bg0,
            c.textPrimary,
            c.textMuted,
            c.accent,
            c.surfaceHi.brighter(0.5f),
            c.bg0,
            theme.metrics.cornerRadius};
}

} // namespace synth::ui::keypicker
