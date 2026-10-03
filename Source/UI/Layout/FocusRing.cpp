#include "UI/Layout/FocusRing.h"

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

float focusRingThickness(const synth::theme::Theme& theme) {
    return theme.metrics.borderWidth * (theme.isDark ? 1.5f : 2.5f);
}

void paintFocusRingAlways(juce::Graphics& g, juce::Rectangle<float> area, const juce::Component& comp,
                          float cornerRadius) {
    juce::Colour colour = juce::Colours::orange;
    float thickness = 1.5f;
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&comp.getLookAndFeel())) {
        colour = lf->getTheme().colors.accent;
        thickness = focusRingThickness(lf->getTheme());
    }
    g.setColour(colour);
    const auto ring = area.reduced(thickness * 0.5f);
    if (cornerRadius > 0.0f)
        g.drawRoundedRectangle(ring, cornerRadius, thickness);
    else
        g.drawRect(ring, thickness);
}

void paintFocusRing(juce::Graphics& g, juce::Rectangle<float> area, const juce::Component& comp, float cornerRadius) {
    if (comp.hasKeyboardFocus(false))
        paintFocusRingAlways(g, area, comp, cornerRadius);
}

} // namespace synth::ui
