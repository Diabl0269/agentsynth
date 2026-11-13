// SettingsWindowShortcutHintsTests.cpp -- holding Cmd over the Settings window labels each tab button
// with its positional Cmd+N, through the window's real modifierKeysChanged handler.
#include "../Accessibility/AccessibilitySettingsFixture.h"
#include "UI/Chrome/ShortcutHint/ShortcutHintText.h"
#include "UI/Settings/SettingsWindow.h"
#include <gtest/gtest.h>

namespace {

constexpr auto kCmd = juce::ModifierKeys::commandModifier;
const juce::ModifierKeys kCmdOnly(kCmd);

class SettingsWindowShortcutHintsTest : public AccessibilitySettingsTest {
protected:
    void SetUp() override {
        AccessibilitySettingsTest::SetUp();
        window = std::make_unique<SettingsWindow>(deviceManager, appProperties, *aiService, *aiChat, shortcutManager,
                                                  themeManager, nullptr);
        window->setSize(SettingsWindow::kDefaultWidth, SettingsWindow::kDefaultHeight);
        window->resized();
        window->getShortcutHintsForTest().setClockForTest([this] { return nowMs; });
    }

    void holdCmdFor(double ms) {
        window->modifierKeysChanged(kCmdOnly);
        nowMs += ms;
        window->modifierKeysChanged(kCmdOnly);
    }

    std::unique_ptr<SettingsWindow> window;
    double nowMs = 1000.0;
};

} // namespace

TEST_F(SettingsWindowShortcutHintsTest, HoldingCmdLabelsEveryTabWithItsPositionalKey) {
    auto& hints = window->getShortcutHintsForTest();
    EXPECT_FALSE(hints.areHintsShowing());
    holdCmdFor(500.0);
    ASSERT_TRUE(hints.areHintsShowing());

    juce::StringArray shown;
    for (const auto& entry : hints.getEntries())
        shown.add(entry.keyText);
    ASSERT_EQ(window->getNumTabs(), 6);
    EXPECT_EQ(shown.size(), window->getNumTabs());
    for (int i = 0; i < window->getNumTabs(); ++i) {
        const auto expected = synth::ui::hint::formatKeyCapTextForPlatform(
            juce::KeyPress('1' + i, juce::ModifierKeys::commandModifier, '1' + i));
        EXPECT_TRUE(shown.contains(expected)) << "no badge reading " << expected << " for tab " << i;
    }
}

TEST_F(SettingsWindowShortcutHintsTest, EachBadgeSitsOnItsOwnTabButton) {
    holdCmdFor(500.0);
    auto& hints = window->getShortcutHintsForTest();
    ASSERT_TRUE(hints.areHintsShowing());
    for (int i = 0; i < window->getNumTabs(); ++i) {
        const auto* button = window->getTabs().getTabbedButtonBar().getTabButton(i);
        ASSERT_NE(button, nullptr);
        const auto buttonBounds = hints.getLocalArea(button, button->getLocalBounds());
        const auto expected = synth::ui::hint::formatKeyCapTextForPlatform(
            juce::KeyPress('1' + i, juce::ModifierKeys::commandModifier, '1' + i));
        bool found = false;
        for (const auto& entry : hints.getEntries())
            found = found || (entry.keyText == expected && entry.bounds.getCentreX() >= buttonBounds.getX() &&
                              entry.bounds.getCentreX() <= buttonBounds.getRight());
        EXPECT_TRUE(found) << "tab " << i;
    }
}

TEST_F(SettingsWindowShortcutHintsTest, ReleasingCmdOrPressingAnotherKeyHidesTheBadges) {
    auto& hints = window->getShortcutHintsForTest();
    holdCmdFor(500.0);
    ASSERT_TRUE(hints.areHintsShowing());
    window->modifierKeysChanged(juce::ModifierKeys());
    EXPECT_FALSE(hints.isPending());

    holdCmdFor(500.0);
    ASSERT_TRUE(hints.areHintsShowing());
    EXPECT_FALSE(hints.keyPressed(juce::KeyPress('z', kCmd, 0), window.get())) << "the chord is never consumed";
    EXPECT_FALSE(hints.areHintsShowing()) << "but it ends the hold's hints";
}

// The app launches Settings as a modal dialog. The overlay stays out of the way of OTHER modals (a menu,
// an alert), but its own window being the modal one must not silence it: before this, no badge ever
// appeared in the real Settings window.
TEST_F(SettingsWindowShortcutHintsTest, BadgesShowWhileTheSettingsWindowItselfIsModal) {
    juce::Component dialog;
    dialog.addAndMakeVisible(*window);
    dialog.enterModalState(false);
    ASSERT_EQ(juce::Component::getCurrentlyModalComponent(), &dialog);
    holdCmdFor(500.0);
    EXPECT_TRUE(window->getShortcutHintsForTest().areHintsShowing());
    dialog.exitModalState(0);
    dialog.removeChildComponent(window.get());
}

TEST_F(SettingsWindowShortcutHintsTest, AnotherModalOnTopStillSilencesTheBadges) {
    juce::Component alert;
    alert.enterModalState(false);
    ASSERT_EQ(juce::Component::getCurrentlyModalComponent(), &alert);
    holdCmdFor(500.0);
    EXPECT_FALSE(window->getShortcutHintsForTest().areHintsShowing());
    alert.exitModalState(0);
}
