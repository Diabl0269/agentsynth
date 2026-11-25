#pragma once

// ThemeSweepToolbarHelpers.h -- what the toolbar-icon theme tests share (ThemeSweepToolbarIconTests.cpp and the mixer
// toolbar test in MixerSectionPanelTests.cpp): a root that carries a sweep theme while the Desktop default look and
// feel stands for another, and pixel counting over a rendered button.

#include "ThemeSweepSupport.h"
#include "UI/Chrome/ToolbarButton/ToolbarButton.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace synth::test::theme_sweep {

// Pixels of exactly `colour`: the glyph's strokes are thin, so a fully covered pixel of the hue is the mark of the
// theme's colour (an anti-aliased edge blends it with its neighbours and never equals it).
inline int solidPixelsOf(const juce::Image& image, juce::Colour colour) {
    int count = 0;
    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = 0; x < image.getWidth(); ++x)
            if (image.getPixelAt(x, y) == colour)
                ++count;
    return count;
}

inline void collectToolbarButtons(juce::Component& parent, std::vector<synth::ui::ToolbarButton*>& out) {
    for (auto* child : parent.getChildren()) {
        if (auto* button = dynamic_cast<synth::ui::ToolbarButton*>(child))
            out.push_back(button);
        collectToolbarButtons(*child, out);
    }
}

// The Desktop default look and feel stands for the app's default theme; the root carries the sweep theme.
struct ThemedRoot {
    ThemedRoot(const synth::theme::Theme& theme, const synth::theme::Theme& defaultTheme)
        : previous(&juce::Desktop::getInstance().getDefaultLookAndFeel()) {
        defaultLook.applyTheme(defaultTheme);
        look.applyTheme(theme);
        juce::Desktop::getInstance().setDefaultLookAndFeel(&defaultLook);
        root.setLookAndFeel(&look);
        root.setSize(400, 80);
    }
    ~ThemedRoot() {
        root.deleteAllChildren();
        root.setLookAndFeel(nullptr);
        juce::Desktop::getInstance().setDefaultLookAndFeel(previous);
    }
    synth::theme::AppLookAndFeel defaultLook;
    synth::theme::AppLookAndFeel look;
    juce::Component root;
    juce::LookAndFeel* previous;
};

inline const synth::theme::Theme& otherTheme(const std::vector<synth::theme::Theme>& themes, size_t i) {
    for (size_t k = 1; k < themes.size(); ++k)
        if (themes[(i + k) % themes.size()].id != themes[i].id &&
            themes[(i + k) % themes.size()].colors.hueAmber != themes[i].colors.hueAmber)
            return themes[(i + k) % themes.size()];
    return themes[(i + 1) % themes.size()];
}

} // namespace synth::test::theme_sweep
