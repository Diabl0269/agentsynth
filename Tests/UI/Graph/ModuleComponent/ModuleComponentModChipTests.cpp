// FRO288: the hover chip's text formatting (formatModHoverChipText, ModuleComponentModChip.h) --
// no ModuleComponent, no LookAndFeel, no graph. See ModuleComponentModBandTests.cpp for the sibling
// depth-band geometry test and its own file-header note on the split.

#include "UI/Graph/ModuleComponent/ModuleComponentModChip.h"
#include <gtest/gtest.h>

using synth::ui::formatModHoverChipText;

TEST(ModHoverChipTextTest, PositiveAmountGetsAnExplicitPlusSign) {
    EXPECT_EQ(formatModHoverChipText("LFO", 0.63f), juce::String::fromUTF8("LFO \xC2\xB7 +63%"));
}

TEST(ModHoverChipTextTest, NegativeAmountKeepsItsOwnMinusSign) {
    EXPECT_EQ(formatModHoverChipText("Env 2", -0.40f), juce::String::fromUTF8("Env 2 \xC2\xB7 -40%"));
}

TEST(ModHoverChipTextTest, ZeroAmountHasNoSign) {
    EXPECT_EQ(formatModHoverChipText("LFO", 0.0f), juce::String::fromUTF8("LFO \xC2\xB7 0%"));
}

TEST(ModHoverChipTextTest, RoundsToTheNearestPercent) {
    // 0.126 -> 12.6% rounds to 13%.
    EXPECT_EQ(formatModHoverChipText("LFO", 0.126f), juce::String::fromUTF8("LFO \xC2\xB7 +13%"));
}
