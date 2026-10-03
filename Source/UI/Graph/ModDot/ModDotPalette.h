#pragma once

// The colours and fonts of the mod dot's panel. A parentless CallOutBox is a window of its own and does not
// inherit the LookAndFeel the main window carries, so every piece of the panel paints itself from these tokens
// (like ModMatrixPicker); with no themed LookAndFeel, e.g. a headless test, plain colours stand in.

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

struct ModDotPalette {
    juce::Colour panel = juce::Colours::darkgrey.darker(0.4f); // surface
    juce::Colour hover = juce::Colours::darkgrey;              // surface-hi
    juce::Colour border = juce::Colours::grey.darker();
    juce::Colour field = juce::Colours::black.withAlpha(0.35f); // bg-0: bar track and search field
    juce::Colour text = juce::Colours::white;
    juce::Colour muted = juce::Colours::grey;
    juce::Colour disabled = juce::Colours::grey.darker();
    juce::Colour accent = juce::Colours::lightblue;
    juce::Colour positive = juce::Colour(0xff00E5FF); // modRingPositive
    juce::Colour negative = juce::Colour(0xffFF6E00); // modRingNegative
    float radius = 6.0f;
    juce::String monoFamily = juce::Font::getDefaultMonospacedFontName();

    juce::Colour swatchFor(float amount) const { return amount >= 0.0f ? positive : negative; }
    juce::Font mono(float size, int style = juce::Font::plain) const {
        return juce::Font(juce::FontOptions(monoFamily, size, style));
    }
};

inline ModDotPalette modDotPaletteFor(const juce::Component& comp) {
    ModDotPalette p;
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&comp.getLookAndFeel())) {
        const auto& theme = lf->getTheme();
        const auto& c = theme.colors;
        p.panel = c.surface;
        p.hover = c.surfaceHi;
        p.border = c.border;
        p.field = c.bg0;
        p.text = c.textPrimary;
        p.muted = c.textMuted;
        p.disabled = c.textDisabled;
        p.accent = c.accent;
        p.positive = c.modRingPositive;
        p.negative = c.modRingNegative;
        p.radius = theme.metrics.cornerRadius;
        p.monoFamily = theme.type.monoFamily;
    }
    return p;
}

} // namespace synth::ui
