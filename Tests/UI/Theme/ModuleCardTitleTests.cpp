// ModuleCardTitleTests.cpp -- a module card's title is drawn in its own case, and the descenders of a
// mixed-case title (g, p, q, y) fit inside the 24 px header instead of being clipped by it.
//
// Images use SoftwareImageType(): the default native image type reads back zeros on a Windows runner.

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace {

constexpr int kHeaderHeight = 24; // ModuleComponent::kHeaderHeight
constexpr int kCardWidth = 260;
constexpr int kCardHeight = 80;

juce::Image paintCard(synth::theme::AppLookAndFeel& lf, const juce::String& title) {
    juce::Image image(juce::Image::ARGB, kCardWidth, kCardHeight, true, juce::SoftwareImageType());
    juce::Graphics g(image);
    lf.drawModulePanel(g, {0.0f, 0.0f, (float)kCardWidth, (float)kCardHeight}, kHeaderHeight, title, false, false);
    return image;
}

// The lowest row inside the header band (body is the bounds reduced by 2) whose pixels differ from the
// same card with no title, i.e. the bottom of the title's ink; -1 when the title drew nothing.
int titleInkBottom(synth::theme::AppLookAndFeel& lf, const juce::String& title) {
    const auto blank = paintCard(lf, {});
    const auto titled = paintCard(lf, title);
    for (int y = 2 + kHeaderHeight - 1; y >= 2; --y)
        for (int x = 0; x < kCardWidth; ++x)
            if (titled.getPixelAt(x, y) != blank.getPixelAt(x, y))
                return y;
    return -1;
}

} // namespace

TEST(ModuleCardTitleTest, DescendersAreDrawnAndStayInsideTheHeader) {
    synth::theme::AppLookAndFeel lf;
    const int headerBottom = 2 + kHeaderHeight; // the header's own bottom hairline row

    const int withDescenders = titleInkBottom(lf, "Gate Sequencer");
    const int withoutDescenders = titleInkBottom(lf, "Gate Seat");
    ASSERT_GE(withoutDescenders, 0);
    EXPECT_GE(withDescenders, withoutDescenders + 2) << "the g and q drop below the baseline, so they are not clipped";
    EXPECT_LT(withDescenders, headerBottom - 1) << "...and still clear the header's bottom hairline";

    EXPECT_LT(titleInkBottom(lf, "Analog Polysynth"), headerBottom - 1);
}

// Not all caps: "oo" is two x-height letters, so its ink is shorter than the capitals "OO".
TEST(ModuleCardTitleTest, TitleIsDrawnInItsOwnCase) {
    synth::theme::AppLookAndFeel lf;
    const auto topInk = [&](const juce::String& title) {
        const auto blank = paintCard(lf, {});
        const auto titled = paintCard(lf, title);
        for (int y = 2; y < 2 + kHeaderHeight; ++y)
            for (int x = 0; x < kCardWidth; ++x)
                if (titled.getPixelAt(x, y) != blank.getPixelAt(x, y))
                    return y;
        return -1;
    };
    EXPECT_GT(topInk("oooo"), topInk("OOOO")) << "lower-case letters start lower than capitals";
}
