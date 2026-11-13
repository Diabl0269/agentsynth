// ToolbarKeyboardTests.cpp -- the toolbar's roving keyboard focus (docs/layout/chrome.md#toolbar-keyboard-access):
// real juce::KeyPress objects through ToolbarComponent::keyPressed, asserting which button's onClick ran.
#include "UI/Chrome/ToolbarComponent.h"
#include <array>
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace {

class ToolbarKeyboardTest : public ::testing::Test {
protected:
    ToolbarKeyboardTest() {
        parent_.addAndMakeVisible(toolbar_);
        std::array<juce::DrawableButton*, ToolbarComponent::NumSlots> ptrs{};
        for (int slot = 0; slot < ToolbarComponent::NumSlots; ++slot) {
            auto& b = buttons_[(size_t)slot];
            b = std::make_unique<juce::DrawableButton>("b" + juce::String(slot), juce::DrawableButton::ImageFitted);
            b->setTitle("Button " + juce::String(slot));
            b->onClick = [this, slot] { clicked_.push_back(slot); };
            parent_.addAndMakeVisible(*b);
            ptrs[(size_t)slot] = b.get();
        }
        toolbar_.setButtons(ptrs);
        parent_.setSize(1600, 40);
        toolbar_.setBounds(0, 0, 1600, 40);
        toolbar_.layoutButtons(toolbar_.getLocalBounds());
    }

    bool press(int keyCode, juce::ModifierKeys mods = {}) {
        return toolbar_.keyPressed(juce::KeyPress(keyCode, mods, 0));
    }
    juce::DrawableButton& button(int slot) { return *buttons_[(size_t)slot]; }

    juce::Component parent_;
    ToolbarComponent toolbar_;
    std::array<std::unique_ptr<juce::DrawableButton>, ToolbarComponent::NumSlots> buttons_;
    std::vector<int> clicked_;
};

} // namespace

TEST_F(ToolbarKeyboardTest, RingStartsOnTheFirstButton) {
    EXPECT_EQ(toolbar_.getFocusedSlot(), (int)ToolbarComponent::Library);
}

TEST_F(ToolbarKeyboardTest, RightAndLeftMoveTheRingAndReturnPressesTheRingedButton) {
    EXPECT_TRUE(press(juce::KeyPress::rightKey));
    EXPECT_TRUE(press(juce::KeyPress::rightKey));
    EXPECT_EQ(toolbar_.getFocusedSlot(), (int)ToolbarComponent::Save);
    EXPECT_TRUE(press(juce::KeyPress::leftKey));
    EXPECT_EQ(toolbar_.getFocusedSlot(), (int)ToolbarComponent::New);

    EXPECT_TRUE(press(juce::KeyPress::returnKey));
    EXPECT_EQ(clicked_, std::vector<int>{ToolbarComponent::New});
}

TEST_F(ToolbarKeyboardTest, SpacePressesTheRingedButtonToo) {
    press(juce::KeyPress::endKey);
    EXPECT_TRUE(press(juce::KeyPress::spaceKey));
    EXPECT_EQ(clicked_, std::vector<int>{ToolbarComponent::ToggleTheme});
}

TEST_F(ToolbarKeyboardTest, HomeAndEndJumpToTheFirstAndLastButton) {
    press(juce::KeyPress::endKey);
    EXPECT_EQ(toolbar_.getFocusedSlot(), (int)ToolbarComponent::ToggleTheme);
    press(juce::KeyPress::homeKey);
    EXPECT_EQ(toolbar_.getFocusedSlot(), (int)ToolbarComponent::Library);
}

TEST_F(ToolbarKeyboardTest, ArrowsStopAtTheEndsAndDoNotWrap) {
    EXPECT_TRUE(press(juce::KeyPress::leftKey)) << "consumed even though there is nowhere to go";
    EXPECT_EQ(toolbar_.getFocusedSlot(), (int)ToolbarComponent::Library);

    press(juce::KeyPress::endKey);
    EXPECT_TRUE(press(juce::KeyPress::rightKey));
    EXPECT_EQ(toolbar_.getFocusedSlot(), (int)ToolbarComponent::ToggleTheme);
}

TEST_F(ToolbarKeyboardTest, HiddenAndDisabledButtonsAreSkipped) {
    button(ToolbarComponent::New).setVisible(false);
    button(ToolbarComponent::Save).setEnabled(false);

    press(juce::KeyPress::rightKey);
    EXPECT_EQ(toolbar_.getFocusedSlot(), (int)ToolbarComponent::Load);

    button(ToolbarComponent::ToggleTheme).setEnabled(false);
    press(juce::KeyPress::endKey);
    EXPECT_EQ(toolbar_.getFocusedSlot(), (int)ToolbarComponent::ToggleBottomPanel);
}

TEST_F(ToolbarKeyboardTest, ModifiedKeysAreLeftForTheAppShortcuts) {
    EXPECT_FALSE(press(juce::KeyPress::rightKey, juce::ModifierKeys::commandModifier));
    EXPECT_FALSE(press(juce::KeyPress::returnKey, juce::ModifierKeys::shiftModifier));
    EXPECT_FALSE(press('a'));
    EXPECT_EQ(toolbar_.getFocusedSlot(), (int)ToolbarComponent::Library);
    EXPECT_TRUE(clicked_.empty());
}

TEST_F(ToolbarKeyboardTest, RingResumesOnTheLastUsedButtonWhenFocusReturns) {
    press(juce::KeyPress::rightKey);
    press(juce::KeyPress::rightKey);
    press(juce::KeyPress::rightKey);
    toolbar_.focusLost(juce::Component::focusChangedByTabKey);
    toolbar_.focusGained(juce::Component::focusChangedByTabKey);
    EXPECT_EQ(toolbar_.getFocusedSlot(), (int)ToolbarComponent::Load);
}

TEST_F(ToolbarKeyboardTest, RingPassesToANeighbourWhenItsButtonStopsBeingNavigable) {
    button(ToolbarComponent::Undo).onClick = [this] { button(ToolbarComponent::Undo).setEnabled(false); };
    for (int i = 0; i < (int)ToolbarComponent::Undo; ++i)
        press(juce::KeyPress::rightKey);
    ASSERT_EQ(toolbar_.getFocusedSlot(), (int)ToolbarComponent::Undo);

    press(juce::KeyPress::returnKey);
    EXPECT_EQ(toolbar_.getFocusedSlot(), (int)ToolbarComponent::Redo) << "not back on the first button";
}

TEST_F(ToolbarKeyboardTest, NothingIsHandledWhenNoButtonIsNavigable) {
    for (auto& b : buttons_)
        b->setEnabled(false);
    EXPECT_EQ(toolbar_.getFocusedSlot(), -1);
    EXPECT_FALSE(press(juce::KeyPress::rightKey));
    EXPECT_FALSE(press(juce::KeyPress::returnKey));
}

TEST_F(ToolbarKeyboardTest, FocusedButtonNameFollowsTheRingAndButtonsDoNotTakeFocusThemselves) {
    EXPECT_EQ(toolbar_.getFocusedButtonName(), "Button 0");
    press(juce::KeyPress::rightKey);
    EXPECT_EQ(toolbar_.getFocusedButtonName(), "Button 1");

    EXPECT_TRUE(toolbar_.getWantsKeyboardFocus());
    EXPECT_FALSE(toolbar_.getTitle().isEmpty());
    for (auto& b : buttons_)
        EXPECT_FALSE(b->getWantsKeyboardFocus()) << "the toolbar is the one Tab stop";
}
