#include "UI/Layout/FocusRing.h"

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

#include <cmath>

namespace synth::ui {

float focusRingThickness(const synth::theme::Theme& theme) {
    return theme.metrics.borderWidth * (theme.isDark ? 1.5f : 2.5f);
}

float focusRingInset(const synth::theme::Theme& theme) { return focusRingThickness(theme) + kFocusRingGap; }

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
bool& focusFillStepShownForTest() {
    static bool shown = false;
    return shown;
}
} // namespace

void setFocusFillStepShownForTest(bool shown) { focusFillStepShownForTest() = shown; }

// Within 0.12 per channel (RGB, 0..1): a fill that close to the accent leaves the ring no contrast to read by.
bool fillHidesFocusRing(juce::Colour fill, const synth::theme::Theme& theme) {
    constexpr float kNear = 0.12f;
    const auto accent = theme.colors.accent;
    return fill.getAlpha() > 0x80 && std::abs(fill.getFloatRed() - accent.getFloatRed()) < kNear &&
           std::abs(fill.getFloatGreen() - accent.getFloatGreen()) < kNear &&
           std::abs(fill.getFloatBlue() - accent.getFloatBlue()) < kNear;
}

bool showsFocusFillStep(const juce::Component& comp) {
    if (comp.getProperties()[kFocusFillHasOwnBandProperty])
        return false;
    if (focusFillStepShownForTest())
        return true;
    const juce::Component* owner = &comp;
    if (comp.getProperties()[kFocusRingOwnerIsParentProperty] && comp.getParentComponent() != nullptr)
        owner = comp.getParentComponent();
    return showsFocusRing(*owner);
}

float focusFillInset(const juce::Component& comp, const synth::theme::Theme& theme) {
    return showsFocusFillStep(comp) ? focusRingThickness(theme) + kFocusFillGap : 0.0f;
}

juce::Rectangle<float> focusSteppedFill(const juce::Component& comp, const synth::theme::Theme& theme,
                                        juce::Rectangle<float> fill, juce::Rectangle<float> ringArea, float amount) {
    const float inset = focusFillInset(comp, theme) * juce::jlimit(0.0f, 1.0f, amount);
    if (inset <= 0.0f)
        return fill;
    const auto stepped = fill.getIntersection(ringArea.reduced(inset));
    return stepped.isEmpty() ? fill : stepped;
}

void setFocusRingsShownForTest(bool shown) { focusRingsShownForTest() = shown; }

bool showsFocusRing(const juce::Component& comp) { return comp.hasKeyboardFocus(false) || focusRingsShownForTest(); }

void paintFocusRing(juce::Graphics& g, juce::Rectangle<float> area, const juce::Component& comp, float cornerRadius) {
    if (showsFocusRing(comp))
        paintFocusRingAlways(g, area, comp, cornerRadius);
}

} // namespace synth::ui
