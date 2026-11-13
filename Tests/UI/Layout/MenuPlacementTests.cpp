// MenuPlacementTests.cpp -- where right-click context menus open (ContextMenuPlacement.h).
//
// A juce::PopupMenu never opens headlessly and its window placement is private to JUCE, so these
// pin the Options the helpers build: a plain Options() carries an EMPTY target rectangle at the
// mouse, which JUCE places on the nearer screen half (left of the pointer on the right half); a
// non-empty target makes JUCE align the menu's top-left to it and clamp at the screen edge.
#include "UI/Layout/ContextMenuPlacement.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace {

using synth::ui::contextMenuOptions;
using synth::ui::contextMenuOptionsAtPoint;
using synth::ui::contextMenuOptionsAtPointer;

TEST(MenuPlacement, PlainOptionsTargetAnEmptyAreaWhichJuceFlipsLeftOnTheRightHalf) {
    // The cause of the bug: this is what every context menu used before the helper.
    EXPECT_TRUE(juce::PopupMenu::Options().getTargetScreenArea().isEmpty());
}

TEST(MenuPlacement, PointOptionsAlignTheTopLeftToThePointer) {
    const auto options = contextMenuOptionsAtPoint({1700, 420});
    const auto area = options.getTargetScreenArea();
    EXPECT_FALSE(area.isEmpty()) << "an empty target is placed on the nearer screen half, i.e. left of the pointer";
    EXPECT_EQ(area.getTopLeft(), juce::Point<int>(1700, 420));
    EXPECT_EQ(options.getTargetComponent(), nullptr) << "a component target would anchor to it, not the pointer";
}

TEST(MenuPlacement, PointerOptionsUseTheMousePosition) {
    const auto options = contextMenuOptionsAtPointer();
    EXPECT_FALSE(options.getTargetScreenArea().isEmpty());
    EXPECT_EQ(options.getTargetScreenArea().getTopLeft(), juce::Desktop::getMousePosition());
    EXPECT_EQ(options.getTargetComponent(), nullptr);
}

TEST(MenuPlacement, KeyboardAnchorWinsOverThePointer) {
    const juce::Rectangle<int> anchor(300, 200, 120, 24);
    EXPECT_EQ(contextMenuOptions(anchor).getTargetScreenArea(), anchor);
}

TEST(MenuPlacement, NoAnchorMeansThePointer) {
    const auto options = contextMenuOptions(std::nullopt);
    EXPECT_FALSE(options.getTargetScreenArea().isEmpty());
    EXPECT_EQ(options.getTargetScreenArea().getTopLeft(), juce::Desktop::getMousePosition());
}

} // namespace
