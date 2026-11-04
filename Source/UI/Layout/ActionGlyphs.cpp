// Concern: the shared painters for actions drawn on several surfaces (see ActionGlyphs.h).
#include "UI/Layout/ActionGlyphs.h"

#include <algorithm>

namespace synth::ui {

namespace {
// The drawings below are laid out on a 12 px square; every length scales with the square actually used.
constexpr float kGridSize = 12.0f;
constexpr float kRingThickness = 1.4f;

juce::Rectangle<float> squareIn(juce::Rectangle<float> area) {
    const float side = std::min(area.getWidth(), area.getHeight());
    return juce::Rectangle<float>(side, side).withCentre(area.getCentre());
}
} // namespace

// A target: a ring with four ticks reaching out of it.
void paintShowOnCanvasGlyph(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour ink, float strokeScale) {
    const auto glyph = squareIn(area);
    const float unit = glyph.getWidth() / kGridSize;
    const float thickness = kRingThickness * unit * strokeScale;
    const auto centre = glyph.getCentre();
    const float radius = kGridSize * 0.28f * unit;
    const float tick = kGridSize * 0.5f * unit;
    g.setColour(ink);
    g.drawEllipse(glyph.withSizeKeepingCentre(radius * 2.0f, radius * 2.0f), thickness);
    for (const auto d :
         {juce::Point<float>(1, 0), juce::Point<float>(-1, 0), juce::Point<float>(0, 1), juce::Point<float>(0, -1)})
        g.drawLine({centre + d * (radius + unit), centre + d * tick}, thickness);
}

} // namespace synth::ui
