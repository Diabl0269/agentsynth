// AutomationLanesAmountModelTests.cpp -- the amount lane's document side with no component (ModulatorAmountLane.h):
// the one write that creates, rewrites and removes the lane, the readout text; and the retired sections format
// read back into blocks and turned into amount-lane points for the load migration (ModulatorSections.h).

#include "UI/Timeline/AutomationLanes/Modulators/ModulatorAmountLane.h"
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorSections.h"
#include <gtest/gtest.h>

using namespace synth::ui;

namespace {
constexpr int kHold = static_cast<int>(synth::BreakpointCurve::Hold);

SectionBlocks blocksOf(std::initializer_list<std::pair<double, double>> list) {
    SectionBlocks out;
    for (const auto& [a, b] : list)
        out.push_back({a, b});
    return out;
}

synth::AutomationLane::Breakpoint hold(double beat, double value) { return {beat, value, 0.0f, kHold}; }

void expectPoints(const std::vector<synth::AutomationLane::Breakpoint>& points,
                  std::initializer_list<std::pair<double, double>> expected) {
    ASSERT_EQ(points.size(), expected.size());
    size_t i = 0;
    for (const auto& [beat, value] : expected) {
        EXPECT_EQ(points[i].beat, beat) << i;
        EXPECT_DOUBLE_EQ(points[i].value, value) << i;
        EXPECT_EQ(points[i].curve, kHold) << i;
        ++i;
    }
}
} // namespace

TEST(ModulatorAmountModelTest, WriteCreatesTheLaneInReadModeRewritesItAndRemovesItWithTheLastPoint) {
    synth::TimelineDoc doc;
    const auto track = doc.addTrack(synth::TrackKind::Midi, "Bass");
    EXPECT_EQ(amountLaneFor(doc, "atten"), nullptr);
    EXPECT_FALSE(writeAmountLane(doc, track, "atten", {})) << "no points and no lane: nothing to do";

    ASSERT_TRUE(writeAmountLane(doc, track, "atten", {hold(0.0, 0.5), hold(8.0, -0.25)}));
    const auto* lane = amountLaneFor(doc, "atten");
    ASSERT_NE(lane, nullptr);
    EXPECT_EQ(lane->paramId, "amount");
    EXPECT_EQ(lane->range.minValue, -1.0f);
    EXPECT_EQ(lane->range.maxValue, 1.0f);
    EXPECT_EQ(lane->range.defaultValue, 0.0f);
    EXPECT_EQ(lane->recordMode, static_cast<int>(synth::LaneRecordMode::Read));
    EXPECT_EQ(doc.getTrackForLane(lane->id)->id, track);
    expectPoints(lane->points, {{0.0, 0.5}, {8.0, -0.25}});

    ASSERT_TRUE(writeAmountLane(doc, track, "atten", {hold(4.0, 1.0)}));
    expectPoints(amountLaneFor(doc, "atten")->points, {{4.0, 1.0}}); // every old point replaced

    ASSERT_TRUE(writeAmountLane(doc, track, "atten", {}));
    EXPECT_EQ(amountLaneFor(doc, "atten"), nullptr) << "no points means no lane";
    EXPECT_EQ(amountLaneFor(doc, ""), nullptr);
}

TEST(ModulatorAmountModelTest, TheReadoutIsASignedPercentageWithARealMinus) {
    EXPECT_EQ(amountText(0.72), "+72%");
    EXPECT_EQ(amountText(1.0), "+100%");
    EXPECT_EQ(amountText(-0.3), juce::String::fromUTF8("\xE2\x88\x92") + "30%");
    EXPECT_EQ(amountText(0.0), "0%");
    EXPECT_EQ(amountText(0.004), "0%") << "rounds to whole percent, no +0%";
}

TEST(ModulatorAmountModelTest, ASectionsLaneReadsBackAsItsBlocks) {
    const auto read = sectionsFromPoints({hold(0.0, 0.0), hold(32.0, 1.0), hold(64.0, 0.0), hold(96.0, 1.0)});
    ASSERT_EQ(read.size(), 2u);
    EXPECT_EQ(read[0], (SectionBlock{32.0, 64.0}));
    EXPECT_EQ(read[1].start, 96.0);
    EXPECT_EQ(read[1].end, kOpenEnd) << "a lane left on at its last point is on to the end of the song";
    // A first point already on is on from the start (the kernel holds it back to beat 0).
    EXPECT_EQ(sectionsFromPoints({hold(8.0, 1.0), hold(16.0, 0.0)})[0].start, 0.0);
    EXPECT_EQ(normalisedSections(blocksOf({{32.0, 64.0}, {64.0, 80.0}, {8.0, 8.0}})), blocksOf({{32.0, 80.0}}))
        << "touching merge, empty drops";
}

TEST(ModulatorAmountModelTest, BlocksBecomeAmountPointsOnTheSameEdges) {
    expectPoints(amountPointsFromSections(blocksOf({{32.0, 64.0}, {96.0, 128.0}}), 0.5),
                 {{0.0, 0.0}, {32.0, 0.5}, {64.0, 0.0}, {96.0, 0.5}, {128.0, 0.0}});
    // A block at beat 0 needs no leading 0; an open block has no end point; off everywhere stays off.
    expectPoints(amountPointsFromSections(blocksOf({{0.0, 8.0}}), -0.75), {{0.0, -0.75}, {8.0, 0.0}});
    expectPoints(amountPointsFromSections(blocksOf({{16.0, kOpenEnd}}), 1.0), {{0.0, 0.0}, {16.0, 1.0}});
    expectPoints(amountPointsFromSections({}, 0.5), {{0.0, 0.0}});
}
