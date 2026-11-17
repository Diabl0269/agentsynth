#include "UI/Layout/FocusRing.h"

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

float focusRingThickness(const synth::theme::Theme& theme) {
    return theme.metrics.borderWidth * (theme.isDark ? 1.5f : 2.5f);
}

void paintFocusRingForTheme(juce::Graphics& g, const synth::theme::Theme& theme, juce::Rectangle<float> area,
                            float cornerRadius) {
    const float thickness = focusRingThickness(theme);
    g.setColour(theme.colors.accent);
    const auto ring = area.reduced(thickness * 0.5f);
    if (cornerRadius > 0.0f)
        g.drawRoundedRectangle(ring, cornerRadius, thickness);
    else
        g.drawRect(ring, thickness);
}

void paintFocusRingAlways(juce::Graphics& g, juce::Rectangle<float> area, const juce::Component& comp,
                          float cornerRadius) {
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&comp.getLookAndFeel())) {
        paintFocusRingForTheme(g, lf->getTheme(), area, cornerRadius);
        return;
    }
    g.setColour(juce::Colours::orange);
    const auto ring = area.reduced(0.75f);
    if (cornerRadius > 0.0f)
        g.drawRoundedRectangle(ring, cornerRadius, 1.5f);
    else
        g.drawRect(ring, 1.5f);
}

namespace {
bool& focusRingsShownForTest() {
    static bool shown = false;
    return shown;
}
} // namespace

void setFocusRingsShownForTest(bool shown) { focusRingsShownForTest() = shown; }

bool showsFocusRing(const juce::Component& comp) { return comp.hasKeyboardFocus(false) || focusRingsShownForTest(); }

void paintFocusRing(juce::Graphics& g, juce::Rectangle<float> area, const juce::Component& comp, float cornerRadius) {
    if (showsFocusRing(comp))
        paintFocusRingAlways(g, area, comp, cornerRadius);
}

} // namespace synth::ui
