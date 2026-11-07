// ThemeSweepGlyphTests.cpp -- controls that paint a glyph instead of text, which the text sweep cannot read: the
// timeline's clip sizing button in every built-in theme and every mode (each mode must draw a different icon, and the
// button keeps one size in all of them), and the origin badge (docs/layout/theming.md, "Theme sweep guard").
#include "ThemeSweepSupport.h"
#include "ThemeSweepSurfaces.h"
#include "UI/Chrome/Badge/Badge.h"
#include "UI/Theme/BuiltInThemes.h"
#include "UI/Timeline/ClipSizingButton.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <vector>

using namespace synth::test::theme_sweep;
using synth::ClipSizingMode;

namespace {

constexpr ClipSizingMode kModes[] = {ClipSizingMode::Trim, ClipSizingMode::CarryContents, ClipSizingMode::Stretch};

int differingPixels(const juce::Image& a, const juce::Image& b, juce::Rectangle<int> area) {
    int count = 0;
    for (int y = area.getY(); y < area.getBottom(); ++y)
        for (int x = area.getX(); x < area.getRight(); ++x)
            if (a.getPixelAt(x, y) != b.getPixelAt(x, y))
                ++count;
    return count;
}

} // namespace

TEST(ThemeSweepGlyphs, TheClipSizingButtonDrawsItsOwnIconPerModeInEveryThemeAtOneSize) {
    juce::MessageManager::getInstance();
    for (const auto& theme : synth::theme::builtInThemes()) {
        SweepEnvironment env(theme);
        synth::ui::ClipSizingButton field;
        field.setLookAndFeel(&env.lookAndFeel());
        field.setSize(field.getFixedWidth(), 28);
        std::vector<juce::Image> images;
        for (const auto mode : kModes) {
            field.choose(mode);
            EXPECT_EQ(field.getWidth(), field.getFixedWidth()) << "the button keeps one size";
            auto image = renderToImage(field);
            writeSweepPng("sizing-field-" + juce::String(synth::clipSizingModeKey(mode)), theme.id, image);
            images.push_back(std::move(image));
        }
        const auto all = field.getLocalBounds();
        for (size_t a = 0; a < images.size(); ++a)
            for (size_t b = a + 1; b < images.size(); ++b)
                EXPECT_GT(differingPixels(images[a], images[b], all), 20)
                    << theme.id << ": " << synth::clipSizingModeKey(kModes[a]) << " and "
                    << synth::clipSizingModeKey(kModes[b]) << " look the same";
        field.setLookAndFeel(nullptr);
    }
}

// The origin badge (Yours, Factory, Pack, Marketplace) in every built-in theme: its text reaches 4.5:1 against its own
// fill composited over each surface it can sit on, and the strip of all four is written as a PNG to look at.
TEST(ThemeSweepGlyphs, TheOriginBadgeTextReadsInEveryThemeOnEverySurface) {
    juce::MessageManager::getInstance();
    constexpr synth::ui::BadgeKind kinds[] = {synth::ui::BadgeKind::Yours, synth::ui::BadgeKind::Factory,
                                              synth::ui::BadgeKind::Pack, synth::ui::BadgeKind::Marketplace};
    for (const auto& theme : synth::theme::builtInThemes()) {
        SweepEnvironment env(theme);
        for (const auto kind : kinds) {
            const auto colours = synth::ui::badgeColours(theme, kind);
            for (const auto surface : {theme.colors.bg0, theme.colors.bg1, theme.colors.surface}) {
                const auto fill = surface.overlaidWith(colours.fill);
                EXPECT_GE(contrastRatio(colours.text, fill), kMinTextContrast)
                    << theme.id << " / " << synth::ui::badgeText(kind);
            }
        }
        juce::Component strip;
        strip.setLookAndFeel(&env.lookAndFeel());
        strip.setOpaque(true);
        int x = 8;
        std::vector<std::unique_ptr<synth::ui::Badge>> badges;
        for (const auto kind : kinds) {
            auto badge = std::make_unique<synth::ui::Badge>(kind);
            const auto ideal = badge->getIdealBounds();
            badge->setBounds(x, 8, ideal.getWidth(), ideal.getHeight());
            strip.addAndMakeVisible(*badge);
            x += ideal.getWidth() + 8;
            badges.push_back(std::move(badge));
        }
        strip.setSize(x, 32);
        writeSweepPng("origin-badges", theme.id, renderToImage(strip));
        strip.setLookAndFeel(nullptr);
    }
}
