// SettingsWindowTabKeysTests.cpp -- the Settings window's tab keys: Cmd+1..9 (fixed, positional), the
// rebindable tabPrevious / tabNext actions, and the remembered tab (saved by name).
//
// Keys are delivered the way the native window delivers them: from the focused control up through its
// ancestors, each one's key listeners before its own keyPressed. A headless window cannot hold real
// keyboard focus, so the "focused" control is just where the walk starts.

#include "../Accessibility/AccessibilitySettingsFixture.h"
#include "UI/Layout/TabSwitchKeys.h"
#include "UI/Settings/SettingsWindow.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace {

constexpr int kCmd = juce::ModifierKeys::commandModifier;
constexpr int kAlt = juce::ModifierKeys::altModifier;

// What ComponentPeer::handleKeyPress does for a key press with `focused` holding focus.
bool deliver(SettingsWindow& window, juce::Component& focused, const juce::KeyPress& key) {
    for (auto* target = &focused; target != nullptr; target = target->getParentComponent()) {
        for (int i = 0; i < window.getNumTabs(); ++i)
            if (window.getArrowKeysForTest(i).isListeningOnForTest(target) &&
                window.getArrowKeysForTest(i).keyPressed(key, target))
                return true;
        auto& tabKeys = window.getTabSwitchKeysForTest();
        if (tabKeys.isAttachedForTest(target) && tabKeys.keyPressed(key, target))
            return true;
        if (target->keyPressed(key))
            return true;
    }
    return false;
}

juce::KeyPress cmdDigit(int digit) { return juce::KeyPress('0' + digit, juce::ModifierKeys(kCmd), '0' + digit); }
juce::KeyPress nextTab() { return juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys(kCmd | kAlt), 0); }
juce::KeyPress previousTab() { return juce::KeyPress(juce::KeyPress::leftKey, juce::ModifierKeys(kCmd | kAlt), 0); }

void collectDescendants(juce::Component& root, std::vector<juce::Component*>& out) {
    for (auto* child : root.getChildren()) {
        out.push_back(child);
        collectDescendants(*child, out);
    }
}

class SettingsWindowTabKeysTest : public AccessibilitySettingsTest {
protected:
    void SetUp() override {
        AccessibilitySettingsTest::SetUp();
        appProperties.getUserSettings()->clear();
    }

    void open(bool showAudioTab = true) {
        window = std::make_unique<SettingsWindow>(deviceManager, appProperties, *aiService, *aiChat, shortcutManager,
                                                  themeManager, nullptr, nullptr, showAudioTab);
        window->setSize(SettingsWindow::kDefaultWidth, SettingsWindow::kDefaultHeight);
        window->resized();
    }

    // Presses `key` with the open tab's own button focused.
    bool pressOnTabButton(const juce::KeyPress& key) { return deliver(*window, *window->getCurrentTabButton(), key); }

    // Every control of `type` in tab `index`, with that tab opened first.
    template <class T>
    std::vector<T*> controlsIn(int index) {
        window->getTabs().setCurrentTabIndex(index);
        std::vector<juce::Component*> all;
        collectDescendants(*window->getTabs().getTabContentComponent(index), all);
        std::vector<T*> out;
        for (auto* c : all)
            if (auto* typed = dynamic_cast<T*>(c))
                out.push_back(typed);
        return out;
    }

    std::unique_ptr<SettingsWindow> window;
};

} // namespace

TEST_F(SettingsWindowTabKeysTest, CommandDigitOpensThatTabByPosition) {
    open();
    ASSERT_EQ(window->getNumTabs(), 6);
    for (int n : {3, 6, 1, 5, 2, 4}) {
        EXPECT_TRUE(pressOnTabButton(cmdDigit(n)));
        EXPECT_EQ(window->getCurrentTabIndex(), n - 1) << "Cmd+" << n;
    }
}

TEST_F(SettingsWindowTabKeysTest, ADigitBeyondTheLastTabDoesNothing) {
    open();
    window->getTabs().setCurrentTabIndex(2);
    for (int n : {7, 8, 9}) {
        EXPECT_TRUE(pressOnTabButton(cmdDigit(n))) << "consumed, so a text field cannot type it";
        EXPECT_EQ(window->getCurrentTabIndex(), 2) << "Cmd+" << n;
    }
}

// The plugin build has no Audio tab, so every position shifts down by one.
TEST_F(SettingsWindowTabKeysTest, WithoutTheAudioTabTheDigitsStillCountFromTheFirstTab) {
    open(/*showAudioTab=*/false);
    ASSERT_EQ(window->getNumTabs(), 5);
    ASSERT_EQ(window->getTabName(0), "AI");
    EXPECT_TRUE(pressOnTabButton(cmdDigit(5)));
    EXPECT_EQ(window->getCurrentTabIndex(), 4);
    EXPECT_TRUE(pressOnTabButton(cmdDigit(6)));
    EXPECT_EQ(window->getCurrentTabIndex(), 4) << "there is no sixth tab";
    EXPECT_TRUE(pressOnTabButton(cmdDigit(1)));
    EXPECT_EQ(window->getTabName(window->getCurrentTabIndex()), "AI");
}

TEST_F(SettingsWindowTabKeysTest, TheDigitNeedsExactlyTheCommandModifier) {
    open();
    window->getTabs().setCurrentTabIndex(1);
    const juce::KeyPress others[] = {
        juce::KeyPress('3', juce::ModifierKeys(kCmd | juce::ModifierKeys::shiftModifier), '3'),
        juce::KeyPress('3', juce::ModifierKeys(kCmd | kAlt), '3'),
        juce::KeyPress('3', juce::ModifierKeys(kAlt), '3'),
        juce::KeyPress('3', juce::ModifierKeys(), '3'),
    };
    for (const auto& key : others) {
        EXPECT_FALSE(window->keyPressed(key));
        EXPECT_EQ(window->getCurrentTabIndex(), 1);
    }
}

TEST_F(SettingsWindowTabKeysTest, NextAndPreviousStepThroughTheTabsAndWrap) {
    open();
    window->getTabs().setCurrentTabIndex(0);
    EXPECT_TRUE(pressOnTabButton(previousTab()));
    EXPECT_EQ(window->getCurrentTabIndex(), 5) << "previous from the first tab wraps to the last";
    EXPECT_TRUE(pressOnTabButton(nextTab()));
    EXPECT_EQ(window->getCurrentTabIndex(), 0) << "next from the last tab wraps to the first";
    for (int expected = 1; expected < 6; ++expected) {
        EXPECT_TRUE(pressOnTabButton(nextTab()));
        EXPECT_EQ(window->getCurrentTabIndex(), expected);
    }
}

// A text field would otherwise type Cmd+digit and keep Cmd+Option+arrow for its caret, so the keys must
// get through from every text field of every tab, leaving its text alone.
TEST_F(SettingsWindowTabKeysTest, TheTabKeysWorkWhileATextFieldHasFocus) {
    open();
    int fieldsSeen = 0;
    for (int tab = 0; tab < window->getNumTabs(); ++tab) {
        for (auto* editor : controlsIn<juce::TextEditor>(tab)) {
            ++fieldsSeen;
            const auto before = editor->getText();
            window->getTabs().setCurrentTabIndex(tab);
            EXPECT_TRUE(deliver(*window, *editor, cmdDigit(2)));
            EXPECT_EQ(window->getCurrentTabIndex(), 1) << "Cmd+2 from a text field in tab " << tab;
            EXPECT_EQ(editor->getText(), before) << "the digit must not be typed";

            window->getTabs().setCurrentTabIndex(tab);
            EXPECT_TRUE(deliver(*window, *editor, nextTab()));
            EXPECT_EQ(window->getCurrentTabIndex(), (tab + 1) % window->getNumTabs());
            window->getTabs().setCurrentTabIndex(tab);
            EXPECT_TRUE(deliver(*window, *editor, previousTab()));
            EXPECT_EQ(window->getCurrentTabIndex(), (tab + window->getNumTabs() - 1) % window->getNumTabs());
        }
    }
    EXPECT_GE(fieldsSeen, 3) << "the AI host, the shortcuts search and the preferences filter, at least";
}

// The text fields are the only controls that need help; every other kind lets the keys bubble up.
TEST_F(SettingsWindowTabKeysTest, ComboBoxesSlidersTogglesAndButtonsLetTheTabKeysBubbleToTheWindow) {
    open();
    int controlsSeen = 0;
    for (int tab = 0; tab < window->getNumTabs(); ++tab) {
        std::vector<juce::Component*> controls;
        for (auto* c : controlsIn<juce::ComboBox>(tab))
            controls.push_back(c);
        for (auto* c : controlsIn<juce::Slider>(tab))
            controls.push_back(c);
        for (auto* c : controlsIn<juce::Button>(tab))
            controls.push_back(c);
        for (auto* control : controls) {
            ++controlsSeen;
            window->getTabs().setCurrentTabIndex(tab);
            EXPECT_TRUE(deliver(*window, *control, nextTab())) << "tab " << tab;
            EXPECT_EQ(window->getCurrentTabIndex(), (tab + 1) % window->getNumTabs()) << "tab " << tab;
            window->getTabs().setCurrentTabIndex(tab);
            EXPECT_TRUE(deliver(*window, *control, cmdDigit(1)));
            EXPECT_EQ(window->getCurrentTabIndex(), 0);
        }
    }
    EXPECT_GT(controlsSeen, 10);
}

TEST_F(SettingsWindowTabKeysTest, RebindingTheActionsMovesTheKeys) {
    open();
    shortcutManager.setBinding("tabNext", juce::KeyPress('j', juce::ModifierKeys(kCmd), 0));
    shortcutManager.setBinding("tabPrevious", juce::KeyPress('k', juce::ModifierKeys(kCmd), 0));
    window->getTabs().setCurrentTabIndex(2);

    EXPECT_FALSE(window->keyPressed(nextTab())) << "the old default no longer does anything";
    EXPECT_EQ(window->getCurrentTabIndex(), 2);
    EXPECT_TRUE(window->keyPressed(juce::KeyPress('j', juce::ModifierKeys(kCmd), 0)));
    EXPECT_EQ(window->getCurrentTabIndex(), 3);
    EXPECT_TRUE(window->keyPressed(juce::KeyPress('k', juce::ModifierKeys(kCmd), 0)));
    EXPECT_EQ(window->getCurrentTabIndex(), 2);

    // An unbound action leaves its key alone.
    shortcutManager.setBinding("tabNext", juce::KeyPress());
    EXPECT_FALSE(window->keyPressed(juce::KeyPress('j', juce::ModifierKeys(kCmd), 0)));
}

TEST_F(SettingsWindowTabKeysTest, EveryTabButtonsTooltipNamesItsCommandDigit) {
    open();
    for (int i = 0; i < window->getNumTabs(); ++i) {
        auto* button = window->getTabs().getTabbedButtonBar().getTabButton(i);
        ASSERT_NE(button, nullptr);
        EXPECT_EQ(button->getTooltip(), "Show the " + window->getTabName(i) + " settings (" + platformCommandKeyName() +
                                            "+" + juce::String(i + 1) + ")");
    }
}

TEST_F(SettingsWindowTabKeysTest, ThePositionalHelperAcceptsOnlyCommandPlusDigitOneToNine) {
    using synth::ui::TabSwitchKeys;
    for (int n = 1; n <= 9; ++n)
        EXPECT_EQ(TabSwitchKeys::positionalTabIndex(cmdDigit(n)), n - 1);
    EXPECT_EQ(TabSwitchKeys::positionalTabIndex(juce::KeyPress('0', juce::ModifierKeys(kCmd), '0')), -1);
    EXPECT_EQ(TabSwitchKeys::positionalTabIndex(juce::KeyPress('a', juce::ModifierKeys(kCmd), 'a')), -1);
    EXPECT_EQ(TabSwitchKeys::positionalTabIndex(juce::KeyPress('1', juce::ModifierKeys(), '1')), -1);
}

// ---- The remembered tab -----------------------------------------------------------------------

TEST_F(SettingsWindowTabKeysTest, TheOpenTabIsSavedByNameAndReopened) {
    open();
    window->getTabs().setCurrentTabIndex(4);
    ASSERT_EQ(window->getTabName(4), "Appearance");
    window.reset();
    EXPECT_EQ(appProperties.getUserSettings()->getValue("settingsTabName"), "Appearance");

    open();
    EXPECT_EQ(window->getTabName(window->getCurrentTabIndex()), "Appearance");
}

// The plugin has no Audio tab: the same saved name opens the same tab there, where the old index
// would have opened the one after it.
TEST_F(SettingsWindowTabKeysTest, ARememberedTabNameOpensTheSameTabWithOrWithoutTheAudioTab) {
    open(/*showAudioTab=*/true);
    window->getTabs().setCurrentTabIndex(3);
    ASSERT_EQ(window->getTabName(3), "Preferences");
    window.reset();

    open(/*showAudioTab=*/false);
    EXPECT_EQ(window->getTabName(window->getCurrentTabIndex()), "Preferences");
    EXPECT_EQ(window->getCurrentTabIndex(), 2);
}

TEST_F(SettingsWindowTabKeysTest, TheNameWinsOverTheOlderIndexKey) {
    appProperties.getUserSettings()->setValue("settingsTab", 1);
    appProperties.getUserSettings()->setValue("settingsTabName", "Feedback");
    open();
    EXPECT_EQ(window->getTabName(window->getCurrentTabIndex()), "Feedback");
}

TEST_F(SettingsWindowTabKeysTest, WithNoSavedNameTheOlderIndexKeyIsStillHonoured) {
    appProperties.getUserSettings()->setValue("settingsTab", 2);
    open();
    EXPECT_EQ(window->getCurrentTabIndex(), 2);
}

TEST_F(SettingsWindowTabKeysTest, ASavedNameThisBuildLacksOpensTheFirstTab) {
    appProperties.getUserSettings()->setValue("settingsTab", 3);
    appProperties.getUserSettings()->setValue("settingsTabName", "Audio");
    open(/*showAudioTab=*/false);
    EXPECT_EQ(window->getCurrentTabIndex(), 0);
}
