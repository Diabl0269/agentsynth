// Menu corner tests: the popup menu window must be non-opaque and the background drawn by
// AppLookAndFeel must leave its rounded corners fully transparent in every built-in theme.
// Images use SoftwareImageType because the platform default reads back zeros on the Windows CI.

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace {

using synth::theme::AppLookAndFeel;

constexpr int kWidth = 120;
constexpr int kHeight = 80;

TEST(MenuCornerTests, MenuBackgroundColourLeavesTheWindowNonOpaque) {
    // JUCE's menu window is opaque and white-filled exactly when this colour is opaque.
    for (const auto& theme : synth::theme::builtInThemes()) {
        AppLookAndFeel laf;
        laf.applyTheme(theme);
        EXPECT_FALSE(laf.findColour(juce::PopupMenu::backgroundColourId).isOpaque()) << theme.name;
    }
}

TEST(MenuCornerTests, BackgroundCornersAreTransparentAndInteriorKeepsTheSurface) {
    for (const auto& theme : synth::theme::builtInThemes()) {
        AppLookAndFeel laf;
        laf.applyTheme(theme);
        juce::Image img(juce::Image::ARGB, kWidth, kHeight, true, juce::SoftwareImageType());
        {
            juce::Graphics g(img);
            laf.drawPopupMenuBackground(g, kWidth, kHeight);
        }
        for (const auto& corner : {juce::Point<int>(0, 0), juce::Point<int>(kWidth - 1, 0),
                                   juce::Point<int>(0, kHeight - 1), juce::Point<int>(kWidth - 1, kHeight - 1)})
            EXPECT_EQ(img.getPixelAt(corner.x, corner.y).getAlpha(), 0) << theme.name;
        // The interior carries the theme's own surface alpha (some themes use a translucent surface).
        EXPECT_EQ(img.getPixelAt(kWidth / 2, kHeight / 2).getAlpha(), theme.colors.surface.getAlpha()) << theme.name;
    }
}

} // namespace
