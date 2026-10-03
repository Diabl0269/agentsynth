// DisclosureChevronTests.cpp -- the one shared fold arrow: closed points right, open points down, the
// states in between are real rotations, the colour follows the theme (muted at rest, primary when
// highlighted) and the look-and-feel member paints exactly what the free function paints. Images use
// SoftwareImageType(): the default native image type reads back zeros on a Windows runner.

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace {

using synth::theme::Theme;

constexpr int kSize = 16;
const juce::Rectangle<float> kArea(4.0f, 4.0f, 8.0f, 8.0f);

juce::Image blank() { return juce::Image(juce::Image::ARGB, kSize, kSize, true, juce::SoftwareImageType()); }

juce::Image renderFree(float openness, juce::Colour colour = juce::Colours::white) {
    auto image = blank();
    juce::Graphics g(image);
    synth::theme::paintDisclosureChevron(g, kArea, openness, colour);
    return image;
}

bool identical(const juce::Image& a, const juce::Image& b) {
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
            if (a.getPixelAt(x, y) != b.getPixelAt(x, y))
                return false;
    return true;
}

int coverage(const juce::Image& image) {
    int sum = 0;
    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = 0; x < image.getWidth(); ++x)
            sum += image.getPixelAt(x, y).getAlpha();
    return sum;
}

std::vector<Theme> darkAndLightThemes() { return {synth::theme::makeObsidian(), synth::theme::makeDaylight()}; }

} // namespace

TEST(DisclosureChevronTest, ClosedAndOpenDiffer) { EXPECT_FALSE(identical(renderFree(0.0f), renderFree(1.0f))); }

TEST(DisclosureChevronTest, ClosedIsTheOpenOneTurnedAQuarter) {
    const auto open = renderFree(1.0f);
    const auto closed = renderFree(0.0f);
    // Turning the picture a quarter turn anticlockwise about its centre takes pixel (x, y) to
    // (y, kSize - 1 - x); antialiased edge pixels may differ by a few levels, a wrong shape by far more.
    for (int y = 0; y < kSize; ++y)
        for (int x = 0; x < kSize; ++x)
            EXPECT_NEAR(open.getPixelAt(x, y).getAlpha(), closed.getPixelAt(y, kSize - 1 - x).getAlpha(), 12)
                << "pixel " << x << "," << y;
}

TEST(DisclosureChevronTest, ClosedPointsRightAndOpenPointsDown) {
    const auto open = renderFree(1.0f);
    EXPECT_GT(open.getPixelAt(8, 5).getAlpha(), 200); // the wide edge on top
    EXPECT_EQ(open.getPixelAt(5, 11).getAlpha(), 0);  // nothing left of the apex at the bottom
    const auto closed = renderFree(0.0f);
    EXPECT_GT(closed.getPixelAt(5, 8).getAlpha(), 200); // the wide edge on the left
    EXPECT_EQ(closed.getPixelAt(11, 5).getAlpha(), 0);  // nothing above the apex on the right
}

TEST(DisclosureChevronTest, HalfOpenSitsBetweenTheTwoStates) {
    const auto half = renderFree(0.5f);
    EXPECT_FALSE(identical(half, renderFree(0.0f)));
    EXPECT_FALSE(identical(half, renderFree(1.0f)));
    // A turned triangle keeps its area, so the painted coverage matches the end states'.
    EXPECT_NEAR(coverage(half), coverage(renderFree(1.0f)), coverage(renderFree(1.0f)) * 0.05);
}

TEST(DisclosureChevronTest, ColourFollowsTheTheme) {
    for (const auto& theme : darkAndLightThemes()) {
        auto rest = blank();
        auto hot = blank();
        {
            juce::Graphics g(rest);
            synth::theme::paintDisclosureChevron(g, kArea, 1.0f, theme, false);
        }
        {
            juce::Graphics g(hot);
            synth::theme::paintDisclosureChevron(g, kArea, 1.0f, theme, true);
        }
        EXPECT_EQ(rest.getPixelAt(8, 6), theme.colors.textMuted) << theme.name;
        EXPECT_EQ(hot.getPixelAt(8, 6), theme.colors.textPrimary) << theme.name;
        EXPECT_EQ(synth::theme::disclosureChevronColour(theme, false), theme.colors.textMuted);
        EXPECT_EQ(synth::theme::disclosureChevronColour(theme, true), theme.colors.textPrimary);
    }
}

TEST(DisclosureChevronTest, TheLookAndFeelMemberPaintsWhatTheFreeFunctionPaints) {
    for (const auto& theme : darkAndLightThemes()) {
        synth::theme::AppLookAndFeel lf;
        lf.applyTheme(theme);
        for (const bool highlighted : {false, true}) {
            auto viaMember = blank();
            {
                juce::Graphics g(viaMember);
                lf.drawDisclosureChevron(g, kArea, 0.5f, highlighted);
            }
            auto viaFree = blank();
            {
                juce::Graphics g(viaFree);
                synth::theme::paintDisclosureChevron(g, kArea, 0.5f, theme, highlighted);
            }
            EXPECT_TRUE(identical(viaMember, viaFree)) << theme.name << (highlighted ? " highlighted" : " rest");
        }
    }
}

TEST(DisclosureChevronTest, ThemeOfFallsBackToTheDefaultThemeWithoutAnAppLookAndFeel) {
    juce::Component bare;
    EXPECT_EQ(synth::theme::themeOf(bare).colors.accent, Theme{}.colors.accent);
    synth::theme::AppLookAndFeel lf;
    lf.applyTheme(synth::theme::makeDaylight());
    bare.setLookAndFeel(&lf);
    EXPECT_EQ(synth::theme::themeOf(bare).colors.accent, synth::theme::makeDaylight().colors.accent);
    bare.setLookAndFeel(nullptr);
}
