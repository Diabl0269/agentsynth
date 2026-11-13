// SettingsWindowTabStripTests.cpp -- the Settings tab strip as ONE keyboard stop: Left / Right / Home /
// End switch tab, Return / Space / Tab go into the tab, Shift+Tab comes back, the ring sits on the open
// tab's button. Keys go through the real handlers (the leaf's keyPressed, then each ancestor's, as the
// native window delivers them); a headless window cannot hold keyboard focus, so where a test needs
// "focus moved" it reads the strip's focus hook and the traverser's answer instead.
#include "../Accessibility/AccessibilitySettingsFixture.h"
#include "../Accessibility/TabOrderHelpers.h"
#include "UI/Settings/SettingsWindow.h"
#include <gtest/gtest.h>
#include <vector>

namespace {

constexpr int kCmd = juce::ModifierKeys::commandModifier;
using synth::test::walkTabOrder;

juce::KeyPress plain(int code) { return juce::KeyPress(code); }

// What ComponentPeer::handleKeyPress does with `focused` holding focus: it, then each ancestor.
bool deliver(juce::Component& focused, const juce::KeyPress& key) {
    for (auto* target = &focused; target != nullptr; target = target->getParentComponent())
        if (target->keyPressed(key))
            return true;
    return false;
}

class SettingsWindowTabStripTest : public AccessibilitySettingsTest {
protected:
    void SetUp() override {
        AccessibilitySettingsTest::SetUp();
        appProperties.getUserSettings()->clear();
        window = std::make_unique<SettingsWindow>(deviceManager, appProperties, *aiService, *aiChat, shortcutManager,
                                                  themeManager, nullptr);
        window->setSize(SettingsWindow::kDefaultWidth, SettingsWindow::kDefaultHeight);
        window->resized();
    }

    juce::Component& strip() { return window->getTabStripFocus(); }
    bool press(int code) { return deliver(strip(), plain(code)); }
    int current() const { return window->getCurrentTabIndex(); }

    juce::Image paintTabs() {
        auto& tabs = window->getSettingsTabsForTest();
        juce::Image img(juce::Image::ARGB, tabs.getWidth(), tabs.getHeight(), true, juce::SoftwareImageType());
        juce::Graphics g(img);
        tabs.paintEntireComponent(g, false);
        return img;
    }

    std::unique_ptr<SettingsWindow> window;
};

int countDifferingPixels(const juce::Image& a, const juce::Image& b, juce::Rectangle<int> area) {
    int differing = 0;
    for (int y = area.getY(); y < area.getBottom(); ++y)
        for (int x = area.getX(); x < area.getRight(); ++x)
            if (a.getPixelAt(x, y) != b.getPixelAt(x, y))
                ++differing;
    return differing;
}

} // namespace

TEST_F(SettingsWindowTabStripTest, TheStripIsOneNamedTabStopAndTheTabButtonsAreNot) {
    EXPECT_TRUE(strip().getWantsKeyboardFocus());
    EXPECT_EQ(strip().getTitle(), "Settings tabs");
    auto* tip = dynamic_cast<juce::TooltipClient*>(&strip());
    ASSERT_NE(tip, nullptr);
    EXPECT_FALSE(tip->getTooltip().isEmpty());
    bool onSelf = true, onChildren = true;
    strip().getInterceptsMouseClicks(onSelf, onChildren);
    EXPECT_FALSE(onSelf) << "mouse clicks reach the tab buttons, never this leaf";

    for (int i = 0; i < window->getNumTabs(); ++i)
        EXPECT_FALSE(window->getTabs().getTabbedButtonBar().getTabButton(i)->getWantsKeyboardFocus()) << i;

    const auto walk = walkTabOrder(*window);
    ASSERT_FALSE(walk.forward.empty());
    EXPECT_EQ(walk.forward.front(), &strip());
    EXPECT_EQ(std::count(walk.forward.begin(), walk.forward.end(), &strip()), 1);
    for (int i = 0; i < window->getNumTabs(); ++i)
        EXPECT_TRUE(std::find(walk.forward.begin(), walk.forward.end(),
                              window->getTabs().getTabbedButtonBar().getTabButton(i)) == walk.forward.end())
            << "tab button " << i << " is a Tab stop";
}

// A custom accessibility handler built on the base class reads only getHelpText(), so VoiceOver would
// never hear the strip's tooltip (which names its keys) without TooltipHelpHandler.
TEST_F(SettingsWindowTabStripTest, ScreenReaderHelpIsTheStripsTooltip) {
    auto* tip = dynamic_cast<juce::TooltipClient*>(&strip());
    ASSERT_NE(tip, nullptr);
    const auto handler = static_cast<SettingsTabs&>(window->getTabs()).createStripAccessibilityHandlerForTest();
    ASSERT_NE(handler, nullptr);
    EXPECT_EQ(handler->getHelp(), tip->getTooltip());
    EXPECT_FALSE(handler->getHelp().isEmpty());
}

TEST_F(SettingsWindowTabStripTest, TabLeavesTheStripForTheOpenTabsFirstControlAndShiftTabComesBack) {
    juce::KeyboardFocusTraverser traverser;
    for (int i = 0; i < window->getNumTabs(); ++i) {
        window->getTabs().setCurrentTabIndex(i);
        auto* content = window->getTabs().getTabContentComponent(i);
        const auto contentWalk = walkTabOrder(*content);
        ASSERT_FALSE(contentWalk.forward.empty()) << window->getTabName(i);
        auto* first = contentWalk.forward.front();

        EXPECT_EQ(traverser.getNextComponent(&strip()), first) << window->getTabName(i) << ": Tab from the strip";
        EXPECT_EQ(traverser.getPreviousComponent(first), &strip()) << window->getTabName(i) << ": Shift+Tab back";
    }
}

TEST_F(SettingsWindowTabStripTest, WholeWindowTabOrderIsTheStripThenTheOpenTab) {
    for (int i = 0; i < window->getNumTabs(); ++i) {
        window->getTabs().setCurrentTabIndex(i);
        const auto names = walkTabOrder(*window).names();
        ASSERT_GE(names.size(), 2) << window->getTabName(i);
        EXPECT_EQ(names[0], "Settings tabs") << window->getTabName(i);
        EXPECT_FALSE(names[1].startsWith("<unnamed")) << window->getTabName(i) << ": " << names[1];
    }
}

TEST_F(SettingsWindowTabStripTest, RightAndLeftOpenTheNeighbouringTabAtOnceAndStopAtTheEnds) {
    window->getTabs().setCurrentTabIndex(0);
    EXPECT_TRUE(press(juce::KeyPress::leftKey)) << "consumed with nowhere to go";
    EXPECT_EQ(current(), 0);
    for (int expected = 1; expected < window->getNumTabs(); ++expected) {
        EXPECT_TRUE(press(juce::KeyPress::rightKey));
        EXPECT_EQ(current(), expected);
    }
    EXPECT_TRUE(press(juce::KeyPress::rightKey));
    EXPECT_EQ(current(), window->getNumTabs() - 1) << "no wrap";
    EXPECT_TRUE(press(juce::KeyPress::leftKey));
    EXPECT_EQ(current(), window->getNumTabs() - 2);
}

TEST_F(SettingsWindowTabStripTest, HomeAndEndJumpToTheFirstAndLastTab) {
    window->getTabs().setCurrentTabIndex(2);
    EXPECT_TRUE(press(juce::KeyPress::endKey));
    EXPECT_EQ(current(), window->getNumTabs() - 1);
    EXPECT_TRUE(press(juce::KeyPress::homeKey));
    EXPECT_EQ(current(), 0);
}

TEST_F(SettingsWindowTabStripTest, ReturnAndSpaceMoveFocusIntoTheOpenTabsFirstControl) {
    juce::Component* target = nullptr;
    window->getSettingsTabsForTest().setContentFocusHookForTest([&target](juce::Component& c) { target = &c; });
    for (int i = 0; i < window->getNumTabs(); ++i) {
        for (int code : {(int)juce::KeyPress::returnKey, (int)juce::KeyPress::spaceKey}) {
            window->getTabs().setCurrentTabIndex(i);
            target = nullptr;
            EXPECT_TRUE(press(code)) << window->getTabName(i);
            ASSERT_NE(target, nullptr) << window->getTabName(i) << " key " << code;
            EXPECT_EQ(current(), i) << "entering a tab does not switch tab";
            EXPECT_EQ(target, walkTabOrder(*window->getTabs().getTabContentComponent(i)).forward.front());
        }
    }
}

TEST_F(SettingsWindowTabStripTest, OtherKeysAreLeftToTheWindow) {
    window->getTabs().setCurrentTabIndex(2);
    EXPECT_FALSE(strip().keyPressed(plain(juce::KeyPress::tabKey))) << "Tab uses normal traversal";
    EXPECT_FALSE(strip().keyPressed(juce::KeyPress(juce::KeyPress::tabKey, juce::ModifierKeys::shiftModifier, 0)));
    EXPECT_FALSE(strip().keyPressed(juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys(kCmd), 0)));
    EXPECT_FALSE(strip().keyPressed(plain(juce::KeyPress::downKey)));
    EXPECT_FALSE(strip().keyPressed(juce::KeyPress(juce::KeyPress::returnKey, juce::ModifierKeys::shiftModifier, 0)));
    EXPECT_EQ(current(), 2);

    // Cmd+digit still reaches the window from the strip, and Escape still closes it.
    EXPECT_TRUE(deliver(strip(), juce::KeyPress('5', juce::ModifierKeys(kCmd), '5')));
    EXPECT_EQ(current(), 4);
    bool closed = false;
    window->onRequestClose = [&closed] { closed = true; };
    EXPECT_TRUE(deliver(strip(), plain(juce::KeyPress::escapeKey)));
    EXPECT_TRUE(closed);
}

TEST_F(SettingsWindowTabStripTest, TheRingSitsOnTheOpenTabsButtonOnlyWhileTheStripIsFocused) {
    auto& tabs = window->getSettingsTabsForTest();
    auto& bar = tabs.getTabbedButtonBar();
    for (int tab : {0, 1}) {
        window->getTabs().setCurrentTabIndex(tab);
        tabs.setStripShownFocusedForTest(false);
        const auto unfocused = paintTabs();
        tabs.setStripShownFocusedForTest(true);
        const auto focused = paintTabs();
        tabs.setStripShownFocusedForTest(false);

        const auto button = bar.getTabButton(tab)->getBounds() + bar.getPosition();
        EXPECT_GT(countDifferingPixels(unfocused, focused, button), 0) << "ring on tab " << tab;
        const juce::Rectangle<int> belowStrip(0, button.getBottom() + 1, tabs.getWidth(),
                                              tabs.getHeight() - button.getBottom() - 1);
        EXPECT_EQ(countDifferingPixels(unfocused, focused, belowStrip), 0) << "nothing drawn below the strip";
    }
}
