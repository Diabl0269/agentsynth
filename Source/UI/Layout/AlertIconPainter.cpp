// Concern: the alert box's severity icon at each Depth level (see AlertIconPainter.h).
#include "UI/Layout/AlertIconPainter.h"

#include <algorithm>

namespace synth::ui {

namespace {
// Rich's shading of the bare warning triangle, all inside its outline: a top light that fades out by the middle and a
// darker foot (kRichTop*), plus a fine light line just inside the top edges (kRichEdge*). One light, from above.
constexpr float kRichTopLight = 0.30f;    // white at the top of the triangle
constexpr float kRichLightEnd = 0.55f;    // where the top light has faded out, as a share of the height
constexpr float kRichFootShade = 0.16f;   // black at the foot
constexpr float kRichEdgeLight = 0.70f;   // white of the inner top-edge line at its brightest
constexpr float kRichEdgeEnd = 0.45f;     // where that line has faded out, as a share of the height
constexpr float kRichEdgeWidth = 2.2f;    // the line's stroke on the 24-unit grid (half of it falls inside)
constexpr float kRichShadowAlpha = 0.42f; // Rich's deeper drop shadow (Soft's is 0.28)

// The bare triangle at Rich: Soft's body under a deeper shadow, shaded inside its own outline, then the mark cut. The
// outline (path plus its rounding stroke) is exactly Soft's, so Rich never adds a shape.
void paintRichWarning(juce::Graphics& g, juce::Point<float> centre, float side, juce::Colour ink, juce::Colour cut) {
    const float unit = side / 24.0f;
    const juce::DropShadow deeper(juce::Colours::black.withAlpha(kRichShadowAlpha * ink.getFloatAlpha()), 4, {0, 2});
    paintSeverityBody(g, centre, side, ink, SeverityGlyph::warning, &deeper);

    const auto body = severityBodyPath(SeverityGlyph::warning, centre, side);
    juce::Path outline;
    juce::PathStrokeType(1.6f * unit, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)
        .createStrokedPath(outline, body);
    outline.addPath(body);
    outline.setUsingNonZeroWinding(true);
    const auto box = outline.getBounds();
    {
        const juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(outline);
        juce::ColourGradient shade(juce::Colours::white.withAlpha(kRichTopLight * ink.getFloatAlpha()), box.getX(),
                                   box.getY(), juce::Colours::black.withAlpha(kRichFootShade * ink.getFloatAlpha()),
                                   box.getX(), box.getBottom(), false);
        shade.addColour(kRichLightEnd, juce::Colours::white.withAlpha(0.0f));
        g.setGradientFill(shade);
        g.fillRect(box);
    }
    {
        const juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(body);
        juce::ColourGradient edge(juce::Colours::white.withAlpha(kRichEdgeLight * ink.getFloatAlpha()), box.getX(),
                                  box.getY(), juce::Colours::white.withAlpha(0.0f), box.getX(),
                                  box.getY() + box.getHeight() * kRichEdgeEnd, false);
        g.setGradientFill(edge);
        g.strokePath(body, juce::PathStrokeType(kRichEdgeWidth * unit, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
    }
    paintSeverityMark(g, centre, side, SeverityGlyph::warning, cut);
}
} // namespace

void paintAlertIcon(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour ink, SeverityGlyph glyph,
                    juce::Colour surface, const synth::theme::Theme& theme) {
    using synth::theme::DepthLevel;
    const float side = std::min(area.getWidth(), area.getHeight());
    const auto disc = juce::Rectangle<float>(side, side).withCentre(area.getCentre());
    const auto centre = disc.getCentre();
    const float bareGlyph = side * kAlertGlyphShare * kFlatGlyphScale;
    const auto depth = theme.treatment.depth;

    if (depth == DepthLevel::Flat) {
        paintSeverityShape(g, centre, bareGlyph, ink, glyph, surface, false, true);
        return;
    }
    // A warning is the bare triangle at every level, never inside a round: Soft adds its small shadow, Rich a deeper
    // shadow and shading inside the same outline.
    if (glyph == SeverityGlyph::warning) {
        if (depth == DepthLevel::Rich)
            paintRichWarning(g, centre, bareGlyph, ink, surface);
        else
            paintSeverityShape(g, centre, bareGlyph, ink, glyph, surface, true, true);
        return;
    }
    // Info and question keep the one disc painter at Soft and Rich: Rich passes the theme and so adds the shared tile
    // layers (shadow, rim, highlight) around Soft's disc and glyph; Soft passes none and is the toast's disc.
    paintSeverityDisc(g, area, ink, glyph, surface, depth == DepthLevel::Rich ? &theme : nullptr);
}

} // namespace synth::ui
