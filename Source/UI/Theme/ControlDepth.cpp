#include "ControlDepth.h"
#include "UI/Theme/CardDepth.h"

namespace synth::theme {

namespace {
constexpr float kRichShadowDeeper = 1.5f;
constexpr float kRichShadowReach = 1.6f;
constexpr float kShadowCeiling = 0.95f;
constexpr float kRimBoost = 2.0f; // a 1px line on a small cap needs more than a card's long edge does
constexpr float kRimWidth = 1.2f;
} // namespace

bool capIsFlat(const Theme& theme) noexcept { return theme.treatment.depth == DepthLevel::Flat; }

float capShadowAlpha(float base, const Theme& theme) noexcept {
    switch (theme.treatment.depth) {
    case DepthLevel::Flat:
        return 0.0f;
    case DepthLevel::Soft:
        return base;
    case DepthLevel::Rich:
        return juce::jmin(juce::jmax(base, kShadowCeiling), base * kRichShadowDeeper);
    }
    return base;
}

float capShadowReach(const Theme& theme) noexcept {
    switch (theme.treatment.depth) {
    case DepthLevel::Flat:
        return 0.0f;
    case DepthLevel::Soft:
        return 1.0f;
    case DepthLevel::Rich:
        return kRichShadowReach;
    }
    return 1.0f;
}

void paintCapRim(juce::Graphics& g, const juce::Path& outline, juce::Rectangle<float> bounds, const Theme& theme) {
    if (theme.treatment.depth != DepthLevel::Rich || outline.isEmpty())
        return;
    const auto rim = cardDepthTones(theme).rimTop.withMultipliedAlpha(kRimBoost);
    juce::ColourGradient light(rim, bounds.getTopLeft(), rim.withAlpha(0.0f), bounds.getBottomRight(), false);
    light.addColour(0.55, rim.withAlpha(0.0f));
    const juce::Graphics::ScopedSaveState state(g);
    g.setGradientFill(light);
    g.strokePath(outline, juce::PathStrokeType(kRimWidth));
}

void paintCapRimDisc(juce::Graphics& g, juce::Rectangle<float> disc, const Theme& theme) {
    if (theme.treatment.depth != DepthLevel::Rich)
        return;
    juce::Path outline;
    outline.addEllipse(disc.reduced(kRimWidth * 0.5f));
    paintCapRim(g, outline, disc, theme);
}

void paintCapRimRect(juce::Graphics& g, juce::Rectangle<float> cap, float radius, const Theme& theme) {
    if (theme.treatment.depth != DepthLevel::Rich)
        return;
    juce::Path outline;
    outline.addRoundedRectangle(cap.reduced(kRimWidth * 0.5f), juce::jmax(0.0f, radius - kRimWidth * 0.5f));
    paintCapRim(g, outline, cap, theme);
}

} // namespace synth::theme
