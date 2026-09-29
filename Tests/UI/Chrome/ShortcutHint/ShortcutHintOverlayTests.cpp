// Concern: the Cmd-hold shortcut-hint overlay's timing and cancel rules, which buttons it labels,
// where the bubbles go and what they say, and the key cap's rendering. Events are delivered
// through the overlay's real handlers (modifierKeysChanged, the KeyListener and focus callbacks).
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
const juce::ModifierKeys kCmdOnly(kCmd);
const juce::ModifierKeys kNoMods;

class ShortcutHintOverlayTest : public ::testing::Test {
protected:
    void SetUp() override {
        lookAndFeel_.applyTheme(synth::theme::makeObsidian());
        host_.setLookAndFeel(&lookAndFeel_);
        host_.setSize(800, 600);
        for (auto* b : {&newButton_, &undoButton_, &panelButton_})
            host_.addAndMakeVisible(b);
        newButton_.setBounds(10, 4, 40, 40);
        undoButton_.setBounds(200, 4, 40, 40);
        panelButton_.setBounds(600, 4, 40, 40);

        overlay_ = std::make_unique<ShortcutHintOverlay>(host_, shortcuts_);
        overlay_->addTarget(newButton_, "newPatch");
        overlay_->addTarget(undoButton_, "undo");
        overlay_->addTarget(panelButton_, "toggleBottomPanel");
        overlay_->setClockForTest([this] { return nowMs_; });
    }
    void TearDown() override {
        overlay_.reset();
        host_.setLookAndFeel(nullptr);
    }

    // Presses Cmd (the real modifierKeysChanged handler) and lets `ms` pass before the next sample.
    void holdCmdFor(double ms) {
        overlay_->modifierKeysChanged(kCmdOnly);
        nowMs_ += ms;
        overlay_->modifierKeysChanged(kCmdOnly);
    }
    juce::String textFor(const juce::Component& c) const {
        for (const auto& e : overlay_->getEntries())
            if (e.bounds.getCentreX() == host_.getLocalArea(&c, c.getLocalBounds()).getCentreX())
                return e.keyText;
        return {};
    }

    synth::theme::AppLookAndFeel lookAndFeel_;
    ShortcutManager shortcuts_;
    juce::Component host_;
    juce::TextButton newButton_{"new"}, undoButton_{"undo"}, panelButton_{"panel"};
    std::unique_ptr<ShortcutHintOverlay> overlay_;
    double nowMs_ = 1000.0;
};

} // namespace

TEST_F(ShortcutHintOverlayTest, CmdHeldAloneHalfASecondShowsTheHints) {
    overlay_->modifierKeysChanged(kCmdOnly);
    EXPECT_TRUE(overlay_->isPending());
    EXPECT_FALSE(overlay_->areHintsShowing());

    nowMs_ += 499.0;
    overlay_->modifierKeysChanged(kCmdOnly);
    EXPECT_FALSE(overlay_->areHintsShowing()) << "one millisecond early";

    nowMs_ += 1.0;
    overlay_->modifierKeysChanged(kCmdOnly);
    EXPECT_TRUE(overlay_->areHintsShowing());
    EXPECT_TRUE(overlay_->isVisible());
    EXPECT_EQ(overlay_->getEntries().size(), 3u);
    EXPECT_GT(overlay_->getOpacity(), 0.0f);
}

TEST_F(ShortcutHintOverlayTest, CmdThenAnotherKeyBeforeHalfASecondNeverShowsHints) {
    overlay_->modifierKeysChanged(kCmdOnly);
    nowMs_ += 100.0;
    EXPECT_FALSE(overlay_->keyPressed(juce::KeyPress('s', kCmd, 0), &host_)) << "the key is never consumed";
    EXPECT_FALSE(overlay_->isPending());

    nowMs_ += 900.0;
    overlay_->modifierKeysChanged(kCmdOnly);
    EXPECT_FALSE(overlay_->areHintsShowing()) << "Cmd is still down, but this hold was already spent on a shortcut";
    EXPECT_FALSE(overlay_->isVisible());
}

TEST_F(ShortcutHintOverlayTest, AShortcutPressedBeforeCmdWasEverSampledStillSpendsTheHold) {
    EXPECT_FALSE(overlay_->keyPressed(juce::KeyPress('s', kCmd, 0), &host_));
    holdCmdFor(600.0);
    EXPECT_FALSE(overlay_->areHintsShowing());

    overlay_->modifierKeysChanged(kNoMods); // release re-arms
    holdCmdFor(500.0);
    EXPECT_TRUE(overlay_->areHintsShowing());
}

TEST_F(ShortcutHintOverlayTest, AnotherKeyWhileVisibleHidesTheHintsAtOnce) {
    holdCmdFor(500.0);
    ASSERT_TRUE(overlay_->areHintsShowing());

    overlay_->keyPressed(juce::KeyPress('z', kCmd, 0), &host_);
    EXPECT_FALSE(overlay_->areHintsShowing());
    EXPECT_FALSE(overlay_->isVisible());
    EXPECT_EQ(overlay_->getOpacity(), 0.0f) << "no fade on cancel";
    EXPECT_TRUE(overlay_->getEntries().empty());
}

TEST_F(ShortcutHintOverlayTest, LosingWindowFocusHidesTheHintsAtOnce) {
    holdCmdFor(500.0);
    ASSERT_TRUE(overlay_->areHintsShowing());
    overlay_->globalFocusChanged(nullptr);
    EXPECT_FALSE(overlay_->areHintsShowing());
    EXPECT_FALSE(overlay_->isVisible());

    overlay_->modifierKeysChanged(kCmdOnly);
    nowMs_ += 600.0;
    overlay_->modifierKeysChanged(kCmdOnly);
    EXPECT_FALSE(overlay_->areHintsShowing()) << "same hold: stays cancelled until Cmd is released";
}

TEST_F(ShortcutHintOverlayTest, ChangingFocusBetweenComponentsDoesNotCancel) {
    holdCmdFor(500.0);
    overlay_->globalFocusChanged(&newButton_);
    EXPECT_TRUE(overlay_->areHintsShowing());
}

TEST_F(ShortcutHintOverlayTest, AMouseButtonDownCancels) {
    holdCmdFor(500.0);
    ASSERT_TRUE(overlay_->areHintsShowing());
    overlay_->modifierKeysChanged(juce::ModifierKeys(kCmd | juce::ModifierKeys::leftButtonModifier));
    EXPECT_FALSE(overlay_->areHintsShowing());
}

TEST_F(ShortcutHintOverlayTest, CmdWithAnotherModifierNeverStartsTheDelay) {
    overlay_->modifierKeysChanged(juce::ModifierKeys(kCmd | juce::ModifierKeys::shiftModifier));
    EXPECT_FALSE(overlay_->isPending());
}

TEST_F(ShortcutHintOverlayTest, ReleasingCmdBeforeTheDelayCancelsThePendingShow) {
    overlay_->modifierKeysChanged(kCmdOnly);
    nowMs_ += 300.0;
    overlay_->modifierKeysChanged(kNoMods);
    EXPECT_FALSE(overlay_->isPending());
    nowMs_ += 600.0;
    overlay_->modifierKeysChanged(kNoMods);
    EXPECT_FALSE(overlay_->areHintsShowing());
}

TEST_F(ShortcutHintOverlayTest, ReleasingCmdHidesTheHints) {
    holdCmdFor(500.0);
    ASSERT_TRUE(overlay_->areHintsShowing());
    overlay_->modifierKeysChanged(kNoMods);
    EXPECT_FALSE(overlay_->areHintsShowing()) << "no window on screen to fade against: lands immediately";
    EXPECT_FALSE(overlay_->isVisible());
}

TEST_F(ShortcutHintOverlayTest, OnlyShowingComponentsGetAHint) {
    undoButton_.setVisible(false);
    holdCmdFor(500.0);
    ASSERT_TRUE(overlay_->areHintsShowing());
    EXPECT_EQ(overlay_->getEntries().size(), 2u);
}

TEST_F(ShortcutHintOverlayTest, AComponentCoveredByAnotherIsSkipped) {
    juce::TextButton cover("cover");
    host_.addAndMakeVisible(cover);
    cover.setBounds(undoButton_.getBounds());
    holdCmdFor(500.0);
    EXPECT_EQ(overlay_->getEntries().size(), 2u);
}

TEST_F(ShortcutHintOverlayTest, AnUnboundActionGetsNoHint) {
    shortcuts_.setBinding("undo", juce::KeyPress());
    holdCmdFor(500.0);
    EXPECT_EQ(overlay_->getEntries().size(), 2u);
}

TEST_F(ShortcutHintOverlayTest, BubblesShowTheBoundKeyUnderTheButton) {
    holdCmdFor(500.0);
    const auto expected = synth::ui::hint::formatKeyCapTextForPlatform(shortcuts_.getBinding("newPatch"));
    ASSERT_FALSE(expected.isEmpty());
    EXPECT_EQ(textFor(newButton_), expected);
    for (const auto& e : overlay_->getEntries()) {
        EXPECT_FALSE(e.isPill);
        EXPECT_EQ(e.bounds.getHeight(), synth::theme::AppLookAndFeel::kKeyCapHeight);
    }
}

TEST_F(ShortcutHintOverlayTest, ARebindChangesTheHintTextBothBeforeAndWhileShowing) {
    shortcuts_.setBinding("newPatch", juce::KeyPress('j', kCmd, 0));
    holdCmdFor(500.0);
    EXPECT_EQ(textFor(newButton_), synth::ui::hint::formatKeyCapTextForPlatform(juce::KeyPress('j', kCmd, 0)));

    shortcuts_.setBinding("newPatch", juce::KeyPress('h', kCmd, 0));
    EXPECT_EQ(textFor(newButton_), synth::ui::hint::formatKeyCapTextForPlatform(juce::KeyPress('h', kCmd, 0)))
        << "a rebind while the hints are up updates them";
}

TEST_F(ShortcutHintOverlayTest, TheOverlayNeverTakesClicksAndIsHiddenFromAccessibility) {
    bool allowsClicks = true, allowsChildClicks = true;
    overlay_->getInterceptsMouseClicks(allowsClicks, allowsChildClicks);
    EXPECT_FALSE(allowsClicks);
    EXPECT_FALSE(allowsChildClicks);
    EXPECT_FALSE(overlay_->hitTest(10, 10));
    EXPECT_FALSE(overlay_->isAccessible());
    EXPECT_FALSE(overlay_->getWantsKeyboardFocus());

    holdCmdFor(500.0);
    EXPECT_EQ(host_.getComponentAt(newButton_.getBounds().getCentre()), &newButton_);
}

TEST_F(ShortcutHintOverlayTest, ShowingNeverMovesOrResizesAnything) {
    const auto before = newButton_.getBounds();
    holdCmdFor(500.0);
    EXPECT_EQ(newButton_.getBounds(), before);
    EXPECT_EQ(overlay_->getBounds(), host_.getLocalBounds());
    host_.setSize(900, 700);
    EXPECT_EQ(overlay_->getBounds(), host_.getLocalBounds()) << "tracks the host's size";
}

TEST_F(ShortcutHintOverlayTest, HiddenDockShowsARowOfPillsInTabOrderAlongTheBottomEdge) {
    juce::Component statusBar;
    host_.addAndMakeVisible(statusBar);
    statusBar.setBounds(0, 576, 800, 24);
    juce::TextButton timelineTab("Timeline"), mixerTab("Mixer");
    overlay_->setDockSource([&] {
        synth::ui::DockHintInfo info;
        info.open = false;
        info.statusBar = &statusBar;
        info.toggle = &panelButton_;
        info.toggleActionId = "toggleBottomPanel";
        info.toggleLabel = "Show Panel";
        info.tabs = {{&timelineTab, "toggleTimelinePanel", "Timeline"}, {&mixerTab, "toggleMixerPanel", "Mixer"}};
        return info;
    });
    holdCmdFor(500.0);
    ASSERT_TRUE(overlay_->areHintsShowing());

    std::vector<ShortcutHintOverlay::Entry> pills;
    for (const auto& e : overlay_->getEntries())
        if (e.isPill)
            pills.push_back(e);
    ASSERT_EQ(pills.size(), 3u);
    EXPECT_EQ(pills[0].label, "Show Panel");
    EXPECT_EQ(pills[1].label, "Timeline");
    EXPECT_EQ(pills[2].label, "Mixer");
    for (size_t i = 0; i < pills.size(); ++i) {
        EXPECT_EQ(pills[i].bounds.getBottom(), 576 - synth::ui::hint::kRowBottomMargin);
        EXPECT_TRUE(pills[i].bounds.contains(pills[i].cap));
        if (i > 0)
            EXPECT_GT(pills[i].bounds.getX(), pills[i - 1].bounds.getRight());
    }
    EXPECT_NEAR(pills.front().bounds.getX(), host_.getWidth() - pills.back().bounds.getRight(), 2);
}

TEST_F(ShortcutHintOverlayTest, OpenDockPutsTheBubbleInsideEachTab) {
    juce::Component dock;
    host_.addAndMakeVisible(dock);
    dock.setBounds(0, 380, 800, 220);
    juce::TextButton tab("Timeline");
    dock.addAndMakeVisible(tab);
    tab.setBounds(0, 4, 260, 18);
    overlay_->setDockSource([&] {
        synth::ui::DockHintInfo info;
        info.open = true;
        info.dock = &dock;
        info.tabs = {{&tab, "toggleTimelinePanel", "Timeline"}};
        return info;
    });
    holdCmdFor(500.0);

    const auto tabBounds = host_.getLocalArea(&tab, tab.getLocalBounds());
    bool found = false;
    for (const auto& e : overlay_->getEntries())
        if (tabBounds.contains(e.bounds)) {
            found = true;
            EXPECT_EQ(e.bounds.getHeight(), synth::theme::AppLookAndFeel::kKeyCapCompactHeight);
            EXPECT_GT(e.bounds.getX(), tabBounds.getCentreX()) << "after the centred tab name";
        }
    EXPECT_TRUE(found);
}

TEST(ShortcutHintKeyCap, RendersARaisedCapIntoASoftwareImage) {
    synth::theme::AppLookAndFeel lf;
    lf.applyTheme(synth::theme::makeObsidian());
    const auto& colors = lf.getTheme().colors;

    const juce::String text = "T";
    const int w = lf.getShortcutKeyCapWidth(text);
    EXPECT_GE(w, synth::theme::AppLookAndFeel::kKeyCapMinWidth);

    // SoftwareImageType: the default (native) image type on Windows is Direct2D-backed.
    juce::Image img(juce::Image::ARGB, w + 20, 40, true, juce::SoftwareImageType());
    {
        juce::Graphics g(img);
        lf.drawShortcutKeyCap(g, {10, 10, w, synth::theme::AppLookAndFeel::kKeyCapHeight}, text);
    }

    EXPECT_EQ(img.getPixelAt(0, 0).getAlpha(), 0) << "nothing outside the cap and its shadow";
    EXPECT_EQ(img.getPixelAt(10 + 3, 10 + 3), colors.surfaceHi) << "the key face";
    EXPECT_EQ(img.getPixelAt(10 + w / 2, 10 + synth::theme::AppLookAndFeel::kKeyCapHeight - 1), colors.border)
        << "the thicker bottom edge";
    EXPECT_GT(img.getPixelAt(10 + w / 2, 10 + synth::theme::AppLookAndFeel::kKeyCapHeight).getAlpha(), 0)
        << "the soft shadow under the cap";

    bool hasLabelPixel = false;
    for (int y = 12; y < 22 && !hasLabelPixel; ++y)
        for (int x = 12; x < 10 + w - 2 && !hasLabelPixel; ++x)
            hasLabelPixel = img.getPixelAt(x, y).getBrightness() > colors.surfaceHi.getBrightness() + 0.2f;
    EXPECT_TRUE(hasLabelPixel) << "the key text is drawn in the primary text colour";
}
