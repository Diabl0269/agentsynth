// ThemeSweepGlyphTests.cpp -- controls that paint a glyph instead of text, which the text sweep cannot read: the
// timeline's Sizing field in every built-in theme and every mode. Its glyph must reach 3:1 against the field's own
// rendered background (the WCAG bar for a non-text graphic), each mode must draw a different glyph, and the field
// keeps one size in all of them (docs/layout/theming.md, "Theme sweep guard").
#include "ThemeSweepSupport.h"
#include "ThemeSweepSurfaces.h"
#include "UI/Theme/BuiltInThemes.h"
#include "UI/Timeline/ClipSizingButton.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <vector>

using namespace synth::test::theme_sweep;
using synth::ClipSizingMode;

namespace {

constexpr double kMinGlyphContrast = 3.0;
constexpr ClipSizingMode kModes[] = {ClipSizingMode::Trim, ClipSizingMode::CarryContents, ClipSizingMode::Stretch};

// The strongest contrast any pixel of `area` reaches against `background`, each pixel composited over it first.
double strongestContrast(const juce::Image& image, juce::Rectangle<int> area, juce::Colour background) {
    double best = 1.0;
    for (int y = area.getY(); y < area.getBottom(); ++y)
        for (int x = area.getX(); x < area.getRight(); ++x)
            best = std::max(best, contrastRatio(background.overlaidWith(image.getPixelAt(x, y)), background));
    return best;
}

int differingPixels(const juce::Image& a, const juce::Image& b, juce::Rectangle<int> area) {
    int count = 0;
    for (int y = area.getY(); y < area.getBottom(); ++y)
        for (int x = area.getX(); x < area.getRight(); ++x)
            if (a.getPixelAt(x, y) != b.getPixelAt(x, y))
                ++count;
    return count;
}

} // namespace

TEST(ThemeSweepGlyphs, TheClipSizingFieldGlyphReadsInEveryThemeAndEveryModeDrawsItsOwn) {
    juce::MessageManager::getInstance();
    for (const auto& theme : synth::theme::builtInThemes()) {
        SweepEnvironment env(theme);
        synth::ui::ClipSizingButton field;
        field.setLookAndFeel(&env.lookAndFeel());
        field.setSize(synth::ui::ClipSizingButton::kWidth, 24);
        const auto glyphArea = field.glyphBounds().toNearestInt();
        std::vector<juce::Image> images;
        for (const auto mode : kModes) {
            field.choose(mode);
            EXPECT_EQ(field.getWidth(), synth::ui::ClipSizingButton::kWidth) << "the field keeps one size";
            auto image = renderToImage(field);
            writeSweepPng("sizing-field-" + juce::String(synth::clipSizingModeKey(mode)), theme.id, image);
            // The fill between the glyph and the chevron, over the panel's own background.
            const auto fill = theme.colors.bg0.overlaidWith(image.getPixelAt(glyphArea.getCentreX(), 3));
            const double ratio = strongestContrast(image, glyphArea, fill);
            EXPECT_GE(ratio, kMinGlyphContrast) << theme.id << " / " << synth::clipSizingModeKey(mode);
            images.push_back(std::move(image));
        }
        for (size_t a = 0; a < images.size(); ++a)
            for (size_t b = a + 1; b < images.size(); ++b)
                EXPECT_GT(differingPixels(images[a], images[b], glyphArea), 20)
                    << theme.id << ": " << synth::clipSizingModeKey(kModes[a]) << " and "
                    << synth::clipSizingModeKey(kModes[b]) << " look the same";
        field.setLookAndFeel(nullptr);
    }
}
