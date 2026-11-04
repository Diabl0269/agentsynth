// MixerStripClickFocusTests.cpp -- a press anywhere in a mixer column focuses it and shows its outline, the outline
// leaves when another control takes the keyboard, Cmd+D copies the focused strip's track every time, and Cmd+Up from a
// clicked strip reaches the top bar ring. Clicks travel the real ComponentPeer path the test mode uses, on a native
// window holding the real panel of an off-screen MainComponent.
#include "../../TestWait.h"
#include "MixerNativeHost.h"
#include "MixerZonesTestRig.h"
#include "ShortcutManager/ShortcutManager.h"
#include "TestMode/TestModeInput.h"
#include <gtest/gtest.h>

namespace {

using namespace synth::test::mixer;

juce::KeyPress cmdD() { return juce::KeyPress('d', juce::ModifierKeys::commandModifier, 0); }
juce::KeyPress cmdUp() { return juce::KeyPress(juce::KeyPress::upKey, juce::ModifierKeys::commandModifier, 0); }

// A point in the column where nothing but the column itself is hit: its empty space.
std::optional<juce::Point<int>> emptySpaceIn(juce::Component& column) {
    for (int y = column.getHeight() - 1; y > 0; y -= 3)
        for (int x = 1; x < column.getWidth(); x += 3)
            if (column.getComponentAt(x, y) == &column)
                return juce::Point<int>(x, y);
    return std::nullopt;
}

int showingRings(synth::ui::MixerPanelComponent& panel) {
    int rings = 0;
    for (int i = 0; auto* column = panel.getStripColumnForTest(i); ++i)
        rings += column->isKeyboardFocusedForTest() ? 1 : 0;
    return rings;
}

} // namespace

// Regression test for FRO1421: clicking a strip showed no focus, so rename and duplicate acted on an unseen strip.
TEST(MixerStripClickFocusTest, APressOnAStripsEmptySpaceOrAControlFocusesThatStrip) {
    if (nativeWindowsUnavailable())
        GTEST_SKIP() << "needs a display for a native window";
    MixerZonesRig r(3);
    NativeHost host(*r.panel);
    auto* second = r.panel->getStripColumnForTest(1);
    auto* third = r.panel->getStripColumnForTest(2);
    ASSERT_NE(second, nullptr);
    ASSERT_NE(third, nullptr);
    ASSERT_EQ(r.panel->getFocusedColumnIndexForTest(), -1);

    const auto empty = emptySpaceIn(*second);
    ASSERT_TRUE(empty.has_value()) << "the strip has no empty space to press";
    ASSERT_TRUE(click(*second, *empty));
    EXPECT_EQ(r.panel->getFocusedColumnIndexForTest(), 1);
    EXPECT_TRUE(second->isKeyboardFocusedForTest()) << "the pressed strip shows the focus outline";
    EXPECT_EQ(showingRings(*r.panel), 1);

    // A press on a control (the fader) focuses its strip as well; a right-click on a header does too.
    auto& fader = third->getFaderForTest();
    ASSERT_TRUE(click(fader, fader.getLocalBounds().getCentre()));
    EXPECT_EQ(r.panel->getFocusedColumnIndexForTest(), 2);
    EXPECT_TRUE(third->isKeyboardFocusedForTest());
    EXPECT_FALSE(second->isKeyboardFocusedForTest());

    r.panel->setShowZoneMenuHookForTest([](juce::PopupMenu&) {});
    // On the name, not the header's corner: a press on the colour dot would open its picker.
    auto* name = second->getHeaderForTest().findChildWithID("mixerColumnHeaderName");
    ASSERT_NE(name, nullptr);
    ASSERT_TRUE(click(*name, name->getLocalBounds().getCentre(), synth::testmode::Button::right));
    EXPECT_EQ(r.panel->getFocusedColumnIndexForTest(), 1) << "the outline shows which strip the menu acts on";
}

// Regression test for FRO1421: the outline stayed on the strip after a click elsewhere took the keyboard.
TEST(MixerStripClickFocusTest, TheOutlineLeavesWhenAnotherControlTakesTheKeyboardAndComesBack) {
    if (nativeWindowsUnavailable())
        GTEST_SKIP() << "needs a display for a native window";
    MixerZonesRig r(2);
    NativeHost host(*r.panel);
    auto* first = r.panel->getStripColumnForTest(0);
    ASSERT_NE(first, nullptr);
    const auto empty = emptySpaceIn(*first);
    ASSERT_TRUE(empty.has_value());
    ASSERT_TRUE(click(*first, *empty));
    if (!r.panel->hasKeyboardFocus(true))
        GTEST_SKIP() << "this machine would not give a temporary window the keyboard";
    EXPECT_TRUE(r.panel->hasKeyboardFocus(false)) << "the panel holds the keyboard, not the column's colour dot";
    ASSERT_TRUE(first->isKeyboardFocusedForTest());

    ASSERT_TRUE(click(host.canvas, host.canvas.getLocalBounds().getCentre()));
    ASSERT_TRUE(host.canvas.hasKeyboardFocus(false));
    EXPECT_FALSE(first->isKeyboardFocusedForTest()) << "the outline leaves with the keyboard";
    EXPECT_EQ(r.panel->getFocusedColumnIndexForTest(), 0) << "the column keeps its place for the way back";

    r.panel->grabKeyboardFocus();
    EXPECT_TRUE(first->isKeyboardFocusedForTest()) << "and comes back with it";
}

// Regression test for FRO1421: Duplicate worked once, then stopped acting on the strip the user had chosen.
TEST(MixerStripClickFocusTest, CmdDCopiesTheFocusedStripsTrackEveryTimeAndFocusStaysOnIt) {
    MixerZonesRig r(2);
    auto& panel = *r.panel;
    ASSERT_TRUE(panel.keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    ASSERT_TRUE(panel.keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    ASSERT_EQ(panel.getFocusedColumnIndexForTest(), 1);
    const auto source = panel.getStripColumnForTest(1)->getUuid();
    const auto sourceName = r.mc.getTimelineDoc().getTracks()[1].name;
    const int rowBefore = r.mc.getTimelinePanel().getFocusedTrackIndexForTest();

    for (int press = 1; press <= 3; ++press) {
        ASSERT_TRUE(panel.keyPressed(cmdD())) << "press " << press;
        ASSERT_EQ(r.mc.getTimelineDoc().getTracks().size(), (size_t)(2 + press)) << "press " << press;
        const int focused = panel.getFocusedColumnIndexForTest();
        ASSERT_NE(panel.getStripColumnForTest(focused), nullptr);
        EXPECT_EQ(panel.getStripColumnForTest(focused)->getUuid(), source) << "focus stays on the source strip";
    }
    int copies = 0;
    for (const auto& track : r.mc.getTimelineDoc().getTracks())
        copies += track.name == sourceName + " copy" ? 1 : 0;
    EXPECT_EQ(copies, 3) << "every press copied the focused strip's own track";
    EXPECT_EQ(r.mc.getTimelinePanel().getFocusedTrackIndexForTest(), rowBefore)
        << "a duplicate from the mixer leaves the timeline's keyboard row alone";
}

TEST(MixerStripClickFocusTest, CmdDIsLeftToBubbleOnDirectAndMaster) {
    MixerZonesRig r(1);
    auto& panel = *r.panel;
    for (int i = 0; i < panel.getColumnCount(); ++i)
        panel.keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
    ASSERT_EQ(panel.getFocusedColumnIndexForTest(), panel.getColumnCount() - 1) << "Master, the last column";
    EXPECT_FALSE(panel.keyPressed(cmdD()));
    EXPECT_EQ(r.mc.getTimelineDoc().getTracks().size(), 1u);
}

TEST(MixerStripClickFocusTest, TheStripMenusDuplicateTrackNamesItsKey) {
    MixerZonesRig r(1);
    juce::String shown;
    r.panel->setShowZoneMenuHookForTest([&](juce::PopupMenu& menu) {
        juce::PopupMenu::MenuItemIterator it(menu);
        while (it.next())
            if (it.getItem().text == "Duplicate Track")
                shown = it.getItem().shortcutKeyDescription;
    });
    r.panel->showChannelMenuForTest(r.stripId(0));
    const auto expected =
        ShortcutManager::keyPressToDisplayString(r.mc.getShortcutManager().getBinding("timelineDuplicateFocusedTrack"));
    EXPECT_FALSE(expected.isEmpty());
    EXPECT_EQ(shown, expected);
}

// Regression test for FRO1421: Cmd+Up after a click on a strip did not reach the top bar ring.
TEST(MixerStripClickFocusTest, CmdUpAfterAClickOnAStripReachesTheTopBarRing) {
    if (nativeWindowsUnavailable())
        GTEST_SKIP() << "needs a display for a native window";
    MixerZonesRig r(2);
    NativeHost host(*r.panel);
    auto* first = r.panel->getStripColumnForTest(0);
    ASSERT_NE(first, nullptr);
    auto& fader = first->getFaderForTest();
    ASSERT_TRUE(click(fader, fader.getLocalBounds().getCentre()));
    ASSERT_EQ(r.panel->getFocusedColumnIndexForTest(), 0);

    // The key walks up from whatever holds the keyboard, as the real dispatch does.
    auto* target = juce::Component::getCurrentlyFocusedComponent();
    if (target == nullptr || !(target == r.panel || r.panel->isParentOf(target)))
        target = r.panel;
    bool handled = false;
    for (auto* c = target; c != nullptr && !handled; c = c->getParentComponent())
        handled = c->keyPressed(cmdUp());
    EXPECT_TRUE(handled);
    EXPECT_TRUE(r.panel->getTopBarRingForTest().isActive());
}
