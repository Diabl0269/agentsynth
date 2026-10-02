// MixerZonesVisibilityTests.cpp: hiding and showing mixer channels from the side pane -- the eye
// toggles, "Show all", Alt-click solo-show, Master's disabled eye, the filter box and chips (which narrow
// only the list) and keyboard navigation skipping hidden columns.
#include "MixerZonesTestRig.h"
#include "UI/Mixer/MixerPanelComponent/MixerPanelComponent.h"
#include <gtest/gtest.h>

namespace {
using synth::MixerZone;
using synth::ui::MixerZoneChannelKind;

juce::ModifierKeys alt() { return juce::ModifierKeys(juce::ModifierKeys::altModifier); }
} // namespace

TEST(MixerZonesVisibilityTests, TheEyeHidesAColumnAndShowAllBringsItBack) {
    MixerZonesRig r(3);
    const int before = r.panel->getColumnCount(); // 3 strips + Direct + Master
    const auto id = r.stripId(1);
    const auto serial = r.mc.getUndoManager().getEditSerial();

    r.row(id)->getEyeForTest().clicked(juce::ModifierKeys());
    EXPECT_TRUE(r.panel->getViewDoc().isHidden(id));
    EXPECT_EQ(r.panel->getColumnCount(), before - 1) << "a hidden channel has no column at all";
    EXPECT_GT(r.mc.getUndoManager().getEditSerial(), serial);
    ASSERT_NE(r.row(id), nullptr) << "the row stays in the list, dimmed";
    EXPECT_FALSE(r.row(id)->getEyeForTest().getToggleState());
    EXPECT_TRUE(r.panel->getZonesPaneForTest().getHiddenLabelForTest().getText().startsWith("1 hidden"));
    EXPECT_TRUE(r.panel->getZonesPaneForTest().getShowAllForTest().isVisible());

    r.panel->getZonesPaneForTest().getShowAllForTest().onClick();
    EXPECT_FALSE(r.panel->getViewDoc().isHidden(id));
    EXPECT_EQ(r.panel->getColumnCount(), before);
    EXPECT_FALSE(r.panel->getZonesPaneForTest().getShowAllForTest().isVisible());
}

TEST(MixerZonesVisibilityTests, HidingAChannelLeavesItsTimelineTrackAlone) {
    MixerZonesRig r(2);
    const auto tracks = r.trackOrder();
    r.panel->setChannelHidden(r.stripId(0), true);
    EXPECT_EQ(r.trackOrder(), tracks);
}

TEST(MixerZonesVisibilityTests, UndoBringsAHiddenChannelBack) {
    MixerZonesRig r(2);
    const int before = r.panel->getColumnCount();
    r.panel->setChannelHidden(r.stripId(0), true);
    ASSERT_EQ(r.panel->getColumnCount(), before - 1);
    ASSERT_TRUE(r.mc.getUndoManager().undo());
    EXPECT_EQ(r.panel->getColumnCount(), before);
}

TEST(MixerZonesVisibilityTests, AltClickAnEyeShowsOnlyThatChannelAndAgainRestores) {
    MixerZonesRig r(3);
    const auto a = r.stripId(0);
    const auto b = r.stripId(1);
    const auto c = r.stripId(2);
    r.panel->setChannelHidden(c, true); // the state Alt-click must restore

    r.row(b)->getEyeForTest().clicked(alt());
    EXPECT_FALSE(r.panel->getViewDoc().isHidden(b));
    EXPECT_TRUE(r.panel->getViewDoc().isHidden(a));
    EXPECT_TRUE(r.panel->getViewDoc().isHidden(c));
    EXPECT_TRUE(r.panel->getViewDoc().isHidden(synth::MixerViewDoc::kDirectId));
    EXPECT_FALSE(r.panel->getViewDoc().isHidden(synth::MixerViewDoc::kMasterId)) << "Master cannot be hidden";
    EXPECT_EQ(r.panel->getColumnCount(), 2) << "only b and Master remain";

    r.row(b)->getEyeForTest().clicked(alt());
    EXPECT_FALSE(r.panel->getViewDoc().isHidden(a));
    EXPECT_TRUE(r.panel->getViewDoc().isHidden(c)) << "restored to what was hidden before, not to all shown";
    EXPECT_FALSE(r.panel->getViewDoc().isHidden(synth::MixerViewDoc::kDirectId));
}

TEST(MixerZonesVisibilityTests, AnOrdinaryEditForgetsTheSoloShowMemory) {
    MixerZonesRig r(3);
    const auto a = r.stripId(0);
    const auto b = r.stripId(1);
    r.row(b)->getEyeForTest().clicked(alt());
    r.row(a)->getEyeForTest().clicked(juce::ModifierKeys()); // show a by hand
    r.row(b)->getEyeForTest().clicked(alt());
    EXPECT_TRUE(r.panel->getViewDoc().isHidden(a)) << "a fresh solo-show, not a restore of the old memory";
}

TEST(MixerZonesVisibilityTests, MastersEyeIsDisabledAndMasterCannotBeHidden) {
    MixerZonesRig r(1);
    auto* master = r.row(synth::MixerViewDoc::kMasterId);
    ASSERT_NE(master, nullptr);
    EXPECT_FALSE(master->getEyeForTest().isEnabled());
    r.panel->setChannelHidden(synth::MixerViewDoc::kMasterId, true);
    EXPECT_FALSE(r.panel->getViewDoc().isHidden(synth::MixerViewDoc::kMasterId));
    EXPECT_NE(r.panel->getMasterColumnForTest(), nullptr);
    EXPECT_TRUE(r.panel->getMasterColumnForTest()->isVisible());
}

TEST(MixerZonesVisibilityTests, DirectCanBeHidden) {
    MixerZonesRig r(1);
    auto* direct = r.row(synth::MixerViewDoc::kDirectId);
    ASSERT_NE(direct, nullptr);
    EXPECT_TRUE(direct->getEyeForTest().isEnabled());
    const int before = r.panel->getColumnCount();
    direct->getEyeForTest().clicked(juce::ModifierKeys());
    EXPECT_EQ(r.panel->getColumnCount(), before - 1);
    EXPECT_FALSE(r.panel->getDirectColumnForTest()->isVisible());
}

TEST(MixerZonesVisibilityTests, HidingAPinnedChannelKeepsItsZoneSoShowingPutsItBack) {
    MixerZonesRig r(2);
    const auto id = r.stripId(0);
    r.panel->pinChannel(id, MixerZone::Left);
    r.panel->setChannelHidden(id, true);
    EXPECT_EQ(r.panel->getViewDoc().getZone(id), MixerZone::Left);
    EXPECT_FALSE(r.panel->getLeftZoneViewportForTest().isVisible()) << "nothing left in the zone to show";
    ASSERT_NE(r.row(id), nullptr);
    EXPECT_EQ(r.row(id)->getChannel().zone, MixerZone::Left) << "the list still files it under Left zone";

    r.panel->setChannelHidden(id, false);
    EXPECT_TRUE(r.panel->getLeftZoneViewportForTest().isVisible());
    EXPECT_EQ(r.panel->getColumnZoneForTest(0), MixerZone::Left);
}

TEST(MixerZonesVisibilityTests, TheFilterAndChipsNarrowOnlyTheList) {
    MixerZonesRig r(3);
    r.panel->createBus();
    r.panel->rebuild();
    auto& pane = r.panel->getZonesPaneForTest();
    const int columns = r.panel->getColumnCount();
    const int rows = pane.getRowCountForTest();
    ASSERT_EQ(rows, columns) << "with nothing hidden the list and the mixer show the same channels";

    pane.getChipForTest(synth::ui::MixerZonesPane::Chip::Buses).onClick();
    EXPECT_EQ(pane.getRowCountForTest(), 1);
    pane.getChipForTest(synth::ui::MixerZonesPane::Chip::Tracks).onClick();
    EXPECT_EQ(pane.getRowCountForTest(), 3);
    pane.getChipForTest(synth::ui::MixerZonesPane::Chip::All).onClick();
    EXPECT_EQ(pane.getRowCountForTest(), rows);

    // TextEditor announces a change on the message queue, so the change callback is run by hand.
    pane.getFilterForTest().setText("master", false);
    pane.getFilterForTest().onTextChange();
    EXPECT_EQ(pane.getRowCountForTest(), 1);
    ASSERT_NE(pane.getRowForTest(0), nullptr);
    EXPECT_EQ(pane.getRowForTest(0)->getChannel().kind, MixerZoneChannelKind::Master);
    pane.getFilterForTest().setText("", false);
    pane.getFilterForTest().onTextChange();
    EXPECT_EQ(pane.getRowCountForTest(), rows);

    EXPECT_EQ(r.panel->getColumnCount(), columns) << "the mixer itself was never filtered";
    for (int i = 0; i < 3; ++i)
        EXPECT_FALSE(r.panel->getViewDoc().isHidden(r.stripId(i)));
}

TEST(MixerZonesVisibilityTests, KeyboardNavigationSkipsHiddenColumns) {
    MixerZonesRig r(3);
    r.panel->setChannelHidden(r.stripId(1), true);
    // Visible columns now: track 0, track 2, Direct, Master.
    ASSERT_EQ(r.panel->getColumnCount(), 4);

    const auto right = juce::KeyPress(juce::KeyPress::rightKey);
    EXPECT_TRUE(r.panel->keyPressed(right)); // from nothing: column 0
    EXPECT_EQ(r.panel->getFocusedColumnIndexForTest(), 0);
    EXPECT_TRUE(r.panel->keyPressed(right));
    EXPECT_EQ(r.panel->getFocusedColumnIndexForTest(), 1);
    EXPECT_EQ(r.panel->getAccessibilityFocusTargetForTest(),
              &r.panel->getStripColumnForTest(1)->getAccessibilityFocusTargetForTest())
        << "the second step lands on track 2, skipping the hidden track 1";
}

TEST(MixerZonesVisibilityTests, NavigationFollowsTheZonesLeftToRight) {
    MixerZonesRig r(2);
    r.panel->pinChannel(synth::MixerViewDoc::kMasterId, MixerZone::Left);
    EXPECT_TRUE(r.panel->keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    EXPECT_EQ(r.panel->getFocusedColumnIndexForTest(), 0);
    EXPECT_EQ(r.panel->getAccessibilityFocusTargetForTest(),
              &r.panel->getMasterColumnForTest()->getAccessibilityFocusTargetForTest())
        << "Master, now pinned left, is the first column";
}

TEST(MixerZonesVisibilityTests, EveryPaneControlHasATitleAndDescription) {
    MixerZonesRig r(1);
    auto& pane = r.panel->getZonesPaneForTest();
    EXPECT_TRUE(pane.getFilterForTest().getTitle().isNotEmpty());
    EXPECT_TRUE(pane.getFilterForTest().getDescription().isNotEmpty());
    for (auto chip : {synth::ui::MixerZonesPane::Chip::All, synth::ui::MixerZonesPane::Chip::Tracks,
                      synth::ui::MixerZonesPane::Chip::Buses}) {
        EXPECT_TRUE(pane.getChipForTest(chip).getTitle().isNotEmpty());
        EXPECT_TRUE(pane.getChipForTest(chip).getDescription().isNotEmpty());
    }
    auto& button = r.panel->getSidePaneButton();
    EXPECT_TRUE(button.getTitle().isNotEmpty());
    EXPECT_TRUE(button.getDescription().isNotEmpty());
    auto* row = pane.getRowForTest(0);
    ASSERT_NE(row, nullptr);
    EXPECT_TRUE(row->getTitle().isNotEmpty());
    EXPECT_TRUE(row->getDescription().isNotEmpty());
    EXPECT_TRUE(row->getEyeForTest().getTitle().isNotEmpty());
    EXPECT_TRUE(row->getEyeForTest().getDescription().isNotEmpty());
}
