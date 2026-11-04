// MixerPanHideKeyTests.cpp -- the focused mixer column's pan keys (Shift+Left / Shift+Right by default) and hide key
// (H), both rebindable: the pan moves one step per press as one undo step, H hides the column and hands the focus to
// its neighbour, and H in the side pane's list shows the channel again. Column keys travel the real ComponentPeer
// dispatch on a native window holding the real panel of an off-screen MainComponent.
#include "../../TestWait.h"
#include "MixerNativeHost.h"
#include "MixerZonesTestRig.h"
#include "ShortcutManager/ShortcutManager.h"
#include <gtest/gtest.h>

namespace {

using namespace synth::test::mixer;

juce::KeyPress right() { return juce::KeyPress(juce::KeyPress::rightKey); }
juce::KeyPress shiftArrow(int code) { return juce::KeyPress(code, juce::ModifierKeys::shiftModifier, 0); }
juce::KeyPress letter(char c) { return juce::KeyPress(c, juce::ModifierKeys::noModifiers, 0); }

double panOf(synth::ui::MixerPanelComponent& panel, int strip) {
    auto* column = panel.getStripColumnForTest(strip);
    EXPECT_NE(column, nullptr) << "no strip " << strip << " of " << panel.getColumnCount() << " columns";
    return column != nullptr ? column->getPanSliderForTest().getValue() : -99.0;
}

// The panel on a native window holding the keyboard, keys going to whatever the window has focused.
struct KeyRig {
    MixerZonesRig r;
    NativeHost host;
    int unhandled = 0;
    explicit KeyRig(int tracks)
        : r(tracks)
        , host(*r.panel) {
        host.onUnhandledKey = [this](const juce::KeyPress&) {
            ++unhandled;
            return true;
        };
        r.panel->grabKeyboardFocus();
    }
    bool hasKeyboard() const { return r.panel->hasKeyboardFocus(true); }
    bool press(const juce::KeyPress& key) { return pressThroughPeer(host, key); }
};

#define SKIP_WITHOUT_A_KEYBOARD_WINDOW(rig)                                                                            \
    if (!(rig).hasKeyboard())                                                                                          \
    GTEST_SKIP() << "this machine would not give a temporary window the keyboard"

} // namespace

// Regression test for FRO1310: a strip's pan had no key, so a keyboard user could not reach it.
TEST(MixerPanHideKeyTest, ShiftArrowsPanTheFocusedStripOneStepPerPressAsOneUndoStepEach) {
    if (nativeWindowsUnavailable())
        GTEST_SKIP() << "needs a display for a native window";
    KeyRig k(2);
    SKIP_WITHOUT_A_KEYBOARD_WINDOW(k);
    auto& panel = *k.r.panel;
    ASSERT_TRUE(k.press(right()));
    ASSERT_TRUE(k.press(right()));
    ASSERT_EQ(panel.getFocusedColumnIndexForTest(), 1);
    ASSERT_NEAR(panOf(panel, 1), 0.0, 1.0e-6);

    EXPECT_TRUE(k.press(shiftArrow(juce::KeyPress::rightKey)));
    EXPECT_NEAR(panOf(panel, 1), 0.1, 1.0e-4);
    EXPECT_EQ(panel.getFocusedColumnIndexForTest(), 1) << "Shift+Right pans; it does not walk to the next column";
    EXPECT_TRUE(k.press(shiftArrow(juce::KeyPress::leftKey)));
    EXPECT_TRUE(k.press(shiftArrow(juce::KeyPress::leftKey)));
    EXPECT_NEAR(panOf(panel, 1), -0.1, 1.0e-4);
    EXPECT_NEAR(panOf(panel, 0), 0.0, 1.0e-6) << "only the focused strip moves";
    EXPECT_EQ(k.unhandled, 0);

    ASSERT_TRUE(k.r.mc.getUndoManager().undo());
    EXPECT_NEAR(panOf(panel, 1), 0.0, 1.0e-4) << "one undo takes back one press";
}

TEST(MixerPanHideKeyTest, ThePanKeysStopAtTheEndsAndBubbleOnMaster) {
    if (nativeWindowsUnavailable())
        GTEST_SKIP() << "needs a display for a native window";
    KeyRig k(1);
    SKIP_WITHOUT_A_KEYBOARD_WINDOW(k);
    auto& panel = *k.r.panel;
    k.press(right());
    for (int i = 0; i < 15; ++i)
        k.press(shiftArrow(juce::KeyPress::leftKey));
    EXPECT_NEAR(panOf(panel, 0), -1.0, 1.0e-4) << "hard left, clamped";

    for (int i = 1; i < panel.getColumnCount(); ++i)
        k.press(right());
    ASSERT_EQ(panel.getFocusedColumnIndexForTest(), panel.getColumnCount() - 1) << "Master, the last column";
    k.press(shiftArrow(juce::KeyPress::rightKey));
    EXPECT_EQ(k.unhandled, 1) << "Master's pan is its pan law, so the key goes on";
    EXPECT_EQ(panel.getFocusedColumnIndexForTest(), panel.getColumnCount() - 1);
}

// Regression test for FRO1310: hiding a channel needed the mouse on the side pane's eye.
TEST(MixerPanHideKeyTest, HHidesTheFocusedColumnAndTheFocusMovesToItsNeighbour) {
    if (nativeWindowsUnavailable())
        GTEST_SKIP() << "needs a display for a native window";
    KeyRig k(3);
    SKIP_WITHOUT_A_KEYBOARD_WINDOW(k);
    auto& panel = *k.r.panel;
    const int before = panel.getColumnCount();
    k.press(right());
    k.press(right());
    ASSERT_EQ(panel.getFocusedColumnIndexForTest(), 1);
    const auto hiddenId = k.r.stripId(1);
    const auto neighbour = panel.getStripColumnForTest(2)->getUuid();

    EXPECT_TRUE(k.press(letter('h')));
    synth::test::flushMessageQueue();
    EXPECT_TRUE(panel.getViewDoc().isHidden(hiddenId));
    EXPECT_EQ(panel.getColumnCount(), before - 1);
    ASSERT_EQ(panel.getFocusedColumnIndexForTest(), 1);
    EXPECT_EQ(panel.getStripColumnForTest(1)->getUuid(), neighbour) << "the right-hand neighbour takes the focus";

    ASSERT_TRUE(k.r.mc.getUndoManager().undo());
    EXPECT_FALSE(panel.getViewDoc().isHidden(hiddenId)) << "hiding is one undo step";
}

TEST(MixerPanHideKeyTest, HIsLeftToBubbleOnMaster) {
    if (nativeWindowsUnavailable())
        GTEST_SKIP() << "needs a display for a native window";
    KeyRig k(1);
    SKIP_WITHOUT_A_KEYBOARD_WINDOW(k);
    auto& panel = *k.r.panel;
    for (int i = 0; i < panel.getColumnCount(); ++i)
        k.press(right());
    ASSERT_EQ(panel.getFocusedColumnIndexForTest(), panel.getColumnCount() - 1);
    k.press(letter('h'));
    EXPECT_EQ(k.unhandled, 1);
    EXPECT_FALSE(panel.getViewDoc().isHidden(synth::MixerViewDoc::kMasterId));
}

TEST(MixerPanHideKeyTest, HInTheSidePaneListShowsAndHidesTheCursorChannel) {
    MixerZonesRig r(2);
    auto& pane = r.panel->getZonesPaneForTest();
    const auto id = r.stripId(0);
    pane.keyPressed(juce::KeyPress(juce::KeyPress::downKey));
    ASSERT_EQ(pane.getCursorIdForTest(), id);
    EXPECT_TRUE(pane.keyPressed(letter('h')));
    EXPECT_TRUE(r.panel->getViewDoc().isHidden(id));
    EXPECT_TRUE(pane.keyPressed(letter('h')));
    EXPECT_FALSE(r.panel->getViewDoc().isHidden(id)) << "the list's H shows a hidden channel again";
}

TEST(MixerPanHideKeyTest, TheKeysAreRebindableAndTheTooltipsNameThem) {
    MixerZonesRig r(1);
    auto& shortcuts = r.mc.getShortcutManager();
    for (const char* id : {"mixerPanLeft", "mixerPanRight", "mixerToggleChannelHidden"})
        EXPECT_EQ(ShortcutManager::getCategory(id), ShortcutCategory::Mixer) << id;
    EXPECT_EQ(shortcuts.getBinding("mixerPanLeft"), shiftArrow(juce::KeyPress::leftKey));
    EXPECT_EQ(shortcuts.getBinding("mixerPanRight"), shiftArrow(juce::KeyPress::rightKey));
    EXPECT_EQ(shortcuts.getBinding("mixerToggleChannelHidden"), letter('h'));

    auto& pan = r.panel->getStripColumnForTest(0)->getPanSliderForTest();
    auto* row = r.row(r.stripId(0));
    ASSERT_NE(row, nullptr);
    const auto panKeys = ShortcutManager::keyPressToDisplayString(shiftArrow(juce::KeyPress::leftKey)) + " / " +
                         ShortcutManager::keyPressToDisplayString(shiftArrow(juce::KeyPress::rightKey));
    EXPECT_TRUE(pan.getTooltip().contains(panKeys)) << pan.getTooltip();
    EXPECT_TRUE(row->getEyeForTest().getTooltip().contains("(H)")) << row->getEyeForTest().getTooltip();

    shortcuts.setBinding("mixerToggleChannelHidden", letter('j'));
    shortcuts.setBinding("mixerPanLeft", letter('['));
    // The Settings tab saves after a rebind, which notifies onBindingsChanged; saving here would write the user's file.
    ASSERT_TRUE(shortcuts.onBindingsChanged);
    shortcuts.onBindingsChanged();
    EXPECT_TRUE(row->getEyeForTest().getTooltip().contains("(J)")) << "the eye's tooltip follows a rebind";
    EXPECT_TRUE(pan.getTooltip().contains("[")) << pan.getTooltip();

    auto& pane = r.panel->getZonesPaneForTest();
    pane.keyPressed(juce::KeyPress(juce::KeyPress::downKey));
    EXPECT_FALSE(pane.keyPressed(letter('h'))) << "the old key no longer hides";
    EXPECT_TRUE(pane.keyPressed(letter('j')));
    EXPECT_TRUE(r.panel->getViewDoc().isHidden(r.stripId(0)));
}
