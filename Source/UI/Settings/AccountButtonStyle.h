#pragma once

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth {

// Paints a text button as a destructive action: the theme's error colour for the text and a faint wash of it
// behind. Call it again from lookAndFeelChanged() so a theme switch re-colours it.
inline void applyDestructiveLook(juce::TextButton& button) {
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&button.getLookAndFeel());
    const juce::Colour error = lf != nullptr ? lf->getTheme().colors.error : juce::Colour(0xffE5484D);
    button.setColour(juce::TextButton::buttonColourId, error.withAlpha(0.18f));
    button.setColour(juce::TextButton::buttonOnColourId, error.withAlpha(0.30f));
    button.setColour(juce::TextButton::textColourOffId, error);
    button.setColour(juce::TextButton::textColourOnId, error);
}

} // namespace synth
