// Concern: the alert box's severity icon at each Depth level (see AlertIconPainter.h).
#include "UI/Layout/AlertIconPainter.h"
#include "UI/Theme/CardDepth.h"
#include "UI/Theme/IconTileDepth.h"

#include <algorithm>

namespace synth::ui {

namespace {

// Rich: the lit disc body (its shadow, wash and rim), without the glyph.
void paintRichDisc(juce::Graphics& g, juce::Rectangle<float> disc, juce::Colour ink, const synth::theme::Theme& theme) {
    const float side = disc.getWidth();
    const auto body = disc.reduced(0.5f);
    synth::theme::paintIconTileUnder(g, body, side * 0.5f, ink, 0.0f, theme);
    if (theme.isDark) {
        g.setGradientFill(juce::ColourGradient(ink.withMultipliedAlpha(kRichWashStart), disc.getX() + side * 0.35f,
                                               disc.getY() + side * 0.28f, ink.withMultipliedAlpha(kRichWashEnd),
                                               disc.getX() + side * 1.15f, disc.getY() + side * 0.28f, true));
        g.fillEllipse(body);
        const auto tones = synth::theme::cardDepthTones(theme);
        g.setGradientFill(juce::ColourGradient(tones.rimTop, body.getX(), body.getY(), tones.shade, body.getRight(),
                                               body.getBottom(), false));
        g.drawEllipse(body, 1.0f);
    } else {
        // Neutral on a light theme: no hue wash on the white surface, just a faint tint and a ring.
        g.setColour(juce::Colours::white.interpolatedWith(ink.withAlpha(1.0f), kRichNeutralFill));
        g.fillEllipse(body);
        g.setColour(juce::Colours::white.interpolatedWith(ink.withAlpha(1.0f), kRichNeutralRing));
        g.drawEllipse(body, 1.0f);
    }
}

} // namespace

void paintAlertIcon(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour ink, SeverityGlyph glyph,
                    juce::Colour surface, const synth::theme::Theme& theme) {
    using synth::theme::DepthLevel;
    const float side = std::min(area.getWidth(), area.getHeight());
    const auto disc = juce::Rectangle<float>(side, side).withCentre(area.getCentre());
    const auto centre = disc.getCentre();
    const float softGlyph = side * kAlertGlyphShare;

    switch (theme.treatment.depth) {
    case DepthLevel::Soft:
        paintSeverityDisc(g, area, ink, glyph, surface);
        return;
    case DepthLevel::Flat:
        paintSeverityShape(g, centre, softGlyph * kFlatGlyphScale, ink, glyph, surface, false, true);
        return;
    case DepthLevel::Rich:
        break;
    }

    paintRichDisc(g, disc, ink, theme);
    if (glyph != SeverityGlyph::warning) {
        paintSeverityShape(g, centre, softGlyph, ink, glyph, surface, true, true);
        return;
    }
    const float borderSide = softGlyph * kRichBorderScale;
    const float stroke = kRichBorderStroke * borderSide / 24.0f;
    juce::Path border = severityBodyPath(glyph, centre, borderSide);
    juce::Path stroked;
    juce::PathStrokeType(stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)
        .createStrokedPath(stroked, border);
    if (theme.isDark)
        juce::DropShadow(ink.withMultipliedAlpha(kRichGlowAlpha), 5, {0, 0}).drawForPath(g, stroked);
    g.setColour(ink);
    g.fillPath(stroked);
    paintSeverityShape(g, centre, borderSide * kRichInnerScale, ink, glyph, surface, true, true);
}

} // namespace synth::ui
