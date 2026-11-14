// TimelineSectionStripSnapTests.cpp — the Sections strip snaps to the longer of the timeline's Snap and one bar;
// holding Shift on a make drag follows the Snap unit itself (free with Snap Off), and a drop lands on the same grid.
#include "TimelineSectionStripFixture.h"
#include "UI/Timeline/TimelineSectionStrip/SectionDropPlan.h"
#include <gtest/gtest.h>

using namespace synth::ui;
using namespace synth::ui::sectionstrip;
using namespace sectionstrip_test;

namespace {
constexpr int kShift = juce::ModifierKeys::shiftModifier;
constexpr double kBeat = 1.0;
} // namespace

TEST(SectionStripSnapTest, SnapBarKeepsTodaysWholeBars) {
    const auto span = makeDragSpan(5.0, 9.5, 4.0, {});
    EXPECT_DOUBLE_EQ(span.start, 4.0);
    EXPECT_DOUBLE_EQ(span.end, 12.0);
    EXPECT_TRUE(spansAtLeastOneUnit({4.0, 8.0}, 4.0));
    EXPECT_FALSE(spansAtLeastOneUnit({4.0, 7.0}, 4.0));
}

TEST(SectionStripSnapTest, TheStripGridIsTheLongerOfSnapAndABar) {
    EXPECT_DOUBLE_EQ(stripGrid(1.0, 4.0, false), 4.0) << "Snap 1/4";
    EXPECT_DOUBLE_EQ(stripGrid(0.25, 4.0, false), 4.0) << "Snap 1/16";
    EXPECT_DOUBLE_EQ(stripGrid(0.0, 4.0, false), 4.0) << "Snap Off";
    EXPECT_DOUBLE_EQ(stripGrid(8.0, 4.0, false), 8.0) << "Snap 2 bars";
    EXPECT_DOUBLE_EQ(stripGrid(1.0, 4.0, true), 1.0) << "Shift follows the unit";
    EXPECT_DOUBLE_EQ(stripGrid(0.0, 4.0, true), 0.0) << "Shift with Snap Off is free";
}

TEST(SectionStripSnapTest, TwoBarSnapMakesTwoBarMultiples) {
    const auto span = makeDragSpan(1.0, 9.6, stripGrid(8.0, 4.0, false), {});
    EXPECT_DOUBLE_EQ(span.start, 0.0);
    EXPECT_DOUBLE_EQ(span.end, 16.0);
    EXPECT_DOUBLE_EQ(makeDragSpan(1.0, 1.2, 8.0, {}).length(), 8.0);
}

TEST(SectionStripSnapTest, SnapBeatWithShiftGivesATenBeatSpanFromBarOneToTheMiddleOfBarThree) {
    const auto span = makeDragSpan(0.0, 9.6, kBeat, {});
    EXPECT_DOUBLE_EQ(span.start, 0.0);
    EXPECT_DOUBLE_EQ(span.end, 10.0) << "the end ceils to the beat: 2.5 bars in 4/4";
    EXPECT_DOUBLE_EQ(makeDragSpan(0.0, 0.2, kBeat, {}).length(), kBeat) << "a click is one snap unit";
}

TEST(SectionStripSnapTest, FreePlacementKeepsRawBeatsAndATinyMinimum) {
    const auto span = makeDragSpan(1.25, 9.6, 0.0, {});
    EXPECT_DOUBLE_EQ(span.start, 1.25);
    EXPECT_DOUBLE_EQ(span.end, 9.6);
    EXPECT_DOUBLE_EQ(makeDragSpan(3.0, 3.0, 0.0, {}).length(), kMinFreeSpanBeats);
    EXPECT_TRUE(spansAtLeastOneUnit({3.0, 3.0 + kMinFreeSpanBeats}, 0.0));
    EXPECT_FALSE(spansAtLeastOneUnit({3.0, 3.0 + kMinFreeSpanBeats / 2}, 0.0));
}

TEST(SectionStripSnapTest, ADropFollowsTheGridAndFreeWhenThereIsNone) {
    EXPECT_DOUBLE_EQ(dropBeat(5.4, 8.0, kBeat, 40.0, {}), 5.0);
    EXPECT_DOUBLE_EQ(dropBeat(5.6, 8.0, kBeat, 40.0, {}), 6.0);
    EXPECT_DOUBLE_EQ(dropBeat(5.4, 8.0, 4.0, 40.0, {}), 4.0);
    EXPECT_DOUBLE_EQ(dropBeat(5.4, 8.0, 0.0, 40.0, {}), 5.4);
    const std::vector<PlacedSpan> placed{{1, 0.0, 8.0}, {2, 8.0, 8.0}};
    const auto plan = planSectionDrop(placed, 1, 17.6, false, true, kBeat, 40.0);
    EXPECT_DOUBLE_EQ(plan.landing, 18.0) << "a plan on a beat grid lands on the beat";
    const auto barPlan = planSectionDrop(placed, 1, 17.6, false, true, stripGrid(kBeat, 4.0, false), 40.0);
    EXPECT_DOUBLE_EQ(barPlan.landing, 16.0) << "a drop uses max(Snap, bar): no Shift override";
}

TEST(SectionStripSnapTest, MakeRoomWithATenBeatSectionShiftsLaterMaterialByExactlyTen) {
    synth::TimelineDoc doc;
    const auto track = doc.addTrack(synth::TrackKind::Midi, "Lead");
    doc.addClip(track, 8.0, 4.0, "Verse clip");
    const auto tag = doc.makeSection(0.0, 10.0, "Tag", 0xff56D4C0, 0.5);
    const auto verse = doc.makeSection(10.0, 18.0, "Verse", 0xff4FC1FF, 0.5);
    ASSERT_TRUE(tag.isValid());
    ASSERT_TRUE(verse.isValid());
    const auto before = doc.getPlacements()[1].startBeat;
    ASSERT_NE(doc.placeSection(tag, 10.0, synth::SectionDropMode::MakeRoom, 0.5).value, 0);
    for (const auto& placement : doc.getPlacements())
        if (placement.section == verse)
            EXPECT_DOUBLE_EQ(placement.startBeat, before + 10.0);
}

TEST(TimelineSectionStripSnapTest, SnapBeatPlainMouseDragMakesWholeBars) {
    StripFixture f;
    f.state.snap = TimelineViewState::Snap::Quarter;
    f.dragAcross(0.0, 9.6);
    ASSERT_EQ(f.doc.getSections().size(), 1u);
    EXPECT_DOUBLE_EQ(f.doc.getPlacements()[0].startBeat, 0.0);
    EXPECT_DOUBLE_EQ(f.doc.getSections()[0].lengthBeats, 12.0)
        << "bar 1 to the middle of bar 3 rounds out to bar 3's end";
}

TEST(TimelineSectionStripSnapTest, SnapBeatShiftMouseDragFollowsTheBeat) {
    StripFixture f;
    f.state.snap = TimelineViewState::Snap::Quarter;
    const juce::Point<float> from{f.x(0.0), 12.0f}, to{f.x(9.6), 12.0f};
    f.strip.mouseDown(f.event(from, from, false, kShift));
    f.strip.mouseDrag(f.event(to, from, true, kShift));
    f.strip.mouseUp(f.event(to, from, true, kShift));
    ASSERT_EQ(f.doc.getSections().size(), 1u);
    EXPECT_DOUBLE_EQ(f.doc.getSections()[0].lengthBeats, 10.0);
}

TEST(TimelineSectionStripSnapTest, SnapBarMouseDragIsUnchanged) {
    StripFixture f;
    f.dragAcross(0.0, 9.6);
    ASSERT_EQ(f.doc.getSections().size(), 1u);
    EXPECT_DOUBLE_EQ(f.doc.getSections()[0].lengthBeats, 12.0);
}

TEST(TimelineSectionStripSnapTest, ShiftWithSnapOffPlacesItFreely) {
    StripFixture f;
    f.state.snap = TimelineViewState::Snap::Off;
    const juce::Point<float> from{f.x(1.25), 12.0f}, to{f.x(9.5), 12.0f};
    f.strip.mouseDown(f.event(from, from, false, kShift));
    f.strip.mouseDrag(f.event(to, from, true, kShift));
    const auto shown = f.strip.getMakeDragSpanForTest();
    EXPECT_NEAR(shown.start, 1.25, 1e-6);
    EXPECT_NEAR(shown.end, 9.5, 1e-6);
    f.strip.mouseUp(f.event(to, from, true, kShift));
    ASSERT_EQ(f.doc.getSections().size(), 1u);
    EXPECT_NEAR(f.doc.getPlacements()[0].startBeat, 1.25, 1e-6);
    EXPECT_NEAR(f.doc.getSections()[0].lengthBeats, 8.25, 1e-6);
}

TEST(TimelineSectionStripSnapTest, SnapOffWithoutShiftMakesWholeBars) {
    StripFixture f;
    f.state.snap = TimelineViewState::Snap::Off;
    f.dragAcross(2.5, 7.0);
    ASSERT_EQ(f.doc.getSections().size(), 1u);
    EXPECT_NEAR(f.doc.getPlacements()[0].startBeat, 0.0, 1e-6);
    EXPECT_NEAR(f.doc.getSections()[0].lengthBeats, 8.0, 1e-6);
}
