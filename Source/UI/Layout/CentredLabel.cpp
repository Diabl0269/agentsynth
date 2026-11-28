#include "CentredLabel.h"

#include <cmath>
#include <map>
#include <tuple>

namespace synth::ui {

namespace {

// Where the glyphs' ink balances, relative to the text's origin: the centroid of the painted area, found by painting
// the path at 8x into a scratch image. The bounds' centre is not it for a figure with a stem to one side ("1", "4",
// "7"): the box centre sits left of the mass and the number then looks pushed to the right of its ring. Cached per
// string and font (typeface and size, so a different default font never reuses another's answer), since a dot is
// painted on every frame.
juce::Point<float> inkCentroid(const juce::Path& path, const juce::String& text, const juce::Font& font) {
    static std::map<std::tuple<juce::String, juce::String, juce::String, float, bool, bool>, juce::Point<float>> cache;
    // The resolved face, not the requested name: a default font asked for as "<Sans-Serif>" is a different face once
    // the theme or platform changes what that alias means.
    const auto face = font.getTypefacePtr();
    const auto key =
        std::make_tuple(text, face->getName(), face->getStyle(), font.getHeight(), font.isBold(), font.isItalic());
    if (const auto found = cache.find(key); found != cache.end())
        return found->second;

    constexpr float kScale = 8.0f;
    const auto bounds = path.getBounds();
    const int w = (int)std::ceil(bounds.getWidth() * kScale) + 4;
    const int h = (int)std::ceil(bounds.getHeight() * kScale) + 4;
    juce::Image image(juce::Image::SingleChannel, w, h, true, juce::SoftwareImageType());
    {
        juce::Graphics g(image);
        g.setColour(juce::Colours::white);
        g.fillPath(
            path,
            juce::AffineTransform::translation(-bounds.getX(), -bounds.getY()).scaled(kScale).translated(2.0f, 2.0f));
    }
    double mass = 0.0, mx = 0.0, my = 0.0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const double a = image.getPixelAt(x, y).getAlpha();
            mass += a;
            mx += a * ((double)x + 0.5);
            my += a * ((double)y + 0.5);
        }
    auto centroid = bounds.getCentre();
    if (mass > 0.0)
        centroid = {bounds.getX() + (float)((mx / mass - 2.0) / kScale),
                    bounds.getY() + (float)((my / mass - 2.0) / kScale)};
    cache[key] = centroid;
    return centroid;
}

} // namespace

// A label whose INK is centred on a float point (numbers in dots). drawText with an int box truncates the centre by up
// to a pixel, and Justification::centred centres the line box including the descender space, so digits ride high; the
// glyph path has neither error, and its ink centroid (not its bounding box) is what the eye reads as the middle.
void drawLabelCentredOn(juce::Graphics& g, const juce::String& text, const juce::Font& font,
                        juce::Point<float> centre) {
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(font, text, 0.0f, 0.0f);
    juce::Path path;
    glyphs.createPath(path);
    if (path.isEmpty())
        return;
    const auto ink = inkCentroid(path, text, font);
    path.applyTransform(juce::AffineTransform::translation(centre.x - ink.x, centre.y - ink.y));
    g.fillPath(path);
}

} // namespace synth::ui
