// Concern: the shortcut-hint overlay's Shift hold: which targets it shows, and that a click or a chord
// keeps it out of the way.
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Chrome/ShortcutHint/ShortcutHintOverlay.h"
#include "UI/Chrome/ShortcutHint/ShortcutHintText.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace {

using synth::ui::ShortcutHintOverlay;
constexpr auto kShift = juce::ModifierKeys::shiftModifier;
const juce::ModifierKeys kShiftOnly(kShift), kNoMods;

// newPatch is bound to Shift+3 (a shape-style key), undo to a bare J, redo to Cmd+Shift+K.
class ShortcutHintShiftTest : public ::testing::Test {
protected:
    void SetUp() override {
        lookAndFeel_.applyTheme(synth::theme::makeObsidian());
        host_.setLookAndFeel(&lookAndFeel_);
        host_.setSize(800, 600);
        for (auto* c : {&shiftButton_, &bareButton_, &chordButton_})
            host_.addAndMakeVisible(c);
        shiftButton_.setBounds(10, 4, 40, 40);
        bareButton_.setBounds(200, 4, 40, 40);
        chordButton_.setBounds(600, 4, 40, 40);
        shortcuts_.setBinding("newPatch", juce::KeyPress('3', kShift, 0));
        shortcuts_.setBinding("undo", juce::KeyPress('j', 0, 0));
        shortcuts_.setBinding("redo", juce::KeyPress('k', juce::ModifierKeys::commandModifier | kShift, 0));

        overlay_ = std::make_unique<ShortcutHintOverlay>(host_, shortcuts_);
        overlay_->addTarget(shiftButton_, "newPatch");
        overlay_->addTarget(bareButton_, "undo");
        overlay_->addTarget(chordButton_, "redo");
        overlay_->setClockForTest([this] { return nowMs_; });
    }
    void TearDown() override {
        overlay_.reset();
        host_.setLookAndFeel(nullptr);
    }

    void hold(const juce::ModifierKeys& mods, double ms = ShortcutHintOverlay::kShowDelayMs) {
        overlay_->modifierKeysChanged(mods);
        nowMs_ += ms;
        overlay_->modifierKeysChanged(mods);
    }
    bool hasBubbleOver(const juce::Component& c) const {
        const auto centre = host_.getLocalArea(&c, c.getLocalBounds()).getCentreX();
        for (const auto& e : overlay_->getEntries())
            if (e.bounds.getCentreX() == centre)
                return true;
        return false;
    }

    synth::theme::AppLookAndFeel lookAndFeel_;
    ShortcutManager shortcuts_;
    juce::Component host_;
    juce::TextButton shiftButton_{"s"}, bareButton_{"b"}, chordButton_{"c"};
    std::unique_ptr<ShortcutHintOverlay> overlay_;
    double nowMs_ = 1000.0;
};

} // namespace

TEST_F(ShortcutHintShiftTest, ShiftAloneShowsOnlyTargetsWhoseBindingUsesShift) {
    hold(kShiftOnly, 100.0);
    EXPECT_TRUE(overlay_->isPending());
    EXPECT_FALSE(overlay_->areHintsShowing()) << "not before the delay";
    nowMs_ += ShortcutHintOverlay::kShowDelayMs;
    overlay_->modifierKeysChanged(kShiftOnly);
    ASSERT_TRUE(overlay_->areHintsShowing());
    EXPECT_TRUE(hasBubbleOver(shiftButton_));
    EXPECT_TRUE(hasBubbleOver(chordButton_)) << "a chord that includes Shift uses Shift";
    EXPECT_FALSE(hasBubbleOver(bareButton_));
    EXPECT_EQ(overlay_->getEntries().size(), 2u);
    overlay_->modifierKeysChanged(kNoMods);
    EXPECT_FALSE(overlay_->isPending());
}

TEST_F(ShortcutHintShiftTest, AMousePressWhileShiftIsHeldCancelsUntilShiftIsReleased) {
    overlay_->modifierKeysChanged(kShiftOnly);
    ASSERT_TRUE(overlay_->isPending());
    // Shift+drag on a lane: the press arrives with the button down.
    overlay_->modifierKeysChanged(juce::ModifierKeys(kShift | juce::ModifierKeys::leftButtonModifier));
    EXPECT_FALSE(overlay_->isPending());
    hold(kShiftOnly, 600.0);
    EXPECT_FALSE(overlay_->areHintsShowing()) << "the same hold never hints after a click";
    overlay_->modifierKeysChanged(kNoMods);
    hold(kShiftOnly);
    EXPECT_TRUE(overlay_->areHintsShowing());
}

TEST_F(ShortcutHintShiftTest, AShiftedKeyCancels) {
    hold(kShiftOnly);
    ASSERT_TRUE(overlay_->areHintsShowing());
    EXPECT_FALSE(overlay_->keyPressed(juce::KeyPress('3', kShift, 0), &host_)) << "never consumes the key";
    EXPECT_FALSE(overlay_->areHintsShowing());
}

TEST_F(ShortcutHintShiftTest, ShiftPlusAnotherModifierIsAChord) {
    overlay_->modifierKeysChanged(juce::ModifierKeys(kShift | juce::ModifierKeys::altModifier));
    EXPECT_FALSE(overlay_->isPending());
}
