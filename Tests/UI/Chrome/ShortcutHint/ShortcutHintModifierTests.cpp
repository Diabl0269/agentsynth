// Concern: the shortcut-hint overlay's Ctrl / Option holds: which targets each modifier shows,
// the live-binding filter, painted-area targets, and the per-modifier cancel and latch rules.
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Chrome/ShortcutHint/ShortcutHintOverlay.h"
#include "UI/Chrome/ShortcutHint/ShortcutHintText.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace {

using synth::ui::ShortcutHintOverlay;
constexpr auto kCmd = juce::ModifierKeys::commandModifier;
constexpr auto kAlt = juce::ModifierKeys::altModifier;
constexpr auto kCtrl = juce::ModifierKeys::ctrlModifier;
const juce::ModifierKeys kCmdOnly(kCmd), kAltOnly(kAlt), kNoMods;
#if JUCE_MAC
const juce::ModifierKeys kCtrlOnly(kCtrl);
#endif

// newPatch is bound to Ctrl+J, undo to Alt+J, toggleBottomPanel to Cmd+K; "roll" is a painted-chip owner.
class ShortcutHintModifierTest : public ::testing::Test {
protected:
    void SetUp() override {
        lookAndFeel_.applyTheme(synth::theme::makeObsidian());
        host_.setLookAndFeel(&lookAndFeel_);
        host_.setSize(800, 600);
        for (auto* c : {static_cast<juce::Component*>(&ctrlButton_), static_cast<juce::Component*>(&altButton_),
                        static_cast<juce::Component*>(&cmdButton_), &roll_})
            host_.addAndMakeVisible(c);
        ctrlButton_.setBounds(10, 4, 40, 40);
        altButton_.setBounds(200, 4, 40, 40);
        cmdButton_.setBounds(600, 4, 40, 40);
        roll_.setBounds(0, 100, 800, 400);
        shortcuts_.setBinding("newPatch", juce::KeyPress('j', kCtrl, 0));
        shortcuts_.setBinding("undo", juce::KeyPress('j', kAlt, 0));
        shortcuts_.setBinding("toggleBottomPanel", juce::KeyPress('k', kCmd, 0));
        shortcuts_.setBinding("redo", juce::KeyPress('v', kCtrl, 0));

        overlay_ = std::make_unique<ShortcutHintOverlay>(host_, shortcuts_);
        overlay_->addTarget(ctrlButton_, "newPatch");
        overlay_->addTarget(altButton_, "undo");
        overlay_->addTarget(cmdButton_, "toggleBottomPanel");
        overlay_->addAreaTarget(roll_, [this] { return chip_; }, "redo");
        overlay_->setClockForTest([this] { return nowMs_; });
    }
    void TearDown() override {
        overlay_.reset();
        host_.setLookAndFeel(nullptr);
    }

    void hold(const juce::ModifierKeys& mods, double ms = 500.0) {
        overlay_->modifierKeysChanged(mods);
        nowMs_ += ms;
        overlay_->modifierKeysChanged(mods);
    }
    juce::String textAt(const juce::Rectangle<int>& inOverlay) const {
        for (const auto& e : overlay_->getEntries())
            if (e.bounds.getCentreX() == inOverlay.getCentreX())
                return e.keyText;
        return {};
    }
    juce::Rectangle<int> inOverlay(const juce::Component& c) const {
        return host_.getLocalArea(&c, c.getLocalBounds());
    }
    static juce::String keyText(const juce::KeyPress& k) { return synth::ui::hint::formatKeyCapTextForPlatform(k); }

    synth::theme::AppLookAndFeel lookAndFeel_;
    ShortcutManager shortcuts_;
    juce::Component host_, roll_;
    juce::TextButton ctrlButton_{"c"}, altButton_{"a"}, cmdButton_{"m"};
    juce::Rectangle<int> chip_{300, 10, 60, 20}; // in roll_'s coordinates
    std::unique_ptr<ShortcutHintOverlay> overlay_;
    double nowMs_ = 1000.0;
};

} // namespace

TEST_F(ShortcutHintModifierTest, OptionAloneShowsOnlyAltBoundTargets) {
    hold(kAltOnly);
    ASSERT_TRUE(overlay_->areHintsShowing());
    ASSERT_EQ(overlay_->getEntries().size(), 1u);
    EXPECT_EQ(textAt(inOverlay(altButton_)), keyText(juce::KeyPress('j', kAlt, 0)));
}

TEST_F(ShortcutHintModifierTest, CmdAloneStillShowsEveryBoundTarget) {
    hold(kCmdOnly);
    ASSERT_TRUE(overlay_->areHintsShowing());
    EXPECT_EQ(overlay_->getEntries().size(), 4u);
}

#if JUCE_MAC
TEST_F(ShortcutHintModifierTest, CtrlAloneShowsEveryBoundTargetLikeCmd) {
    hold(kCtrlOnly);
    ASSERT_TRUE(overlay_->areHintsShowing());
    EXPECT_EQ(overlay_->getEntries().size(), 4u) << "bare-key and chord targets alike, the same set Cmd shows";
    EXPECT_EQ(textAt(inOverlay(ctrlButton_)), keyText(juce::KeyPress('j', kCtrl, 0)));
}

TEST_F(ShortcutHintModifierTest, SwitchingFromCmdToCtrlMidHoldCancelsUntilEverythingIsReleased) {
    hold(kCmdOnly);
    ASSERT_TRUE(overlay_->areHintsShowing());
    overlay_->modifierKeysChanged(kCtrlOnly);
    EXPECT_FALSE(overlay_->areHintsShowing());
    EXPECT_FALSE(overlay_->isPending());
    hold(kCtrlOnly, 600.0);
    EXPECT_FALSE(overlay_->areHintsShowing()) << "spent until released";
    overlay_->modifierKeysChanged(kNoMods);
    hold(kCtrlOnly);
    EXPECT_TRUE(overlay_->areHintsShowing());
}

TEST_F(ShortcutHintModifierTest, ACtrlChordLatchesTheHintsOffUntilCtrlIsReleased) {
    EXPECT_FALSE(overlay_->keyPressed(juce::KeyPress('v', kCtrl, 0), &host_));
    hold(kCtrlOnly, 600.0);
    EXPECT_FALSE(overlay_->areHintsShowing());
    overlay_->modifierKeysChanged(kNoMods);
    hold(kCtrlOnly);
    EXPECT_TRUE(overlay_->areHintsShowing());
}

TEST_F(ShortcutHintModifierTest, CtrlPlusAnotherModifierNeverStartsTheDelay) {
    overlay_->modifierKeysChanged(juce::ModifierKeys(kCtrl | juce::ModifierKeys::shiftModifier));
    EXPECT_FALSE(overlay_->isPending());
    overlay_->modifierKeysChanged(juce::ModifierKeys(kCtrl | kAlt));
    EXPECT_FALSE(overlay_->isPending());
}

TEST_F(ShortcutHintModifierTest, RebindingWhileCtrlHintsShowMovesTheBubbleToTheNewKey) {
    hold(kCtrlOnly);
    ASSERT_EQ(overlay_->getEntries().size(), 4u);
    shortcuts_.setBinding("undo", juce::KeyPress('u', kCtrl, 0)); // while showing: rebuilds
    ASSERT_EQ(overlay_->getEntries().size(), 4u);
    EXPECT_EQ(textAt(inOverlay(altButton_)), keyText(juce::KeyPress('u', kCtrl, 0)));
}
#endif

TEST_F(ShortcutHintModifierTest, RebindingMovesATargetInAndOutOfTheOptionSet) {
    hold(kAltOnly);
    ASSERT_EQ(overlay_->getEntries().size(), 1u);
    shortcuts_.setBinding("newPatch", juce::KeyPress('n', kAlt, 0));
    ASSERT_EQ(overlay_->getEntries().size(), 2u);
    EXPECT_EQ(textAt(inOverlay(ctrlButton_)), keyText(juce::KeyPress('n', kAlt, 0)));
    shortcuts_.setBinding("undo", juce::KeyPress('u', kCmd, 0));
    EXPECT_EQ(overlay_->getEntries().size(), 1u);
    EXPECT_TRUE(textAt(inOverlay(altButton_)).isEmpty());
}

TEST_F(ShortcutHintModifierTest, AnAreaTargetGetsABubbleAnchoredAtTheArea) {
    hold(kCmdOnly);
    const auto anchor = host_.getLocalArea(&roll_, chip_);
    bool found = false;
    for (const auto& e : overlay_->getEntries())
        if (e.bounds.getCentreX() == anchor.getCentreX() && e.bounds.getBottom() > anchor.getY() &&
            e.bounds.getY() < anchor.getBottom() + 40)
            found = true;
    EXPECT_TRUE(found) << "a bubble next to the chip, not at the owner's centre";
}

TEST_F(ShortcutHintModifierTest, AnAreaTargetWithAHiddenOwnerOrEmptyAreaGetsNoBubble) {
    roll_.setVisible(false);
    hold(kCmdOnly);
    EXPECT_EQ(overlay_->getEntries().size(), 3u);
    overlay_->modifierKeysChanged(kNoMods);
    roll_.setVisible(true);
    chip_ = {};
    hold(kCmdOnly);
    EXPECT_EQ(overlay_->getEntries().size(), 3u);
}

TEST_F(ShortcutHintModifierTest, SwitchingFromCmdToOptionMidHoldCancels) {
    hold(kCmdOnly);
    ASSERT_TRUE(overlay_->areHintsShowing());
    overlay_->modifierKeysChanged(kAltOnly);
    EXPECT_FALSE(overlay_->areHintsShowing());
    hold(kAltOnly, 600.0);
    EXPECT_FALSE(overlay_->areHintsShowing());
}

TEST_F(ShortcutHintModifierTest, AnOptionChordLatchesTheHintsOffUntilOptionIsReleased) {
    overlay_->keyPressed(juce::KeyPress('s', kAlt, 0), &host_);
    hold(kAltOnly, 600.0);
    EXPECT_FALSE(overlay_->areHintsShowing());
    overlay_->modifierKeysChanged(kNoMods);
    hold(kAltOnly);
    EXPECT_TRUE(overlay_->areHintsShowing());
}

TEST_F(ShortcutHintModifierTest, OptionPlusAnotherModifierNeverStartsTheDelay) {
    overlay_->modifierKeysChanged(juce::ModifierKeys(kAlt | juce::ModifierKeys::shiftModifier));
    EXPECT_FALSE(overlay_->isPending());
}

TEST_F(ShortcutHintModifierTest, AnOtherModifierPressedMidFadeOutRestartsFromPending) {
    hold(kAltOnly);
    ASSERT_TRUE(overlay_->areHintsShowing());
    // No window on screen, so a release lands at once: drive the fade-out state through the real path
    // only when it lingers; otherwise the restart is trivially from Idle.
    overlay_->modifierKeysChanged(kNoMods);
    overlay_->modifierKeysChanged(kCmdOnly);
    EXPECT_TRUE(overlay_->isPending());
    EXPECT_FALSE(overlay_->areHintsShowing());
    nowMs_ += 500.0;
    overlay_->modifierKeysChanged(kCmdOnly);
    EXPECT_EQ(overlay_->getEntries().size(), 4u) << "now in Cmd mode: every target";
}
