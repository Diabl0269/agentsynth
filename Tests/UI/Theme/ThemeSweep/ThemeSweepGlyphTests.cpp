// ThemeSweepGlyphTests.cpp -- controls that paint a glyph instead of text, which the text sweep cannot read: the
// timeline's clip sizing button in every built-in theme and every mode (each mode must draw a different icon, and the
// button keeps one size in all of them, and its menu-mark notch stays in the chip's corner), and the origin badge
// (docs/layout/theming.md, "Theme sweep guard").
#include "ThemeSweepSupport.h"
#include "ThemeSweepSurfaces.h"
#include "UI/Chrome/Badge/Badge.h"
#include "UI/Chrome/ToolbarButton/SideMenuMark.h"
#include "UI/Chrome/ToolbarButton/ToolbarButton.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include "UI/Timeline/AutomationLanes/LaneShapes/DrawPenButton.h"
#include "UI/Timeline/ClipSizingButton.h"
#include "UI/Timeline/TrackColour.h"
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

// The menu mark is a notch in the chip's lower-right corner. Against a plain toolbar button of the same icon, layout
// and size, every pixel that differs lies inside the chip's lower-right notch-leg square (so it never reaches the
// glyph, which stays full size and centred), and the notch shows in every theme, with labels shown and with them off.
TEST(ThemeSweepGlyphs, TheClipSizingNotchSitsInTheChipCornerAndNeverReachesTheGlyph) {
    juce::MessageManager::getInstance();
    for (const auto& theme : synth::theme::builtInThemes()) {
        SweepEnvironment env(theme);
        for (const bool captionShown : {true, false}) {
            for (const auto mode : kModes) {
                synth::ui::ClipSizingButton field;
                field.setLookAndFeel(&env.lookAndFeel());
                field.setCaptionShown(captionShown);
                field.choose(mode);
                field.setSize(field.getFixedWidth(), 28);

                synth::ui::ToolbarButton plain("plain");
                plain.setLookAndFeel(&env.lookAndFeel());
                plain.setLayout(synth::ui::ToolbarButtonLayout::Inline);
                plain.setIcon(field.getIcon(), synth::ui::ToolbarGroup::Edit);
                plain.setButtonText(field.getButtonText());
                plain.setSize(field.getWidth(), field.getHeight());

                const auto chip = synth::theme::toolbarChipBounds(field).toNearestInt();
                const int leg = (int)synth::ui::kSideMenuNotchLeg;
                const auto corner = chip.withTrimmedLeft(chip.getWidth() - leg).withTrimmedTop(chip.getHeight() - leg);
                const auto withNotch = renderToImage(field);
                const auto without = renderToImage(plain);
                const juce::String label = juce::String(theme.id) + " / " + synth::clipSizingModeKey(mode) +
                                           (captionShown ? " / labels" : " / no labels");
                EXPECT_GT(differingPixels(withNotch, without, corner), 3) << label << ": no notch in the corner";
                EXPECT_EQ(differingPixels(withNotch, without, field.getLocalBounds()),
                          differingPixels(withNotch, without, corner))
                    << label << ": the notch reaches outside the chip's corner";
                if (mode == ClipSizingMode::Stretch)
                    writeSweepPng(juce::String("sizing-field-notch-") + (captionShown ? "labels" : "icon"), theme.id,
                                  withNotch);
                field.setLookAndFeel(nullptr);
                plain.setLookAndFeel(nullptr);
            }
        }
    }
}

namespace {
// The pixel of `image` at `p` as the window shows it: over the theme's background, since a rendered component leaves
// the area behind its translucent chip transparent.
juce::Colour pixelAt(const juce::Image& image, juce::Point<int> p, juce::Colour backdrop) {
    return backdrop.overlaidWith(image.getPixelAt(p.x, p.y));
}

// The notch is visible and reads on the chip: samples in the middle of its two legs and near its corner against the
// fill beside the glyph. `right` / `bottom` are the chip's lower-right corner in the image.
void expectNotchReads(const juce::Image& image, juce::Colour backdrop, juce::Point<int> right_bottom,
                      juce::Point<int> fillAt, const juce::String& label) {
    const auto fill = pixelAt(image, fillAt, backdrop);
    const int r = right_bottom.x, b = right_bottom.y;
    const juce::Point<int> samples[] = {{r - 4, b - 3}, {r - 3, b - 4}};
    for (const auto sample : samples)
        EXPECT_GE(synth::ui::colourContrast(pixelAt(image, sample, backdrop), fill), 3.0f)
            << label << ": the notch at " << sample.x << "," << sample.y << " against the chip fill";
}

// The glyph's box never meets the notch triangle: the box's lower-right corner lies on or beyond its hypotenuse.
void expectGlyphClearOfNotch(juce::Rectangle<float> chip, juce::Rectangle<float> glyph, const juce::String& label) {
    EXPECT_GE((chip.getRight() - glyph.getRight()) + (chip.getBottom() - glyph.getBottom()),
              synth::ui::kSideMenuNotchLeg - 0.001f)
        << label << ": the glyph box meets the notch";
}
} // namespace

// The menu notch on the Sizing chip: at least 8 px, 3:1 against the chip fill in every theme with labels on and off,
// and the glyph's box, centred in the chip, stays clear of it.
TEST(ThemeSweepGlyphs, TheClipSizingNotchIsEightPixelsAndReadsInEveryThemeAndNeverMeetsTheGlyph) {
    juce::MessageManager::getInstance();
    EXPECT_GE(synth::ui::kSideMenuNotchLeg, 8.0f);
    for (const auto& theme : synth::theme::builtInThemes()) {
        SweepEnvironment env(theme);
        for (const bool captionShown : {true, false}) {
            synth::ui::ClipSizingButton field;
            field.setLookAndFeel(&env.lookAndFeel());
            field.setCaptionShown(captionShown);
            field.setSize(field.getFixedWidth(), 28);
            const auto chip = synth::theme::toolbarChipBounds(field);
            const auto glyphSize = synth::ui::toolbarChipSpec(field.getLayout()).glyphSize;
            const auto glyph = juce::Rectangle<float>(glyphSize, glyphSize).withCentre(chip.getCentre());
            const juce::String label = juce::String(theme.id) + (captionShown ? " / labels" : " / no labels");
            expectGlyphClearOfNotch(chip, glyph, label);
            EXPECT_FLOAT_EQ(glyph.getCentreX(), chip.getCentreX()) << label << ": glyph centred";

            const auto image = renderToImage(field);
            expectNotchReads(image, theme.colors.bg0, {(int)chip.getRight(), (int)chip.getBottom()},
                             {(int)chip.getX() + 3, (int)chip.getCentreY()}, label);
            field.setLookAndFeel(nullptr);
        }
    }
}

// The pen's notch is the same shared one: 3:1 against the button fill in every theme, lit and unlit, and the icon
// (centred in the whole button) stays clear of it.
TEST(ThemeSweepGlyphs, ThePenNotchReadsInEveryThemeLitAndUnlitAndNeverMeetsTheIcon) {
    juce::MessageManager::getInstance();
    for (const auto& theme : synth::theme::builtInThemes()) {
        SweepEnvironment env(theme);
        for (const bool lit : {false, true}) {
            synth::ui::DrawPenButton pen("pen");
            pen.setLookAndFeel(&env.lookAndFeel());
            pen.setClickingTogglesState(false);
            pen.setToggleState(lit, juce::dontSendNotification);
            pen.setSize(24, 24);
            const juce::String label = juce::String(theme.id) + (lit ? " / lit" : " / unlit");
            const auto box = pen.getLocalBounds().toFloat();
            expectGlyphClearOfNotch(box, box.reduced(6.0f), label);

            const auto image = renderToImage(pen);
            expectNotchReads(image, theme.colors.bg0, {pen.getWidth(), pen.getHeight()}, {3, pen.getHeight() / 2},
                             label);
            pen.setLookAndFeel(nullptr);
        }
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
