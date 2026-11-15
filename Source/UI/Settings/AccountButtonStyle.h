#pragma once

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/ReadableText.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth {

// Paints a text button as a destructive action: the theme's error colour for the text and a faint wash of it
// behind. Call it again from lookAndFeelChanged() so a theme switch re-colours it.
inline void applyDestructiveLook(juce::TextButton& button) {
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&button.getLookAndFeel());
    const juce::Colour error = lf != nullptr ? lf->getTheme().colors.error : juce::Colour(0xffE5484D);
    button.setColour(juce::TextButton::buttonColourId, error.withAlpha(0.18f));
    button.setColour(juce::TextButton::buttonOnColourId, error.withAlpha(0.30f));
    // The text keeps the error hue but moves toward white or black until it reads (4.5:1) on the wash over every
    // surface the button can sit on.
    juce::Colour text = error;
    if (lf != nullptr) {
        const auto& c = lf->getTheme().colors;
        std::vector<juce::Colour> fills;
        for (const auto under : {c.bg0, c.bg1, c.surface, c.surfaceHi})
            for (const float alpha : {0.18f, 0.30f})
                fills.push_back(under.overlaidWith(error.withAlpha(alpha)).withAlpha(1.0f));
        text = synth::theme::nudgeToReadable(error, fills);
    }
    button.setColour(juce::TextButton::textColourOffId, text);
    button.setColour(juce::TextButton::textColourOnId, text);
}

} // namespace synth
