// Alert box icon placement: the icon is a 22 px square wholly inside the window for any window size.

#include "UI/Theme/AppLookAndFeel/AlertIconBounds.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace {

using juce::Rectangle;

TEST(AlertIconBoundsTest, TheIconIsTwentyTwoSquareAndInsideTheWindow) {
    const std::vector<Rectangle<int>> windows = {{0, 0, 350, 160}, {0, 0, 600, 300}, {0, 0, 400, 120}, {0, 0, 120, 60}};
    for (const auto& window : windows) {
        const auto icon = synth::ui::alertIconBounds(window);
        EXPECT_EQ(icon.getWidth(), 22) << window.toString();
        EXPECT_EQ(icon.getHeight(), 22) << window.toString();
        EXPECT_TRUE(window.contains(icon)) << window.toString() << " vs " << icon.toString();
    }
}

TEST(AlertIconBoundsTest, TheIconEndsTenPixelsBeforeTheMessageAtTheTitleTop) {
    const auto icon = synth::ui::alertIconBounds({0, 0, 400, 200});
    EXPECT_EQ(81 - icon.getRight(), 10) << "JUCE starts the message 81 px in";
    EXPECT_EQ(icon.getY(), 30) << "the message and its title start 30 px down";
}

} // namespace
