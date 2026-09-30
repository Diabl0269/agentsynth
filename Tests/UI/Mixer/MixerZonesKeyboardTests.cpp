// MixerZonesKeyboardTests.cpp: the Mixer's keyboard and accessibility paths added for the side pane
// and the section rows -- the Zones list's row cursor (Up/Down, Space, Alt+Up/Down, Esc), the Cmd+Shift+B
// toggle moving focus into the list, and the rebindable Ctrl+I/S/E section toggles and their tooltips.
// Drives a real off-screen MainComponent, so every edit goes through the real undo and rebuild path.
#include "MixerZonesTestRig.h"
#include "ShortcutManager/ShortcutManager.h"
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
