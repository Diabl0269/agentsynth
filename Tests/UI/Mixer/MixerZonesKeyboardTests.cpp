// MixerZonesKeyboardTests.cpp: the Mixer's keyboard and accessibility paths added for the side pane
// and the section rows -- the Zones list's row cursor (Up/Down, Space, Alt+Up/Down, Esc), the Cmd+Shift+B
// toggle moving focus into the list, and the rebindable Ctrl+I/S/E section toggles and their tooltips.
// Drives a real off-screen MainComponent, so every edit goes through the real undo and rebuild path.
#include "../../TestWait.h"
#include "MixerNativeHost.h"
#include "MixerZonesTestRig.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Mixer/MixerPanelComponent/MixerPanelComponent.h"
#include "UI/Mixer/MixerPanelComponent/MixerPanelToolbar.h"
#include <gtest/gtest.h>

namespace {
using synth::MixerZone;
using synth::ui::MixerSection;

juce::KeyPress plain(int code) { return juce::KeyPress(code, juce::ModifierKeys(), 0); }
juce::KeyPress withAlt(int code) {
    return juce::KeyPress(code, juce::ModifierKeys(juce::ModifierKeys::altModifier), 0);
}
} // namespace

TEST(MixerZonesKeyboardTests, ArrowsWalkTheListedRowsAndClampAtBothEnds) {
    MixerZonesRig r(2);
    auto& pane = r.panel->getZonesPaneForTest();
    const int rows = pane.getRowCountForTest();
    ASSERT_GE(rows, 3);

    EXPECT_TRUE(pane.keyPressed(plain(juce::KeyPress::downKey)));
    EXPECT_EQ(pane.getCursorIdForTest(), pane.getRowForTest(0)->getChannel().id) << "from no cursor: the first row";
    for (int i = 0; i < rows + 3; ++i)
        pane.keyPressed(plain(juce::KeyPress::downKey));
    EXPECT_EQ(pane.getCursorIdForTest(), pane.getRowForTest(rows - 1)->getChannel().id) << "clamped, never wraps";
    pane.keyPressed(plain(juce::KeyPress::upKey));
    EXPECT_EQ(pane.getCursorIdForTest(), pane.getRowForTest(rows - 2)->getChannel().id);
}

TEST(MixerZonesKeyboardTests, SpaceHidesAndShowsTheCursorChannelButNeverMaster) {
    MixerZonesRig r(2);
    auto& pane = r.panel->getZonesPaneForTest();
    const auto id = r.stripId(0);
    const int before = r.panel->getColumnCount();
    pane.keyPressed(plain(juce::KeyPress::downKey));
    ASSERT_EQ(pane.getCursorIdForTest(), id);

    EXPECT_TRUE(pane.keyPressed(plain(juce::KeyPress::spaceKey)));
    EXPECT_TRUE(r.panel->getViewDoc().isHidden(id));
    EXPECT_EQ(r.panel->getColumnCount(), before - 1);
    EXPECT_EQ(pane.getCursorIdForTest(), id) << "the cursor survives the rebuild the edit causes";
    pane.keyPressed(plain(juce::KeyPress::spaceKey));
    EXPECT_FALSE(r.panel->getViewDoc().isHidden(id));

    for (int i = 0; i < 10; ++i)
        pane.keyPressed(plain(juce::KeyPress::downKey));
    ASSERT_EQ(pane.getCursorIdForTest(), juce::String(synth::MixerViewDoc::kMasterId)) << "Master is last";
    pane.keyPressed(plain(juce::KeyPress::spaceKey));
    EXPECT_FALSE(r.panel->getViewDoc().isHidden(synth::MixerViewDoc::kMasterId));
}

TEST(MixerZonesKeyboardTests, AltArrowsMoveTheCursorChannelOneGroupAtATime) {
    MixerZonesRig r(2);
    auto& pane = r.panel->getZonesPaneForTest();
    const auto id = r.stripId(1);
    pane.keyPressed(plain(juce::KeyPress::downKey));
    pane.keyPressed(plain(juce::KeyPress::downKey));
    ASSERT_EQ(pane.getCursorIdForTest(), id);

    EXPECT_TRUE(pane.keyPressed(withAlt(juce::KeyPress::upKey)));
    EXPECT_EQ(r.panel->getViewDoc().getZone(id), MixerZone::Left);
    pane.keyPressed(withAlt(juce::KeyPress::upKey));
    EXPECT_EQ(r.panel->getViewDoc().getZone(id), MixerZone::Left) << "already in the first group";
    pane.keyPressed(withAlt(juce::KeyPress::downKey));
    pane.keyPressed(withAlt(juce::KeyPress::downKey));
    EXPECT_EQ(r.panel->getViewDoc().getZone(id), MixerZone::Right);
    EXPECT_EQ(pane.getCursorIdForTest(), id) << "the cursor follows the channel into its new group";
    EXPECT_GT(r.row(id)->getY(), pane.getGroupHeaderForTest(MixerZone::Right).getY());
}

TEST(MixerZonesKeyboardTests, EscapeHandsFocusBackToTheMixer) {
    MixerZonesRig r(1);
    auto& pane = r.panel->getZonesPaneForTest();
    bool released = false;
    const auto previous = pane.onFocusReleased;
    pane.onFocusReleased = [&] { released = true; };
    EXPECT_TRUE(pane.keyPressed(plain(juce::KeyPress::escapeKey)));
    EXPECT_TRUE(released);
    pane.onFocusReleased = previous;
}

TEST(MixerZonesKeyboardTests, OpeningThePaneFromTheKeyboardPutsTheCursorOnARow) {
    MixerZonesRig r(2);
    auto& pane = r.panel->getZonesPaneForTest();
    if (r.panel->getSidePane().isOpen())
        r.panel->toggleSidePane();
    ASSERT_FALSE(r.panel->getSidePane().isOpen());
    EXPECT_TRUE(r.panel->toggleSidePane());
    EXPECT_TRUE(r.panel->getSidePane().isOpen());
    EXPECT_EQ(pane.getCursorIdForTest(), pane.getRowForTest(0)->getChannel().id);
}

TEST(MixerZonesKeyboardTests, RowsAndTheListAreNamedForScreenReaders) {
    MixerZonesRig r(1);
    auto* row = r.row(r.stripId(0));
    ASSERT_NE(row, nullptr);
    EXPECT_FALSE(row->getTitle().isEmpty());
    EXPECT_TRUE(row->getDescription().contains("Alt+Up"));
    EXPECT_TRUE(r.panel->getZonesPaneForTest().getWantsKeyboardFocus()) << "the list is reachable by keyboard";
}

TEST(MixerZonesKeyboardTests, GroupHeadingsAreDrawnInSentenceCase) {
    for (const auto zone : {MixerZone::Left, MixerZone::Scrolling, MixerZone::Right}) {
        synth::ui::MixerZonesGroupHeader header(zone);
        EXPECT_EQ(header.getDisplayText(), synth::ui::MixerZonesGroupHeader::titleFor(zone)) << "not upper-cased";
    }
    EXPECT_EQ(synth::ui::MixerZonesGroupHeader(MixerZone::Left).getDisplayText(), "Left zone");
}

// Regression test for FRO391: the audit of the live mixer's accessibility tree found these unnamed.
TEST(MixerZonesKeyboardTests, MixerControlsTheAuditFoundUnnamedNowCarryNames) {
    MixerZonesRig r(1);
    EXPECT_EQ(r.panel->getTitle(), "Mixer");
    auto* column = r.panel->getStripColumnForTest(0);
    ASSERT_NE(column, nullptr);
    const auto name = column->getTitle();
    EXPECT_EQ(column->getHeaderForTest().getNameLabelForTest().getTitle(), name + " name");
    EXPECT_EQ(column->getEqThumbnailForTest().getTitle(), name + " EQ curve");
    EXPECT_EQ(r.panel->getMasterColumnForTest()->getMeterReadoutForTest().getTitle(), "Master peak");
}

// ---- Section toggles ----

TEST(MixerSectionShortcutTests, CtrlLettersToggleInsertsSendsAndEqLikeTheToolbar) {
    MixerZonesRig r(1);
    auto& toolbar = r.panel->getToolbarForTest();
    struct Case {
        MixerSection section;
        const char* action;
    };
    for (const auto& c : {Case{MixerSection::Inserts, "mixerToggleInserts"},
                          Case{MixerSection::Sends, "mixerToggleSends"}, Case{MixerSection::Eq, "mixerToggleEq"}}) {
        const auto key = r.mc.getShortcutManager().getBinding(c.action);
        auto& toggle = toolbar.getSectionToggleForTest(c.section);
        const bool shown = toggle.getToggleState(); // the layout is persisted, so start from whatever it is
        EXPECT_TRUE(r.panel->keyPressed(key)) << c.action;
        EXPECT_NE(toggle.getToggleState(), shown) << c.action << ": the toolbar toggle follows the key";
        EXPECT_TRUE(r.panel->keyPressed(key));
        EXPECT_EQ(toggle.getToggleState(), shown);
    }
}

TEST(MixerSectionShortcutTests, DefaultsNeverShadowSaveAndLiveInTheMixerCategory) {
    ShortcutManager shortcuts;
    for (const auto* id : {"mixerToggleInserts", "mixerToggleSends", "mixerToggleEq"}) {
        EXPECT_EQ(ShortcutManager::getCategory(id), ShortcutCategory::Mixer) << id;
        EXPECT_NE(ShortcutManager::getActionDescription(id), juce::String(id)) << id << " has a readable name";
        EXPECT_FALSE(ShortcutManager::keyPressMatches(shortcuts.getBinding(id), shortcuts.getBinding("savePreset")))
            << id;
    }
#if JUCE_MAC
    EXPECT_EQ(shortcuts.getBinding("mixerToggleSends"), juce::KeyPress('s', juce::ModifierKeys::ctrlModifier, 0));
#endif
}

TEST(MixerSectionShortcutTests, ToggleTooltipsNameTheBindingAndFollowARebind) {
    MixerZonesRig r(1);
    auto& shortcuts = r.mc.getShortcutManager();
    auto& toggle = r.panel->getToolbarForTest().getSectionToggleForTest(MixerSection::Sends);
    EXPECT_TRUE(toggle.getTooltip().contains(
        ShortcutManager::keyPressToDisplayString(shortcuts.getBinding("mixerToggleSends"))))
        << toggle.getTooltip();

    const juce::KeyPress rebound('j', juce::ModifierKeys::ctrlModifier, 0);
    const auto original = shortcuts.getBinding("mixerToggleSends");
    shortcuts.setBinding("mixerToggleSends", rebound);
    // What the Settings tab's save fires, without writing the shared settings file.
    ASSERT_TRUE(shortcuts.onBindingsChanged);
    shortcuts.onBindingsChanged();
    EXPECT_TRUE(toggle.getTooltip().contains(ShortcutManager::keyPressToDisplayString(rebound))) << toggle.getTooltip();
    const bool shown = toggle.getToggleState();
    EXPECT_TRUE(r.panel->keyPressed(rebound));
    EXPECT_NE(toggle.getToggleState(), shown);
    shortcuts.setBinding("mixerToggleSends", original);
    r.panel->keyPressed(original);
}

// Pan and hide keys: the focused mixer column's pan keys (Shift+Left / Shift+Right by default) and hide key
// (H), both rebindable: the pan moves one step per press as one undo step, H hides the column and hands the focus to
// its neighbour, and H in the side pane's list shows the channel again. Column keys travel the real ComponentPeer
// dispatch on a native window holding the real panel of an off-screen MainComponent.

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
