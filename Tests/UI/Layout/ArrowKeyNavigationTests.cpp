// ArrowKeyNavigationTests.cpp -- list-style arrow keys in a Settings tab / dialog (ArrowKeyNavigation).
//
// A headless component cannot hold real keyboard focus, so two things are stood in for: which
// component "has focus" and where a focus move lands (the helper's test hooks). Everything else is
// the real thing: keys are delivered the way the native window delivers them (walking up from the
// focused control, each ancestor's key listeners before its own keyPressed), so JUCE's own
// ToggleButton, ComboBox, Slider and Viewport key handling runs unmodified.

#include "../Accessibility/AccessibilitySettingsFixture.h"
#include "../Accessibility/TabOrderHelpers.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Layout/ArrowKeyNavigation.h"
#include "UI/Settings/PreferencesSettingsTab/PreferencesSettingsTab.h"
#include "UI/Settings/SettingsWindow.h"
#include "UI/Settings/ShortcutsSettingsTab.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace {

using synth::ui::ArrowKeyNavigation;
using synth::ui::FoldableHeader;

// What ComponentPeer::handleKeyPress does for a key press with `focused` holding focus.
bool deliver(juce::Component& focused, const juce::KeyPress& key, const std::vector<ArrowKeyNavigation*>& navs) {
    for (auto* target = &focused; target != nullptr; target = target->getParentComponent()) {
        for (auto* nav : navs)
            if (nav->isListeningOnForTest(target) && nav->keyPressed(key, target))
                return true;
        if (target->keyPressed(key))
            return true;
    }
    return false;
}

// Tracks "which component has focus" for one navigation and delivers keys to it.
struct Keys {
    explicit Keys(ArrowKeyNavigation& navigation)
        : navs{&navigation} {
        hook(navigation);
    }
    explicit Keys(std::vector<ArrowKeyNavigation*> all)
        : navs(std::move(all)) {
        for (auto* n : navs)
            hook(*n);
    }
    void hook(ArrowKeyNavigation& n) {
        n.setFocusHooksForTest({[this] { return focus; }, [this](juce::Component& c) { focus = &c; }});
    }
    bool press(int keyCode, juce::ModifierKeys mods = {}) {
        return focus != nullptr && deliver(*focus, juce::KeyPress(keyCode, mods, 0), navs);
    }
    bool down() { return press(juce::KeyPress::downKey); }
    bool up() { return press(juce::KeyPress::upKey); }
    bool left() { return press(juce::KeyPress::leftKey); }
    bool right() { return press(juce::KeyPress::rightKey); }

    juce::Component* focus = nullptr;
    std::vector<ArrowKeyNavigation*> navs;
};

// A column of four check boxes in a scope component.
struct ToggleColumn {
    ToggleColumn() {
        scope.setSize(240, 200);
        for (int i = 0; i < 4; ++i) {
            toggles[i].setButtonText("Toggle " + juce::String(i + 1));
            toggles[i].setBounds(0, i * 30, 240, 26);
            scope.addAndMakeVisible(toggles[i]);
        }
    }
    juce::Component scope;
    juce::ToggleButton toggles[4];
};

} // namespace

TEST(ArrowKeyNavigationTest, DownAndUpWalkTheControlsInOrder) {
    ToggleColumn col;
    ArrowKeyNavigation nav(col.scope);
    Keys keys(nav);
    keys.focus = &col.toggles[0];

    EXPECT_TRUE(keys.down());
    EXPECT_EQ(keys.focus, &col.toggles[1]);
    EXPECT_TRUE(keys.down());
    EXPECT_EQ(keys.focus, &col.toggles[2]);
    EXPECT_TRUE(keys.up());
    EXPECT_EQ(keys.focus, &col.toggles[1]);
}

TEST(ArrowKeyNavigationTest, ClampsAtBothEndsAndConsumesTheKey) {
    ToggleColumn col;
    ArrowKeyNavigation nav(col.scope);
    Keys keys(nav);

    keys.focus = &col.toggles[3];
    EXPECT_TRUE(keys.down()) << "the key is consumed so it cannot scroll or wrap";
    EXPECT_EQ(keys.focus, &col.toggles[3]);

    keys.focus = &col.toggles[0];
    EXPECT_TRUE(keys.up());
    EXPECT_EQ(keys.focus, &col.toggles[0]);
}

TEST(ArrowKeyNavigationTest, SkipsHiddenDisabledAndBoundsLessControls) {
    ToggleColumn col;
    col.toggles[1].setVisible(false);
    col.toggles[2].setEnabled(false);
    ArrowKeyNavigation nav(col.scope);
    Keys keys(nav);
    keys.focus = &col.toggles[0];

    EXPECT_TRUE(keys.down());
    EXPECT_EQ(keys.focus, &col.toggles[3]) << "hidden and disabled controls are not stops";

    col.toggles[1].setVisible(true);
    col.toggles[1].setBounds(0, 0, 0, 0); // visible but with no area: a Tab stop nobody can see
    keys.focus = &col.toggles[0];
    EXPECT_TRUE(keys.down());
    EXPECT_EQ(keys.focus, &col.toggles[3]);
}

TEST(ArrowKeyNavigationTest, OtherKeysAndModifiedArrowsPassThrough) {
    ToggleColumn col;
    ArrowKeyNavigation nav(col.scope);
    Keys keys(nav);
    keys.focus = &col.toggles[0];

    EXPECT_FALSE(keys.press('a'));
    EXPECT_FALSE(keys.press(juce::KeyPress::homeKey));
    EXPECT_FALSE(keys.press(juce::KeyPress::downKey, juce::ModifierKeys::commandModifier));
    EXPECT_FALSE(keys.press(juce::KeyPress::rightKey, juce::ModifierKeys::shiftModifier));
    EXPECT_EQ(keys.focus, &col.toggles[0]);
    EXPECT_FALSE(col.toggles[0].getToggleState());
}

// A juce::Viewport consumes Up/Down for itself when a key bubbles through it and its scrollbar is
// showing, so a key from a control inside one never reaches a listener on the outer scope. The
// helper listens on the viewport too, ahead of the viewport's own keyPressed.
TEST(ArrowKeyNavigationTest, WorksThroughAScrollingViewport) {
    juce::Component scope;
    scope.setSize(240, 80);
    juce::Viewport viewport;
    viewport.setBounds(scope.getLocalBounds());
    scope.addAndMakeVisible(viewport);
    juce::Component content;
    content.setSize(240, 200);
    juce::ToggleButton toggles[4];
    for (int i = 0; i < 4; ++i) {
        toggles[i].setBounds(0, i * 50, 240, 26);
        content.addAndMakeVisible(toggles[i]);
    }
    viewport.setViewedComponent(&content, false);
    viewport.setScrollBarsShown(true, false);
    ASSERT_TRUE(viewport.getVerticalScrollBar().isVisible()) << "the viewport scrolls, so it would take Up/Down";

    // Control: a helper that only listens on the scope never sees the key.
    {
        ArrowKeyNavigation scopeOnly(scope);
        Keys keys(scopeOnly);
        keys.focus = &toggles[0];
        keys.down();
        EXPECT_EQ(keys.focus, &toggles[0]) << "the viewport swallowed the key before the scope's listener";
    }
    viewport.setViewPosition(0, 0);

    ArrowKeyNavigation nav(scope);
    nav.watchViewportsInScope();
    ASSERT_TRUE(nav.isListeningOnForTest(&viewport));
    Keys keys(nav);
    keys.focus = &toggles[0];
    EXPECT_TRUE(keys.down());
    EXPECT_EQ(keys.focus, &toggles[1]);
    EXPECT_TRUE(keys.down());
    EXPECT_EQ(keys.focus, &toggles[2]);
    EXPECT_EQ(viewport.getViewPositionY(), 0) << "moving focus is the helper's job; scrolling to it is the owner's";
}

// A native control that owns the arrows keeps them: it consumes the key before it bubbles. Down onto
// a combo box (focus moves), then Down again changes the combo; Tab is what moves on.
TEST(ArrowKeyNavigationTest, ComboBoxKeepsUpAndDownOnceItHasFocus) {
    juce::Component scope;
    scope.setSize(240, 200);
    juce::ToggleButton before("Before");
    juce::ComboBox combo;
    juce::ToggleButton after("After");
    before.setBounds(0, 0, 240, 26);
    combo.setBounds(0, 40, 240, 26);
    after.setBounds(0, 80, 240, 26);
    combo.addItem("One", 1);
    combo.addItem("Two", 2);
    combo.addItem("Three", 3);
    combo.setSelectedId(1, juce::dontSendNotification);
    scope.addAndMakeVisible(before);
    scope.addAndMakeVisible(combo);
    scope.addAndMakeVisible(after);

    ArrowKeyNavigation nav(scope);
    Keys keys(nav);
    keys.focus = &before;

    EXPECT_TRUE(keys.down());
    ASSERT_EQ(keys.focus, &combo) << "Down from the check box lands on the combo box";
    EXPECT_EQ(combo.getSelectedId(), 1);

    EXPECT_TRUE(keys.down());
    EXPECT_EQ(keys.focus, &combo) << "the combo box consumed the key";
    EXPECT_EQ(combo.getSelectedId(), 2) << "...and used it to change its selection";
}

TEST(ArrowKeyNavigationTest, RightTicksAndLeftUnticksThroughTheClickPath) {
    ToggleColumn col;
    ArrowKeyNavigation nav(col.scope);
    Keys keys(nav);
    int clicks = 0;
    col.toggles[1].onClick = [&] { ++clicks; };
    keys.focus = &col.toggles[1];

    EXPECT_TRUE(keys.right());
    EXPECT_TRUE(col.toggles[1].getToggleState());
    EXPECT_EQ(clicks, 1) << "the same callback a mouse click runs, so the setting is saved";

    EXPECT_TRUE(keys.right());
    EXPECT_TRUE(col.toggles[1].getToggleState()) << "Right on a ticked box does nothing";
    EXPECT_EQ(clicks, 1);

    EXPECT_TRUE(keys.left());
    EXPECT_FALSE(col.toggles[1].getToggleState());
    EXPECT_EQ(clicks, 2);

    EXPECT_TRUE(keys.left());
    EXPECT_FALSE(col.toggles[1].getToggleState()) << "Left on an unticked box does nothing";
    EXPECT_EQ(clicks, 2);
}

TEST(ArrowKeyNavigationTest, LeftDoesNothingOnARadioButtonButRightSelectsIt) {
    ToggleColumn col;
    col.toggles[0].setRadioGroupId(7);
    col.toggles[1].setRadioGroupId(7);
    col.toggles[0].setToggleState(true, juce::dontSendNotification);
    ArrowKeyNavigation nav(col.scope);
    Keys keys(nav);

    keys.focus = &col.toggles[0];
    EXPECT_FALSE(keys.left()) << "a radio button cannot be switched off";
    EXPECT_TRUE(col.toggles[0].getToggleState());

    keys.focus = &col.toggles[1];
    EXPECT_TRUE(keys.right());
    EXPECT_TRUE(col.toggles[1].getToggleState());
    EXPECT_FALSE(col.toggles[0].getToggleState()) << "the group's other button switched off";
}

namespace {
struct TestHeader
    : juce::Button
    , FoldableHeader {
    TestHeader()
        : juce::Button("Header") {}
    void paintButton(juce::Graphics&, bool, bool) override {}
    bool isFolded() const override { return folded; }
    void setFolded(bool f) override { folded = f; }
    bool folded = false;
};
} // namespace

TEST(ArrowKeyNavigationTest, LeftFoldsAndRightUnfoldsAHeader) {
    juce::Component scope;
    scope.setSize(240, 100);
    TestHeader header;
    header.setBounds(0, 0, 240, 26);
    scope.addAndMakeVisible(header);
    ArrowKeyNavigation nav(scope);
    Keys keys(nav);
    keys.focus = &header;

    EXPECT_TRUE(keys.left());
    EXPECT_TRUE(header.folded);
    EXPECT_TRUE(keys.left());
    EXPECT_TRUE(header.folded) << "idempotent";
    EXPECT_TRUE(keys.right());
    EXPECT_FALSE(header.folded);
}

// ---------------------------------------------------------------------------------------------
// The Settings window and its tabs.
// ---------------------------------------------------------------------------------------------

namespace {
std::vector<ArrowKeyNavigation*> allNavigations(SettingsWindow& window) {
    std::vector<ArrowKeyNavigation*> out;
    for (int i = 0; i < window.getNumTabs(); ++i)
        out.push_back(&window.getArrowKeysForTest(i));
    return out;
}

juce::ToggleButton* findToggle(juce::Component& root, const juce::String& text) {
    for (auto* child : root.getChildren()) {
        if (auto* toggle = dynamic_cast<juce::ToggleButton*>(child);
            toggle != nullptr && toggle->getButtonText() == text)
            return toggle;
        if (auto* nested = findToggle(*child, text))
            return nested;
    }
    return nullptr;
}
} // namespace

class SettingsArrowKeysTest : public AccessibilitySettingsTest {
protected:
    void openWindow() {
        window = std::make_unique<SettingsWindow>(deviceManager, appProperties, *aiService, *aiChat, shortcutManager,
                                                  themeManager, nullptr);
        window->setSize(SettingsWindow::kDefaultWidth, SettingsWindow::kDefaultHeight);
        window->resized();
        prefs = dynamic_cast<PreferencesSettingsTab*>(window->getTabs().getTabContentComponent(3));
    }

    std::unique_ptr<SettingsWindow> window;
    PreferencesSettingsTab* prefs = nullptr;
};

TEST_F(SettingsArrowKeysTest, EveryTabHasItsOwnNavigationAndThePreferencesViewportIsWatched) {
    openWindow();
    ASSERT_NE(prefs, nullptr);
    for (int i = 0; i < window->getNumTabs(); ++i)
        EXPECT_TRUE(window->getArrowKeysForTest(i).isListeningOnForTest(window->getTabs().getTabContentComponent(i)))
            << window->getTabName(i).toStdString();
    EXPECT_TRUE(window->getArrowKeysForTest(3).isListeningOnForTest(&prefs->getContentViewportForTest()));
}

TEST_F(SettingsArrowKeysTest, DownFromTheFirstGraphToggleLandsOnTheNextControlInTabOrder) {
    openWindow();
    ASSERT_NE(prefs, nullptr);
    prefs->setSelectedCategory(PreferencesSettingsTab::Category::Graph);
    auto* first = findToggle(*prefs, "Double-click port to disconnect");
    ASSERT_NE(first, nullptr);
    ASSERT_TRUE(first->isVisible());

    const auto walk = synth::test::walkTabOrder(*prefs);
    const auto it = std::find(walk.forward.begin(), walk.forward.end(), first);
    ASSERT_NE(it, walk.forward.end());
    ASSERT_NE(it + 1, walk.forward.end());

    Keys keys(allNavigations(*window));
    keys.focus = first;
    EXPECT_TRUE(keys.down());
    EXPECT_NE(keys.focus, first);
    EXPECT_EQ(keys.focus, *(it + 1)) << "the next stop Tab would reach";

    EXPECT_TRUE(keys.up());
    EXPECT_EQ(keys.focus, first);
}

TEST_F(SettingsArrowKeysTest, RightTicksAPreferenceAndSavesIt) {
    openWindow();
    ASSERT_NE(prefs, nullptr);
    prefs->setDoubleClickPortDisconnectEnabled(false);
    auto* toggle = findToggle(*prefs, "Double-click port to disconnect");
    ASSERT_NE(toggle, nullptr);
    ASSERT_FALSE(toggle->getToggleState());

    Keys keys(allNavigations(*window));
    keys.focus = toggle;
    EXPECT_TRUE(keys.right());
    EXPECT_TRUE(prefs->isDoubleClickPortDisconnectEnabled()) << "the click callback persisted it";
    EXPECT_TRUE(keys.left());
    EXPECT_FALSE(prefs->isDoubleClickPortDisconnectEnabled());
}

TEST_F(SettingsArrowKeysTest, LeftFoldsAndRightUnfoldsAPreferencesSectionAndDownSkipsFoldedRows) {
    openWindow();
    ASSERT_NE(prefs, nullptr);
    prefs->setSelectedCategory(PreferencesSettingsTab::Category::All);
    using Category = PreferencesSettingsTab::Category;
    auto& graphHeader = prefs->getSectionHeaderForTest(Category::Graph);
    auto& timelineHeader = prefs->getSectionHeaderForTest(Category::Timeline);
    ASSERT_TRUE(graphHeader.isVisible());

    Keys keys(allNavigations(*window));
    keys.focus = &graphHeader;

    // Unfolded: Down from the header goes into the section's own rows.
    EXPECT_TRUE(keys.down());
    EXPECT_NE(keys.focus, &timelineHeader);
    EXPECT_NE(keys.focus, &graphHeader);
    keys.focus = &graphHeader;

    EXPECT_TRUE(keys.left());
    EXPECT_TRUE(prefs->isSectionCollapsed(Category::Graph));
    EXPECT_TRUE(keys.left());
    EXPECT_TRUE(prefs->isSectionCollapsed(Category::Graph)) << "idempotent";

    EXPECT_TRUE(keys.down());
    EXPECT_EQ(keys.focus, &timelineHeader) << "the folded section's rows are skipped";

    keys.focus = &graphHeader;
    EXPECT_TRUE(keys.right());
    EXPECT_FALSE(prefs->isSectionCollapsed(Category::Graph));
}

// While a row listens for its new key, the rebind button takes EVERY key, arrows included: they become
// the binding. The key never bubbles to the helper, so nothing navigates.
TEST(ShortcutsArrowKeysTest, ArrowsAreCapturedAsTheNewBindingWhileListening) {
    ShortcutManager manager;
    ShortcutsSettingsTab tab(manager);
    tab.setSize(600, 500);
    ArrowKeyNavigation nav(tab);
    nav.watchViewportsInScope();
    Keys keys(nav);

    // The rebind button of row 0 and the one after it, found by what they show.
    std::vector<juce::TextButton*> bindButtons;
    std::vector<juce::Component*> stack{&tab};
    while (!stack.empty()) {
        auto* c = stack.back();
        stack.pop_back();
        if (auto* b = dynamic_cast<juce::TextButton*>(c);
            b != nullptr && b->getTooltip() == "Click, then press a key to rebind")
            bindButtons.push_back(b);
        for (auto* child : c->getChildren())
            stack.push_back(child);
    }
    ASSERT_GE(bindButtons.size(), 2u);
    auto* first = bindButtons.front();

    tab.startListeningForTest(0);
    ASSERT_EQ(tab.getRowBindingText(0), "Press a key...");
    keys.focus = first;

    EXPECT_TRUE(keys.down());
    EXPECT_EQ(keys.focus, first) << "focus did not move";
    const auto actionId = manager.getActionIds()[0];
    EXPECT_EQ(manager.getBinding(actionId).getKeyCode(), juce::KeyPress::downKey) << "Down became the binding";
    EXPECT_NE(tab.getRowBindingText(0), "Press a key...") << "listening ended";

    // Not listening any more: the same key navigates.
    EXPECT_TRUE(keys.down());
    EXPECT_NE(keys.focus, first);
}

TEST(ShortcutsArrowKeysTest, LeftFoldsAndRightUnfoldsASectionHeader) {
    ShortcutManager manager;
    ShortcutsSettingsTab tab(manager);
    tab.setSize(600, 500);
    ArrowKeyNavigation nav(tab);
    nav.watchViewportsInScope();
    Keys keys(nav);

    FoldableHeader* header = nullptr;
    juce::Component* headerComponent = nullptr;
    std::vector<juce::Component*> stack{&tab};
    while (!stack.empty() && header == nullptr) {
        auto* c = stack.back();
        stack.pop_back();
        if (auto* h = dynamic_cast<FoldableHeader*>(c)) {
            header = h;
            headerComponent = c;
        }
        for (auto* child : c->getChildren())
            stack.push_back(child);
    }
    ASSERT_NE(header, nullptr);
    keys.focus = headerComponent;
    ASSERT_FALSE(header->isFolded());

    EXPECT_TRUE(keys.left());
    EXPECT_TRUE(header->isFolded());
    EXPECT_TRUE(keys.left());
    EXPECT_TRUE(header->isFolded());
    EXPECT_TRUE(keys.right());
    EXPECT_FALSE(header->isFolded());
}
