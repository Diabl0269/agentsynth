// SplitButtonTests.cpp -- the shared split button (mod dot "Add source", hosted card "Add controls"): two halves
// with configurable icons, names and tooltips; lit state; and the Left/Right hop between them. Its two real uses
// are covered where they live (ModDotInlineAddTests.cpp, PluginKnobPickerTests.cpp).
#include "UI/Layout/SplitButton.h"
#include <gtest/gtest.h>

using synth::ui::ModDotGlyph;
using synth::ui::SplitButton;
using synth::ui::SplitButtonHalfSpec;

namespace {
bool pressKey(juce::Component& target, const juce::KeyPress& key) { return target.keyPressed(key); }
SplitButtonHalfSpec iconOnly(ModDotGlyph glyph, const char* title, const char* lit = "") {
    return {glyph, {}, title, lit, title};
}
} // namespace

TEST(SplitButtonTest, IconOnlyHalvesSplitTheWidthEvenlyAndCarryTheirNamesAndTooltips) {
    SplitButton split(iconOnly(ModDotGlyph::List, "Add from list"),
                      iconOnly(ModDotGlyph::Hand, "Add by moving", "Add by moving, on"), "Add controls");
    split.setSize(80, SplitButton::kHeight);

    EXPECT_EQ(split.getTitle(), "Add controls");
    EXPECT_EQ(split.leftHalf().getTitle(), "Add from list");
    EXPECT_EQ(split.leftHalf().getTooltip(), "Add from list");
    EXPECT_EQ(split.rightHalf().getTooltip(), "Add by moving");
    EXPECT_TRUE(split.leftHalf().getWantsKeyboardFocus());
    EXPECT_TRUE(split.rightHalf().getWantsKeyboardFocus());
    EXPECT_EQ(split.leftHalf().getWidth(), 40);
    EXPECT_EQ(split.leftHalf().getRight(), split.rightHalf().getX());
    EXPECT_EQ(split.rightHalf().getRight(), 80);
}

TEST(SplitButtonTest, LabelledHalvesGiveTheLeftOneMoreRoom) {
    SplitButton split({ModDotGlyph::List, "Add source", "Add source", "", "Add source"},
                      {ModDotGlyph::Crosshair, "Pick on canvas", "Pick on canvas", "", "Pick"}, "Add a source");
    split.setSize(200, SplitButton::kHeight);
    EXPECT_GT(split.leftHalf().getWidth(), split.rightHalf().getWidth());
}

TEST(SplitButtonTest, LitStateRenamesAHalfThatHasALitNameAndTogglesTheRightHalfForAssistiveTech) {
    SplitButton split(iconOnly(ModDotGlyph::List, "Open list", "Open list, open"),
                      iconOnly(ModDotGlyph::Hand, "Add by moving", "Add by moving, on"), "Add controls");

    split.setRightLit(true);
    EXPECT_TRUE(split.isRightLit());
    EXPECT_TRUE(split.rightHalf().getToggleState());
    EXPECT_EQ(split.rightHalf().getTitle(), "Add by moving, on");
    split.setRightLit(false);
    EXPECT_FALSE(split.isRightLit());
    EXPECT_FALSE(split.rightHalf().getToggleState());
    EXPECT_EQ(split.rightHalf().getTitle(), "Add by moving");

    split.setLeftLit(true);
    EXPECT_TRUE(split.isLeftLit());
    EXPECT_EQ(split.leftHalf().getTitle(), "Open list, open");
    split.setLeftLit(false);
    EXPECT_EQ(split.leftHalf().getTitle(), "Open list");
}

TEST(SplitButtonTest, ReturnAndSpaceClickAHalfAndArrowsHopBetweenTheHalves) {
    SplitButton split(iconOnly(ModDotGlyph::List, "A"), iconOnly(ModDotGlyph::Hand, "B"), "AB");
    int left = 0, right = 0;
    split.leftHalf().onClick = [&] { ++left; };
    split.rightHalf().onClick = [&] { ++right; };

    EXPECT_TRUE(pressKey(split.leftHalf(), juce::KeyPress(juce::KeyPress::returnKey)));
    EXPECT_TRUE(pressKey(split.rightHalf(), juce::KeyPress(juce::KeyPress::spaceKey)));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
    EXPECT_EQ(left, 1);
    EXPECT_EQ(right, 1);

    EXPECT_TRUE(pressKey(split.leftHalf(), juce::KeyPress(juce::KeyPress::rightKey)));
    EXPECT_TRUE(pressKey(split.rightHalf(), juce::KeyPress(juce::KeyPress::leftKey)));
    EXPECT_FALSE(pressKey(split.leftHalf(), juce::KeyPress(juce::KeyPress::leftKey))) << "nothing to the left";
    EXPECT_FALSE(pressKey(split.leftHalf(), juce::KeyPress(juce::KeyPress::downKey))) << "Up/Down are the owner's";
}
