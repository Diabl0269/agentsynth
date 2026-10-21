// LaneChipRowTests.cpp -- the shared lane chip row: several chips on at once, one Tab stop with arrow keys and Space,
// and a named toggle button per chip for the mouse and a screen reader.
#include "UI/Layout/LaneChips/LaneChipRow.h"
#include <gtest/gtest.h>

using synth::ui::LaneChipRow;

TEST(LaneChipRow, EveryChipIsANamedToggleWithATooltip) {
    LaneChipRow row;
    row.setLanes({"Pitch", "Gate", "Velocity"});
    ASSERT_EQ(row.getNumChips(), 3);
    EXPECT_EQ(row.getChip(1)->getTitle(), "Gate lane");
    EXPECT_EQ(row.getChip(1)->getTooltip(), "Show the Gate lane");
}

TEST(LaneChipRow, SeveralChipsCanBeOnAtOnceAndSetShownDoesNotNotify) {
    LaneChipRow row;
    row.setLanes({"Pitch", "Gate", "Velocity"});
    int notified = 0;
    row.onToggle = [&](int, bool) { ++notified; };
    row.setShown(0, true);
    row.setShown(2, true);
    EXPECT_TRUE(row.isShown(0));
    EXPECT_FALSE(row.isShown(1));
    EXPECT_TRUE(row.isShown(2));
    EXPECT_EQ(notified, 0);
}

TEST(LaneChipRow, AClickNotifiesWithTheNewState) {
    LaneChipRow row;
    row.setLanes({"Pitch", "Gate"});
    int index = -1;
    bool shown = false;
    row.onToggle = [&](int i, bool s) {
        index = i;
        shown = s;
    };
    row.getChip(1)->setToggleState(true, juce::sendNotificationSync); // what a click does
    EXPECT_EQ(index, 1);
    EXPECT_TRUE(shown);
    EXPECT_TRUE(row.isShown(1));
}

TEST(LaneChipRow, ArrowKeysMoveTheFocusedChipAndSpaceTogglesIt) {
    LaneChipRow row;
    row.setLanes({"Pitch", "Gate", "Velocity"});
    row.setSize(300, LaneChipRow::kHeight);
    int toggled = -1;
    row.onToggle = [&](int i, bool) { toggled = i; };
    EXPECT_EQ(row.getFocusedChip(), 0);
    EXPECT_TRUE(row.keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    EXPECT_EQ(row.getFocusedChip(), 1);
    EXPECT_TRUE(row.keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)));
    EXPECT_EQ(toggled, 1);
    EXPECT_TRUE(row.keyPressed(juce::KeyPress(juce::KeyPress::leftKey)));
    EXPECT_EQ(row.getFocusedChip(), 0);
    EXPECT_FALSE(row.keyPressed(juce::KeyPress('x')));
}
