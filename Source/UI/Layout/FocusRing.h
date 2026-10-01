#pragma once

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

// Strokes the same ring as paintFocusRing without asking whether `comp` holds focus, for a caller that
// decides that itself (a ring that follows a tracked item rather than the component, or a test with no
// native window to take focus). `comp` only supplies the theme.
inline void paintFocusRingAlways(juce::Graphics& g, juce::Rectangle<float> area, const juce::Component& comp,
                                 float cornerRadius = 0.0f) {
    juce::Colour colour = juce::Colours::orange;
    float thickness = 1.5f;
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&comp.getLookAndFeel())) {
        colour = lf->getTheme().colors.accent;
        thickness = lf->getTheme().metrics.borderWidth * 1.5f;
    }
    g.setColour(colour);
    const auto ring = area.reduced(thickness * 0.5f);
    if (cornerRadius > 0.0f)
        g.drawRoundedRectangle(ring, cornerRadius, thickness);
    else
        g.drawRect(ring, thickness);
}

// Strokes the keyboard-focus ring of a small control: a solid, full-alpha `accent` ring (1.5x the
// theme's border weight) around `area`, inset by half its thickness so it never clips against the
// component's own edge. Draws nothing unless `comp` itself holds keyboard focus (descendants do not
// count). `cornerRadius` 0 draws a square ring. The colour comes from the component's AppLookAndFeel
// theme, falling back to orange when it has none. Use this for buttons, toggles, knobs and sliders;
// whole panels use paintFocusRegionOutline (FocusRegion.h), which is softer.
inline void paintFocusRing(juce::Graphics& g, juce::Rectangle<float> area, const juce::Component& comp,
                           float cornerRadius = 0.0f) {
    if (comp.hasKeyboardFocus(false))
        paintFocusRingAlways(g, area, comp, cornerRadius);
}

} // namespace synth::ui
