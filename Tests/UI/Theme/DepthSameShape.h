#pragma once

// DepthSameShape.h -- the guard of the bug kind look-level-reshapes: Rich adds light and shade to a surface,
// it never reshapes it. For one surface painted twice, at Soft and at Rich, in one theme:
//
//   1. Silhouette: Soft's footprint (every pixel it paints) is still painted at Rich, and Rich paints beyond it by at
//      most kShadowBand pixels (the drop shadow and an active glow).
//   2. Glyph: the bounding box of the glyph (the pixels that differ strongly from the tile's own fill) is the same
//   (edges within kGlyphTolerance).
//   3. Colour: the mean pixel delta over the body, outside the rim band and outside any extra layer the surface
//      declares (the alert's border), stays under kMeanDeltaBudget per channel, so the hue stays Soft's.
//
// A Depth-aware painter registers by adding its surface to a TEST that calls expectSameShape for every built-in theme
// (IconTileDepthTests.cpp, AlertIconDepthTests.cpp); the registry test in IconTileDepthTests.cpp fails for a painter
// that is not listed. Images use SoftwareImageType(): the default native image type reads back zeros on Windows.

#include <algorithm>
#include <cmath>
#include <functional>
#include <gtest/gtest.h>
#include <juce_graphics/juce_graphics.h>
#include <string>
#include <vector>

namespace depthshape {

constexpr int kShadowBand = 6;     // px Rich may paint beyond Soft's footprint (shadow offset + blur, glow)
constexpr int kRimBand = 3;        // px inside the tile's edge left out of the colour comparison (ring, highlight)
constexpr int kGlyphTolerance = 1; // px an edge of the glyph box may move: anti-aliasing against a slightly lit ground
constexpr int kGlyphContrast = 60; // channel delta (0..255) from the tile fill that makes a pixel part of the glyph
constexpr double kMeanDeltaBudget =
    8.0; // mean abs channel delta (0..255) over the body: a faint wash, not a new colour

inline int channelDelta(juce::Colour a, juce::Colour b) {
    return juce::jmax(std::abs((int)a.getAlpha() - (int)b.getAlpha()), std::abs((int)a.getRed() - (int)b.getRed()),
                      std::abs((int)a.getGreen() - (int)b.getGreen()), std::abs((int)a.getBlue() - (int)b.getBlue()));
}

inline double meanChannelDelta(juce::Colour a, juce::Colour b) {
    return (std::abs((int)a.getAlpha() - (int)b.getAlpha()) + std::abs((int)a.getRed() - (int)b.getRed()) +
            std::abs((int)a.getGreen() - (int)b.getGreen()) + std::abs((int)a.getBlue() - (int)b.getBlue())) /
           4.0;
}

inline juce::Rectangle<int> footprint(const juce::Image& image) {
    const auto background = image.getPixelAt(0, 0);
    juce::Rectangle<int> box;
    bool any = false;
    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = 0; x < image.getWidth(); ++x)
            if (image.getPixelAt(x, y) != background) {
                const juce::Rectangle<int> px(x, y, 1, 1);
                box = any ? box.getUnion(px) : px;
                any = true;
            }
    return box;
}

// The tile's own fill is read at the four corners of the region (a glyph's corners are empty ground), as the
// per-channel median, so a faint wash that tilts the fill across the region cannot move the glyph's edge. Pixels
// outside `body` (rim, border) are skipped.
inline juce::Rectangle<int> glyphBox(const juce::Image& image, juce::Rectangle<int> region,
                                     const std::function<bool(int, int)>& body) {
    std::vector<int> a, r, g, b;
    for (const auto p : {juce::Point<int>(region.getX() + 1, region.getY() + 1),
                         juce::Point<int>(region.getRight() - 2, region.getY() + 1),
                         juce::Point<int>(region.getX() + 1, region.getBottom() - 2),
                         juce::Point<int>(region.getRight() - 2, region.getBottom() - 2)})
        if (body(p.x, p.y)) {
            const auto c = image.getPixelAt(p.x, p.y);
            a.push_back(c.getAlpha());
            r.push_back(c.getRed());
            g.push_back(c.getGreen());
            b.push_back(c.getBlue());
        }
    if (a.empty())
        return {};
    const auto median = [](std::vector<int>& v) {
        std::nth_element(v.begin(), v.begin() + (long)v.size() / 2, v.end());
        return v[v.size() / 2];
    };
    const auto fill =
        juce::Colour((juce::uint8)median(r), (juce::uint8)median(g), (juce::uint8)median(b), (juce::uint8)median(a));
    juce::Rectangle<int> box;
    bool any = false;
    for (int y = region.getY(); y < region.getBottom(); ++y)
        for (int x = region.getX(); x < region.getRight(); ++x)
            if (body(x, y) && channelDelta(image.getPixelAt(x, y), fill) >= kGlyphContrast) {
                const juce::Rectangle<int> px(x, y, 1, 1);
                box = any ? box.getUnion(px) : px;
                any = true;
            }
    return box;
}

/** What a surface tells the guard about itself. `body(x, y)` is true for the pixels whose colour must stay Soft's (the
 *  tile inside its rim band, minus any extra layer the surface adds on purpose). */
struct Surface {
    std::string name;
    juce::Rectangle<int> glyphRegion; // where the glyph lives (clear of any border or ring the surface adds)
    std::function<bool(int, int)> body;
};

inline void expectSameShape(const juce::Image& soft, const juce::Image& rich, const Surface& s,
                            const juce::String& id) {
    const std::string what = s.name + " in " + id.toStdString();
    ASSERT_EQ(soft.getBounds(), rich.getBounds()) << what;
    EXPECT_EQ(soft.getPixelAt(0, 0), rich.getPixelAt(0, 0)) << what << ": the image margin must stay clear";
    const auto softPrint = footprint(soft);
    const auto richPrint = footprint(rich);
    ASSERT_FALSE(softPrint.isEmpty()) << what;
    EXPECT_TRUE(richPrint.contains(softPrint)) << what << ": Rich erased part of Soft's silhouette";
    EXPECT_TRUE(softPrint.expanded(kShadowBand).contains(richPrint))
        << what << ": Rich paints more than the shadow band beyond Soft's silhouette";
    const auto softGlyph = glyphBox(soft, s.glyphRegion, s.body);
    const auto richGlyph = glyphBox(rich, s.glyphRegion, s.body);
    ASSERT_FALSE(softGlyph.isEmpty()) << what << ": no glyph found";
    EXPECT_TRUE(std::abs(softGlyph.getX() - richGlyph.getX()) <= kGlyphTolerance &&
                std::abs(softGlyph.getY() - richGlyph.getY()) <= kGlyphTolerance &&
                std::abs(softGlyph.getRight() - richGlyph.getRight()) <= kGlyphTolerance &&
                std::abs(softGlyph.getBottom() - richGlyph.getBottom()) <= kGlyphTolerance)
        << what << ": the glyph box changed from " << softGlyph.toString() << " to " << richGlyph.toString();
    double sum = 0.0;
    int n = 0;
    for (int y = 0; y < soft.getHeight(); ++y)
        for (int x = 0; x < soft.getWidth(); ++x)
            if (s.body(x, y)) {
                sum += meanChannelDelta(soft.getPixelAt(x, y), rich.getPixelAt(x, y));
                ++n;
            }
    ASSERT_GT(n, 20) << what << ": the body mask is empty";
    EXPECT_LT(sum / n, kMeanDeltaBudget) << what << ": Rich changed the body colour (mean delta " << sum / n << ")";
}

} // namespace depthshape
