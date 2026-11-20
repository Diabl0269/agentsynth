// Concern: the alert box's severity icon at each Depth level (see AlertIconPainter.h).
#include "UI/Layout/AlertIconPainter.h"

#include <algorithm>

namespace synth::ui {

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

    // Soft's disc and glyph, untouched, with the shared Rich tile layers (shadow, rim, highlight) around them.
    paintSeverityDisc(g, area, ink, glyph, surface, &theme);
    if (glyph != SeverityGlyph::warning)
        return;
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
}

} // namespace synth::ui
