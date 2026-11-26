#include "CardDepth.h"

namespace synth::theme {

CardDepthTones cardDepthTones(const Theme& theme) {
    const auto& c = theme.colors;
    CardDepthTones t;
    if (theme.isDark) {
        const auto light = c.textPrimary.withAlpha(1.0f);
        t.rimTop = light.withAlpha(0.22f);
        t.shade = juce::Colours::black.withAlpha(0.42f);
        t.seam = c.accent.withAlpha(0.18f);
    } else {
        t.rimTop = juce::Colours::white;
        t.shade = c.textPrimary.withAlpha(0.16f);
        t.seam = juce::Colours::transparentBlack;
    }
    return t;
}

float cardShadowScale(DepthLevel level) noexcept {
    switch (level) {
    case DepthLevel::Flat:
        return 0.0f;
    case DepthLevel::Soft:
        return 1.0f;
    case DepthLevel::Rich:
        return 1.8f;
    }
    return 1.0f;
}

void paintCardDropShadow(juce::Graphics& g, juce::Rectangle<float> body, float radius, float shadowStrength,
                         DepthLevel level) {
    const float scale = cardShadowScale(level);
    if (shadowStrength <= 0.0f || scale <= 0.0f)
        return;
    // Plain translucent rounded rects, no blur: a blur re-rasterises on every zoom step of a buffered card.
    for (int i = 3; i >= 1; --i) {
        const float grow = (float)i * 1.5f;
        g.setColour(juce::Colours::black.withAlpha(juce::jmin(1.0f, shadowStrength * 0.13f * scale / (float)i)));
        g.fillRoundedRectangle(body.expanded(grow).translated(0.0f, 2.0f + (float)i), radius + grow);
    }
}

void paintCardInnerShade(juce::Graphics& g, juce::Rectangle<float> body, float radius, const Theme& theme) {
    if (theme.treatment.depth != DepthLevel::Rich)
        return;
    const auto tones = cardDepthTones(theme);
    juce::Path clip;
    clip.addRoundedRectangle(body, radius);
    g.saveState();
    g.reduceClipRegion(clip);
    const float top = body.getBottom() - body.getHeight() * 0.38f;
    juce::ColourGradient shade(tones.shade.withAlpha(0.0f), body.getX(), top, tones.shade.withMultipliedAlpha(0.55f),
                               body.getX(), body.getBottom(), false);
    g.setGradientFill(shade);
    g.fillRect(body.withTop(top));
    g.setColour(tones.shade);
    g.drawHorizontalLine((int)body.getBottom() - 1, body.getX() + radius, body.getRight() - radius);
    g.restoreState();
}

void paintCardRim(juce::Graphics& g, juce::Rectangle<float> body, float radius, const Theme& theme) {
    if (theme.treatment.depth != DepthLevel::Rich)
        return;
    const auto tones = cardDepthTones(theme);
    g.setColour(tones.rimTop);
    g.drawHorizontalLine((int)body.getY() + 1, body.getX() + radius, body.getRight() - radius);
}

void paintCardHeaderSeam(juce::Graphics& g, juce::Rectangle<float> header, const Theme& theme) {
    if (theme.treatment.depth != DepthLevel::Rich)
        return;
    const auto tones = cardDepthTones(theme);
    if (tones.seam.isTransparent())
        return;
    g.setColour(tones.seam);
    g.drawHorizontalLine((int)header.getBottom() + 1, header.getX(), header.getRight());
}

void paintCardRichLight(juce::Graphics& g, juce::Rectangle<float> body, float radius, const Theme& theme) {
    paintCardInnerShade(g, body, radius, theme);
    paintCardRim(g, body, radius, theme);
}

} // namespace synth::theme
