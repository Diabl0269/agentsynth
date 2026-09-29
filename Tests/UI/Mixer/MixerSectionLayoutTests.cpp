// MixerSectionLayoutTests.cpp (docs/mixer/panel.md#shared-sections): the mixer's shared section
// state on its own -- row snapping, fitting the sections into one column height (the fader keeps its
// minimum), the divider drag measured from its start, and the settings round-trip.
#include "UI/Mixer/MixerSections/MixerSectionLayout.h"
#include <gtest/gtest.h>

namespace {
using synth::ui::MixerSection;
using Layout = synth::ui::MixerSectionLayout;

size_t at(MixerSection section) { return (size_t)section; }
} // namespace

TEST(MixerSectionLayoutTest, DefaultsAreFourInsertsPlusTheLinkRowTwoSendsPlusAddAndTheEqCurve) {
    Layout layout;
    EXPECT_EQ(layout.getRequestedHeight(MixerSection::Inserts), 90);
    EXPECT_EQ(layout.getRequestedHeight(MixerSection::Sends), 60);
    EXPECT_EQ(layout.getRequestedHeight(MixerSection::Eq), 28);
    EXPECT_EQ(layout.getRowCount(MixerSection::Inserts), 5);
    EXPECT_EQ(layout.getRowCount(MixerSection::Sends), 3);
    for (auto section : {MixerSection::Inserts, MixerSection::Sends, MixerSection::Eq})
        EXPECT_FALSE(layout.isHidden(section));
}

TEST(MixerSectionLayoutTest, HeightsSnapToWholeRowsWithinOneAndTheMaximumRowCount) {
    Layout layout;
    layout.setRequestedHeight(MixerSection::Inserts, 100); // 5.6 rows of 18
    EXPECT_EQ(layout.getRequestedHeight(MixerSection::Inserts), 108);
    layout.setRequestedHeight(MixerSection::Sends, 69); // 3.45 rows of 20
    EXPECT_EQ(layout.getRequestedHeight(MixerSection::Sends), 60);
    layout.setRequestedHeight(MixerSection::Inserts, 0);
    EXPECT_EQ(layout.getRequestedHeight(MixerSection::Inserts), 18) << "never below one row";
    layout.setRequestedHeight(MixerSection::Inserts, 100000);
    EXPECT_EQ(layout.getRequestedHeight(MixerSection::Inserts), Layout::kMaxRows * 18);
    layout.setRequestedHeight(MixerSection::Eq, 200);
    EXPECT_EQ(layout.getRequestedHeight(MixerSection::Eq), 28) << "the EQ curve has one fixed height";
}

TEST(MixerSectionLayoutTest, WithRoomEverySectionGetsItsHeightAndTheFaderTakesTheRest) {
    Layout layout;
    const auto g = layout.resolve(600);
    EXPECT_EQ(g.sectionTop[at(MixerSection::Inserts)], 40) << "under the 24 px header and 14 px source line";
    EXPECT_EQ(g.sectionHeight[at(MixerSection::Inserts)], 90);
    EXPECT_EQ(g.dividerTop[at(MixerSection::Inserts)], 130);
    EXPECT_EQ(g.sectionTop[at(MixerSection::Sends)], 136);
    EXPECT_EQ(g.sectionHeight[at(MixerSection::Sends)], 60);
    EXPECT_EQ(g.sectionTop[at(MixerSection::Eq)], 202);
    EXPECT_EQ(g.sectionHeight[at(MixerSection::Eq)], 28);
    EXPECT_EQ(g.panTop, 236);
    EXPECT_EQ(g.panHeight, 28);
    EXPECT_EQ(g.readoutTop, 264);
    EXPECT_EQ(g.faderTop, 276);
    EXPECT_EQ(g.msTop, 600 - 2 - 20);
    EXPECT_EQ(g.faderHeight, g.msTop - g.faderTop);
}

// Regression test for FRO298: at the bottom dock's default column height the fader must keep its
// minimum; the sections give way first, EQ then Sends then Inserts.
TEST(MixerSectionLayoutTest, AShortColumnGivesWayEqThenSendsThenInsertsAndTheFaderKeepsItsMinimum) {
    Layout layout;
    const auto g = layout.resolve(198);
    EXPECT_EQ(g.faderHeight, Layout::kMinFaderHeight);
    EXPECT_EQ(g.sectionHeight[at(MixerSection::Eq)], 0);
    EXPECT_EQ(g.sectionHeight[at(MixerSection::Sends)], 0);
    EXPECT_EQ(g.sectionHeight[at(MixerSection::Inserts)], 22);
    EXPECT_EQ(g.panHeight, Layout::kPanHeight) << "the pan knob gives way only after every section";
}

TEST(MixerSectionLayoutTest, RequiredColumnHeightFitsEverySectionWithTheFaderAtItsMinimum) {
    Layout layout;
    const int required = layout.requiredColumnHeight();
    EXPECT_EQ(required, 354);
    const auto g = layout.resolve(required);
    EXPECT_EQ(g.faderHeight, Layout::kMinFaderHeight);
    EXPECT_EQ(g.sectionHeight[at(MixerSection::Inserts)], 90);
    EXPECT_EQ(g.sectionHeight[at(MixerSection::Eq)], 28);
    EXPECT_LT(layout.resolve(required - 1).sectionHeight[at(MixerSection::Eq)], 28);
}

TEST(MixerSectionLayoutTest, AHiddenSectionKeepsItsFourteenPixelStripAndGivesTheRestToTheFader) {
    Layout layout;
    const int faderBefore = layout.resolve(600).faderHeight;
    int geometryChanges = 0;
    int commits = 0;
    layout.onGeometryChanged = [&] { ++geometryChanges; };
    layout.onCommitted = [&] { ++commits; };

    layout.setHidden(MixerSection::Sends, true);
    const auto g = layout.resolve(600);
    EXPECT_EQ(g.sectionHeight[at(MixerSection::Sends)], Layout::kCollapsedHeight);
    EXPECT_EQ(g.faderHeight, faderBefore + 60 - Layout::kCollapsedHeight);
    EXPECT_EQ(layout.getRequestedHeight(MixerSection::Sends), 60) << "the height comes back when shown again";
    EXPECT_EQ(geometryChanges, 1);
    EXPECT_EQ(commits, 1) << "show/hide is a finished gesture, so it persists";

    layout.setHidden(MixerSection::Sends, true);
    EXPECT_EQ(geometryChanges, 1) << "no change, no notification";
}

TEST(MixerSectionLayoutTest, DividerDragIsMeasuredFromTheDragStartAndSnapsToRows) {
    Layout layout;
    int commits = 0;
    layout.onCommitted = [&] { ++commits; };

    layout.beginDividerDrag(MixerSection::Inserts);
    EXPECT_EQ(layout.getDraggingDivider(), (int)MixerSection::Inserts);
    layout.dragDividerBy(MixerSection::Inserts, 20); // 110 -> 6 rows
    EXPECT_EQ(layout.getRequestedHeight(MixerSection::Inserts), 108);
    layout.dragDividerBy(MixerSection::Inserts, 5); // absolute from the start: 95 -> 5 rows, not 108 + 5
    EXPECT_EQ(layout.getRequestedHeight(MixerSection::Inserts), 90);
    layout.dragDividerBy(MixerSection::Sends, 200);
    EXPECT_EQ(layout.getRequestedHeight(MixerSection::Sends), 60) << "only the dragged divider moves";
    EXPECT_EQ(commits, 0) << "nothing persists mid-drag";
    layout.endDividerDrag();
    EXPECT_EQ(layout.getDraggingDivider(), -1);
    EXPECT_EQ(commits, 1);
}

TEST(MixerSectionLayoutTest, DraggingAHiddenSectionsDividerDownShowsItAgain) {
    Layout layout;
    layout.setHidden(MixerSection::Inserts, true);
    layout.beginDividerDrag(MixerSection::Inserts);
    layout.dragDividerBy(MixerSection::Inserts, -10);
    EXPECT_TRUE(layout.isHidden(MixerSection::Inserts)) << "dragging up on a strip does nothing";
    layout.dragDividerBy(MixerSection::Inserts, 30); // grows from the 14 px strip: 44 -> 2 rows
    EXPECT_FALSE(layout.isHidden(MixerSection::Inserts));
    EXPECT_EQ(layout.getRequestedHeight(MixerSection::Inserts), 36);
    layout.dragDividerBy(MixerSection::Inserts, 60); // 74 -> 4 rows
    EXPECT_EQ(layout.getRequestedHeight(MixerSection::Inserts), 72);
    layout.endDividerDrag();
}

TEST(MixerSectionLayoutTest, ResetRestoresTheDefaultHeightAndPersists) {
    Layout layout;
    int commits = 0;
    layout.onCommitted = [&] { ++commits; };
    layout.setRequestedHeight(MixerSection::Sends, 160);
    layout.resetToDefault(MixerSection::Sends);
    EXPECT_EQ(layout.getRequestedHeight(MixerSection::Sends), 60);
    EXPECT_EQ(commits, 1);
}

TEST(MixerSectionLayoutTest, SettingsRoundTripHeightsAndHiddenFlags) {
    Layout layout;
    layout.setRequestedHeight(MixerSection::Inserts, 144);
    layout.setRequestedHeight(MixerSection::Sends, 100);
    layout.setHidden(MixerSection::Eq, true);
    layout.setHidden(MixerSection::Sends, true);

    juce::PropertySet settings;
    layout.saveTo(settings);

    Layout restored;
    restored.loadFrom(settings);
    EXPECT_EQ(restored.getRequestedHeight(MixerSection::Inserts), 144);
    EXPECT_EQ(restored.getRequestedHeight(MixerSection::Sends), 100);
    EXPECT_TRUE(restored.isHidden(MixerSection::Eq));
    EXPECT_TRUE(restored.isHidden(MixerSection::Sends));
    EXPECT_FALSE(restored.isHidden(MixerSection::Inserts));

    juce::PropertySet empty;
    Layout fresh;
    fresh.loadFrom(empty);
    EXPECT_EQ(fresh.getRequestedHeight(MixerSection::Inserts), 90) << "absent keys load the defaults";
}
