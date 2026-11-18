#include "IconTileDepth.h"
#include "UI/Theme/CardDepth.h"

namespace synth::theme {

namespace {
// The radial light at its centre. A dark theme's wash is the hue itself; on a light theme the hue glyphs have little
// contrast to spare against a tinted chip, so its light is white: the chip gets lighter toward the top left.
constexpr float kWashAlphaDark = 0.28f;
constexpr float kWashAlphaLight = 0.45f;
constexpr float kRingAlpha = 0.45f; // the 1px ring at rest
constexpr float kRingActiveAlpha = 0.65f;
constexpr float kGlowAlpha = 0.30f;  // an active tile's glow, as a share of the hue
constexpr float kShadeAlpha = 0.45f; // the inset shade of an active tile, as a share of the card shade tone

juce::Path tilePath(juce::Rectangle<float> tile, float radius) {
    juce::Path p;
    p.addRoundedRectangle(tile, radius);
    return p;
}
} // namespace

void paintIconTileUnder(juce::Graphics& g, juce::Rectangle<float> tile, float radius, juce::Colour hue, float active,
                        const Theme& theme) {
    if (theme.treatment.depth != DepthLevel::Rich)
        return;
    const auto tones = cardDepthTones(theme);
    // Outside the tile only: a surface's own fill may be translucent (a faint chip), and a shadow showing through it
    // would dim the glyph's ground.
    juce::Path outside;
    outside.addRectangle(g.getClipBounds().toFloat());
    outside.addRoundedRectangle(tile, radius);
    outside.setUsingNonZeroWinding(false);
    const juce::Graphics::ScopedSaveState state(g);
    g.reduceClipRegion(outside);
    // Stepped translucent rounded rects, no blur (a blur re-rasterises on every hover step of a button).
    for (int i = 2; i >= 1; --i) {
        const float grow = (float)i;
        g.setColour(tones.shade.withMultipliedAlpha(0.30f / (float)i));
        g.fillRoundedRectangle(tile.expanded(grow * 0.5f).translated(0.0f, 1.0f + grow), radius + grow * 0.5f);
    }
    if (active <= 0.0f)
        return;
    for (int i = 3; i >= 1; --i) {
        const float grow = (float)i * 1.5f;
        g.setColour(hue.withMultipliedAlpha(kGlowAlpha * active / (float)(i + 1)));
        g.fillRoundedRectangle(tile.expanded(grow), radius + grow);
    }
}

void paintIconTileOver(juce::Graphics& g, juce::Rectangle<float> tile, float radius, juce::Colour hue, float active,
                       float wash, const Theme& theme) {
    if (theme.treatment.depth != DepthLevel::Rich)
        return;
    const auto tones = cardDepthTones(theme);
    const auto path = tilePath(tile, radius);
    g.saveState();
    g.reduceClipRegion(path);
    if (wash > 0.0f) {
        const float reach = juce::jmax(tile.getWidth(), tile.getHeight()) * 0.9f;
        const auto washColour = theme.isDark ? hue : juce::Colours::white;
        const float alpha = (theme.isDark ? kWashAlphaDark : kWashAlphaLight) * wash;
        const auto centre = tile.getPosition() + juce::Point<float>(tile.getWidth() * 0.30f, tile.getHeight() * 0.20f);
        juce::ColourGradient light(washColour.withAlpha(alpha), centre, washColour.withAlpha(0.0f),
                                   centre.translated(reach, 0.0f), true);
        g.setGradientFill(light);
        g.fillRect(tile);
    }
    if (active > 0.0f) {
        const float top = tile.getBottom() - tile.getHeight() * 0.45f;
        juce::ColourGradient shade(tones.shade.withAlpha(0.0f), tile.getX(), top,
                                   tones.shade.withMultipliedAlpha(kShadeAlpha * active), tile.getX(), tile.getBottom(),
                                   false);
        g.setGradientFill(shade);
        g.fillRect(tile.withTop(top));
    }
    g.restoreState();
    g.setColour(hue.withMultipliedAlpha(kRingAlpha + (kRingActiveAlpha - kRingAlpha) * active));
    g.drawRoundedRectangle(tile.reduced(0.5f), juce::jmax(0.0f, radius - 0.5f), 1.0f);
    g.setColour(tones.rimTop);
    g.drawHorizontalLine((int)tile.getY() + 1, tile.getX() + radius, tile.getRight() - radius);
}

} // namespace synth::theme
