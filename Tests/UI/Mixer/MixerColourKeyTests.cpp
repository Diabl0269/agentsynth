// MixerColourKeyTests.cpp -- the colour key (C by default, rebindable) opens the focused strip's colour picker on its
// dot, Return on the dot opens it once, and Space on the dot is left to the window's play command. Keys travel the real
// ComponentPeer dispatch on a native window holding the real panel of an off-screen MainComponent.
#include "../../TestWait.h"
#include "MixerNativeHost.h"
#include "MixerZonesTestRig.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Chrome/ColourPickerPopup.h"
#include <gtest/gtest.h>

namespace {

using namespace synth::test::mixer;
using ColourRoute = synth::ui::MixerPanelComponent::ColourRoute;

juce::KeyPress letter(char c) { return juce::KeyPress(c, juce::ModifierKeys::noModifiers, 0); }

// Counts the pickers the panel opens and where they are anchored, instead of showing a CallOutBox.
struct PickerCounter {
    int opened = 0;
    juce::Rectangle<int> anchor;
    void hook(synth::ui::MixerPanelComponent& panel) {
        panel.setShowColourPickerHookForTest(
            [this](ColourRoute, std::unique_ptr<synth::ui::ColourPickerPopup>, auto bounds) {
                ++opened;
                anchor = bounds;
            });
    }
};

// The window's own commands: counts the keys that reach it, as the app's Space (play/stop) would see them.
struct WindowKeys {
    int spaces = 0;
    int others = 0;
    void hook(NativeHost& host) {
        host.onUnhandledKey = [this](const juce::KeyPress& key) {
            (key == juce::KeyPress(juce::KeyPress::spaceKey) ? spaces : others)++;
            return true;
        };
    }
};

bool panelHasKeyboard(NativeHost& host) {
    host.panel.grabKeyboardFocus();
    return host.panel.hasKeyboardFocus(false);
}

} // namespace

// Regression test for FRO1206: no key opened a strip's colour picker; the dot was only reachable by mouse or Tab.
TEST(MixerColourKeyTest, COpensTheFocusedStripsPickerOnItsDot) {
    if (nativeWindowsUnavailable())
        GTEST_SKIP() << "needs a display for a native window";
    MixerZonesRig r(2);
    NativeHost host(*r.panel);
    if (!panelHasKeyboard(host))
        GTEST_SKIP() << "this machine would not give a temporary window the keyboard";
    PickerCounter picker;
    picker.hook(*r.panel);
    WindowKeys window;
    window.hook(host);

    ASSERT_TRUE(pressThroughPeer(host, juce::KeyPress(juce::KeyPress::rightKey)));
    ASSERT_TRUE(pressThroughPeer(host, juce::KeyPress(juce::KeyPress::rightKey)));
    ASSERT_EQ(r.panel->getFocusedColumnIndexForTest(), 1);
    EXPECT_TRUE(pressThroughPeer(host, letter('c')));
    EXPECT_EQ(picker.opened, 1);
    EXPECT_EQ(window.others, 0) << "the panel consumed the key";
    const auto& dot = r.panel->getStripColumnForTest(1)->getHeaderForTest().getColourDotForTest();
    EXPECT_EQ(picker.anchor, dot.getScreenBounds()) << "the picker opens on the focused strip's own dot";
}

TEST(MixerColourKeyTest, CIsLeftToBubbleOnAColumnWithNothingToRecolour) {
    if (nativeWindowsUnavailable())
        GTEST_SKIP() << "needs a display for a native window";
    MixerZonesRig r(1);
    NativeHost host(*r.panel);
    if (!panelHasKeyboard(host))
        GTEST_SKIP() << "this machine would not give a temporary window the keyboard";
    PickerCounter picker;
    picker.hook(*r.panel);
    WindowKeys window;
    window.hook(host);
    for (int i = 0; i < r.panel->getColumnCount(); ++i)
        pressThroughPeer(host, juce::KeyPress(juce::KeyPress::rightKey));
    ASSERT_EQ(r.panel->getFocusedColumnIndexForTest(), r.panel->getColumnCount() - 1) << "Master, the last column";
    pressThroughPeer(host, letter('c'));
    EXPECT_EQ(picker.opened, 0);
    EXPECT_EQ(window.others, 1) << "Master has no colour, so the key goes on to the window";
}

TEST(MixerColourKeyTest, TheKeyIsRebindableAndTheDotsTooltipNamesIt) {
    if (nativeWindowsUnavailable())
        GTEST_SKIP() << "needs a display for a native window";
    MixerZonesRig r(1);
    auto& shortcuts = r.mc.getShortcutManager();
    EXPECT_EQ(ShortcutManager::getCategory("mixerOpenColour"), ShortcutCategory::Mixer);
    EXPECT_EQ(shortcuts.getBinding("mixerOpenColour"), letter('c'));
    auto& dot = r.panel->getStripColumnForTest(0)->getHeaderForTest().getColourDotForTest();
    EXPECT_EQ(dot.getTooltip(), "Change this channel's colour  (C)");

    shortcuts.setBinding("mixerOpenColour", letter('k'));
    // The Settings tab saves after a rebind, which notifies onBindingsChanged; saving here would write the user's file.
    ASSERT_TRUE(shortcuts.onBindingsChanged);
    shortcuts.onBindingsChanged();
    EXPECT_EQ(dot.getTooltip(), "Change this channel's colour  (K)") << "the tooltip follows a rebind";

    NativeHost host(*r.panel);
    if (!panelHasKeyboard(host))
        GTEST_SKIP() << "this machine would not give a temporary window the keyboard";
    PickerCounter picker;
    picker.hook(*r.panel);
    WindowKeys window;
    window.hook(host);
    pressThroughPeer(host, juce::KeyPress(juce::KeyPress::rightKey));
    ASSERT_EQ(r.panel->getFocusedColumnIndexForTest(), 0);
    pressThroughPeer(host, letter('c'));
    EXPECT_EQ(picker.opened, 0) << "the old key no longer opens the picker";
    pressThroughPeer(host, letter('k'));
    EXPECT_EQ(picker.opened, 1);
}

// Regression test for FRO1206: Space on a focused colour dot opened the picker instead of playing, and a click on a
// strip often leaves the keyboard on the dot.
TEST(MixerColourKeyTest, OnTheDotReturnOpensThePickerOnceAndSpaceGoesOnToPlay) {
    if (nativeWindowsUnavailable())
        GTEST_SKIP() << "needs a display for a native window";
    MixerZonesRig r(1);
    NativeHost host(*r.panel);
    PickerCounter picker;
    picker.hook(*r.panel);
    WindowKeys window;
    window.hook(host);
    auto& dot = r.panel->getStripColumnForTest(0)->getHeaderForTest().getColourDotForTest();
    dot.grabKeyboardFocus();
    if (!dot.hasKeyboardFocus(false))
        GTEST_SKIP() << "this machine would not give a temporary window the keyboard";

    pressThroughPeer(host, juce::KeyPress(juce::KeyPress::spaceKey));
    synth::test::flushMessageQueue(); // a click is posted as a command message
    EXPECT_EQ(picker.opened, 0) << "Space on the dot opened the picker";
    EXPECT_EQ(window.spaces, 1) << "Space reaches the window's play command";

    EXPECT_TRUE(pressThroughPeer(host, juce::KeyPress(juce::KeyPress::returnKey)));
    synth::test::flushMessageQueue();
    EXPECT_EQ(picker.opened, 1) << "Return on the dot opens the picker exactly once";
}
