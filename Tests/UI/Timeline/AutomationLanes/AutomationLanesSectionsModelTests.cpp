// AutomationLanesSectionsModelTests.cpp -- the sections algebra (ModulatorSections.h) with no component: a
// lane's Hold breakpoints to and from blocks, the span edits a gesture makes, and the one doc mutation.

#include "UI/Timeline/AutomationLanes/Modulators/ModulatorSections.h"
#include <gtest/gtest.h>

using namespace synth::ui;

namespace {
SectionBlocks blocksOf(std::initializer_list<std::pair<double, double>> list) {
    SectionBlocks out;
    for (const auto& [a, b] : list)
        out.push_back({a, b});
    return out;
}
} // namespace

TEST(ModulatorSectionsModelTest, PointsAreAZeroAtBeatZeroThenAStartAndEndPerBlock) {
    const auto points = pointsFromSections(blocksOf({{32.0, 64.0}, {96.0, 128.0}}));
    ASSERT_EQ(points.size(), 5u);
    const double beats[] = {0.0, 32.0, 64.0, 96.0, 128.0};
    const double values[] = {0.0, 1.0, 0.0, 1.0, 0.0};
    for (size_t i = 0; i < points.size(); ++i) {
        EXPECT_EQ(points[i].beat, beats[i]) << i;
        EXPECT_EQ(points[i].value, values[i]) << i;
        EXPECT_EQ(points[i].curve, static_cast<int>(synth::BreakpointCurve::Hold)) << i;
    }
    EXPECT_TRUE(pointsFromSections({}).empty());
}

TEST(ModulatorSectionsModelTest, ABlockAtBeatZeroNeedsNoLeadingZero) {
    const auto points = pointsFromSections(blocksOf({{0.0, 8.0}}));
    ASSERT_EQ(points.size(), 2u);
    EXPECT_EQ(points[0].beat, 0.0);
    EXPECT_EQ(points[0].value, 1.0);
}

TEST(ModulatorSectionsModelTest, PointsReadBackAsTheSameBlocks) {
    const auto original = blocksOf({{0.0, 8.0}, {32.0, 64.0}, {96.0, 100.0}});
    EXPECT_EQ(sectionsFromPoints(pointsFromSections(original)), original);
    // A lane left on at its last point is on to the end of the song.
    const auto open = sectionsFromPoints({{0.0, 0.0, 0.0f, 0}, {16.0, 1.0, 0.0f, 0}});
    ASSERT_EQ(open.size(), 1u);
    EXPECT_EQ(open[0].start, 16.0);
    EXPECT_EQ(open[0].end, kOpenEnd);
    // A first point already on is on from the start (the kernel holds it back to beat 0).
    EXPECT_EQ(sectionsFromPoints({{8.0, 1.0, 0.0f, 0}, {16.0, 0.0, 0.0f, 0}})[0].start, 0.0);
}

TEST(ModulatorSectionsModelTest, OverlappingAndTouchingBlocksMerge) {
    EXPECT_EQ(normalisedSections(blocksOf({{32.0, 64.0}, {48.0, 80.0}})), blocksOf({{32.0, 80.0}}));
    EXPECT_EQ(normalisedSections(blocksOf({{32.0, 64.0}, {64.0, 80.0}})), blocksOf({{32.0, 80.0}})) << "touching";
    EXPECT_EQ(normalisedSections(blocksOf({{96.0, 100.0}, {0.0, 8.0}})), blocksOf({{0.0, 8.0}, {96.0, 100.0}}))
        << "sorted";
    EXPECT_TRUE(normalisedSections(blocksOf({{8.0, 8.0}, {9.0, 4.0}})).empty()) << "empty and inverted drop";
    EXPECT_EQ(paintedSpan(blocksOf({{32.0, 64.0}}), 60.0, 80.0), blocksOf({{32.0, 80.0}}));
}

TEST(ModulatorSectionsModelTest, ErasingSplitsShrinksAndRemoves) {
    const auto base = blocksOf({{32.0, 64.0}});
    EXPECT_EQ(erasedSpan(base, 40.0, 48.0), blocksOf({{32.0, 40.0}, {48.0, 64.0}})) << "splits";
    EXPECT_EQ(erasedSpan(base, 24.0, 40.0), blocksOf({{40.0, 64.0}})) << "shrinks from the left";
    EXPECT_EQ(erasedSpan(base, 56.0, 80.0), blocksOf({{32.0, 56.0}})) << "shrinks from the right";
    EXPECT_TRUE(erasedSpan(base, 0.0, 100.0).empty()) << "removes";
    EXPECT_EQ(erasedSpan(base, 64.0, 80.0), base) << "a span that only touches changes nothing";
}

TEST(ModulatorSectionsModelTest, ResizeAndMoveKeepBlocksSortedAndNeverCross) {
    const auto base = blocksOf({{32.0, 64.0}, {96.0, 128.0}});
    EXPECT_EQ(resizedBlock(base, 0, false, 80.0, 4.0), blocksOf({{32.0, 80.0}, {96.0, 128.0}}));
    EXPECT_EQ(resizedBlock(base, 0, false, 100.0, 4.0), blocksOf({{32.0, 128.0}})) << "grows into the next: merges";
    EXPECT_EQ(resizedBlock(base, 0, true, 100.0, 4.0), blocksOf({{60.0, 64.0}, {96.0, 128.0}}))
        << "the start edge stops a minimum length short of the end";
    EXPECT_EQ(resizedBlock(base, 1, false, 0.0, 4.0), blocksOf({{32.0, 64.0}, {96.0, 100.0}}));
    EXPECT_EQ(movedBlock(base, 0, 8.0), blocksOf({{8.0, 40.0}, {96.0, 128.0}}));
    EXPECT_EQ(movedBlock(base, 0, -10.0), blocksOf({{0.0, 32.0}, {96.0, 128.0}})) << "not before beat 0";
    EXPECT_EQ(movedBlock(base, 0, 80.0), blocksOf({{80.0, 128.0}})) << "moved onto the next: merges";
    EXPECT_EQ(withoutBlock(base, 0), blocksOf({{96.0, 128.0}}));
}

TEST(ModulatorSectionsModelTest, TheDescriptionNamesTheBarsAndSaysEverywhereWithNoLane) {
    EXPECT_EQ(describeSections({}, false, 4.0), "on everywhere");
    EXPECT_EQ(describeSections({}, true, 4.0), "off everywhere");
    const auto dash = juce::String::fromUTF8("\xE2\x80\x93");
    EXPECT_EQ(describeSections(blocksOf({{32.0, 64.0}, {96.0, 128.0}}), true, 4.0),
              "bars 9" + dash + "16, 25" + dash + "32");
    EXPECT_EQ(describeSections(blocksOf({{32.0, 36.0}}), true, 4.0), "bar 9");
    EXPECT_EQ(describeSections(blocksOf({{34.0, 40.0}}), true, 4.0), "bars 9.5" + dash + "10") << "off the bar line";
    EXPECT_EQ(describeSections(blocksOf({{96.0, kOpenEnd}}), true, 4.0), "bars 25 onward");
    EXPECT_EQ(describeSections(blocksOf({{24.0, 48.0}}), true, 3.0), "bars 9" + dash + "16") << "3/4 time";
}

TEST(ModulatorSectionsModelTest, ApplyCreatesTheLaneRewritesItAndRemovesItWithTheLastBlock) {
    synth::TimelineDoc doc;
    const auto track = doc.addTrack(synth::TrackKind::Midi, "Bass");
    EXPECT_FALSE(applySections(doc, track, "lfo-1", {})) << "nothing to remove";
    EXPECT_EQ(sectionsLaneFor(doc, "lfo-1"), nullptr);

    ASSERT_TRUE(applySections(doc, track, "lfo-1", blocksOf({{32.0, 64.0}})));
    const auto* lane = sectionsLaneFor(doc, "lfo-1");
    ASSERT_NE(lane, nullptr);
    EXPECT_EQ(lane->paramId, "level");
    EXPECT_EQ(doc.getTrackForLane(lane->id)->id, track);
    EXPECT_EQ(lane->recordMode, static_cast<int>(synth::LaneRecordMode::Read));
    EXPECT_EQ(lane->points.size(), 3u);

    ASSERT_TRUE(applySections(doc, track, "lfo-1", blocksOf({{8.0, 16.0}, {32.0, 64.0}})));
    EXPECT_EQ(sectionsLaneFor(doc, "lfo-1")->points.size(), 5u) << "rewritten in place, not duplicated";
    EXPECT_EQ(doc.getTracks().front().lanes.size(), 1u);

    ASSERT_TRUE(applySections(doc, track, "lfo-1", {}));
    EXPECT_EQ(sectionsLaneFor(doc, "lfo-1"), nullptr) << "no blocks: the lane goes, the modulator is on everywhere";
}
