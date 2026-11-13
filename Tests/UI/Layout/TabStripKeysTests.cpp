// TabStripKeysTests.cpp -- the pure key-to-tab decision shared by every one-stop tab strip.
#include "UI/Layout/TabStripKeys.h"
#include <gtest/gtest.h>

using synth::ui::tabStripKeyTarget;

namespace {
juce::KeyPress key(int code, int mods = 0) { return juce::KeyPress(code, juce::ModifierKeys(mods), 0); }
} // namespace

TEST(TabStripKeys, LeftAndRightStepToTheNeighbourAndStopAtTheEnds) {
    EXPECT_EQ(tabStripKeyTarget(key(juce::KeyPress::rightKey), 0, 3), 1);
    EXPECT_EQ(tabStripKeyTarget(key(juce::KeyPress::rightKey), 1, 3), 2);
    EXPECT_EQ(tabStripKeyTarget(key(juce::KeyPress::rightKey), 2, 3), 2) << "no wrap past the last tab";
    EXPECT_EQ(tabStripKeyTarget(key(juce::KeyPress::leftKey), 2, 3), 1);
    EXPECT_EQ(tabStripKeyTarget(key(juce::KeyPress::leftKey), 0, 3), 0) << "no wrap before the first tab";
}

TEST(TabStripKeys, HomeAndEndJumpToTheFirstAndLastTab) {
    EXPECT_EQ(tabStripKeyTarget(key(juce::KeyPress::homeKey), 2, 4), 0);
    EXPECT_EQ(tabStripKeyTarget(key(juce::KeyPress::endKey), 1, 4), 3);
}

TEST(TabStripKeys, AnyModifierOrOtherKeyIsNotTheStrips) {
    for (int mods : {(int)juce::ModifierKeys::commandModifier, (int)juce::ModifierKeys::shiftModifier,
                     (int)juce::ModifierKeys::altModifier, (int)juce::ModifierKeys::ctrlModifier})
        EXPECT_FALSE(tabStripKeyTarget(key(juce::KeyPress::rightKey, mods), 0, 3).has_value());
    for (int code : {(int)juce::KeyPress::upKey, (int)juce::KeyPress::downKey, (int)juce::KeyPress::tabKey,
                     (int)juce::KeyPress::returnKey, (int)juce::KeyPress::spaceKey, (int)'a'})
        EXPECT_FALSE(tabStripKeyTarget(key(code), 0, 3).has_value()) << code;
}

TEST(TabStripKeys, NoTabsMeansNoTarget) {
    EXPECT_FALSE(tabStripKeyTarget(key(juce::KeyPress::rightKey), 0, 0).has_value());
    EXPECT_FALSE(tabStripKeyTarget(key(juce::KeyPress::homeKey), 0, 0).has_value());
}

TEST(TabStripKeys, AnOutOfRangeCurrentTabIsClamped) {
    EXPECT_EQ(tabStripKeyTarget(key(juce::KeyPress::rightKey), 7, 3), 2);
    EXPECT_EQ(tabStripKeyTarget(key(juce::KeyPress::leftKey), -1, 3), 0);
}
