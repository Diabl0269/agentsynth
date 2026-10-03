// SettingsWindowEscapeFadeTests.cpp -- Escape on the Preferences window fades it out instead of
// closing it directly. The key goes through the real SettingsWindow::keyPressed handler
// inside a real juce::DialogWindow; only the native peer is missing, which PopupMotion's
// off-screen seam stands in for.

#include "../Accessibility/AccessibilitySettingsFixture.h"
#include "UI/Layout/PopupMotion.h"
#include "UI/Settings/SettingsWindow.h"
#include <functional>
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace {

using synth::ui::PopupMotion;

class RecordingDialog : public juce::DialogWindow {
public:
    RecordingDialog()
        : juce::DialogWindow("Preferences", juce::Colours::black, true, false) {}
    void closeButtonPressed() override { ++closeCount; }
    int closeCount = 0;
};

class SettingsWindowEscapeFadeTest : public AccessibilitySettingsTest {
protected:
    void SetUp() override {
        AccessibilitySettingsTest::SetUp();
        appProperties.getUserSettings()->clear();
        settings = std::make_unique<SettingsWindow>(deviceManager, appProperties, *aiService, *aiChat, shortcutManager,
                                                    themeManager, nullptr, nullptr, true);
        settings->setSize(SettingsWindow::kDefaultWidth, SettingsWindow::kDefaultHeight);
        dialog.setContentNonOwned(settings.get(), true);
        PopupMotion::attach(dialog);
        dialog.setVisible(true);
    }
    void TearDown() override {
        PopupMotion::setAnimateOffScreenForTest(false);
        dialog.clearContentComponent();
        settings.reset();
        AccessibilitySettingsTest::TearDown();
    }

    bool pumpUntil(const std::function<bool()>& done) {
        const auto deadline = juce::Time::getMillisecondCounter() + 2000;
        while (!done() && juce::Time::getMillisecondCounter() < deadline)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        return done();
    }

    std::unique_ptr<SettingsWindow> settings;
    RecordingDialog dialog;
};

} // namespace

TEST_F(SettingsWindowEscapeFadeTest, EscapeClosesAtOnceWhenTheWindowIsNotOnScreen) {
    EXPECT_TRUE(settings->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_EQ(dialog.closeCount, 1); // headless: final state is there immediately
}

TEST_F(SettingsWindowEscapeFadeTest, EscapeStartsAFadeAndClosesWhenItEnds) {
    PopupMotion::setAnimateOffScreenForTest(true);
    EXPECT_TRUE(settings->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_EQ(dialog.closeCount, 0); // not destroyed or hidden directly
    EXPECT_TRUE(PopupMotion::isDismissing(dialog));
    ASSERT_TRUE(pumpUntil([&] { return dialog.closeCount > 0; }));
    EXPECT_EQ(dialog.closeCount, 1);
}
