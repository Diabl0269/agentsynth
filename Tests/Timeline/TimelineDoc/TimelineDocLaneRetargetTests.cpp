// TimelineDoc::retargetLane and duplicateLane: pointing a lane at another parameter keeps its curve's shape across
// ranges, never lets two lanes automate one parameter, and a duplicate lands directly below its source.

#include "TimelineDocTestHelpers.h"

namespace {
struct RetargetFixture : TimelineDocTest {
    TrackId track;
    LaneId lane;

    void SetUp() override {
        TimelineDocTest::SetUp();
        track = doc.addTrack(TrackKind::Midi, "T");
        lane = doc.addLane(track, "uuid-a", "cutoff", makeRange(0.0f, 100.0f, 50.0f));
        doc.addBreakpoint(lane, 0.0, 0.0);
        doc.addBreakpoint(lane, 1.0, 25.0);
        doc.addBreakpoint(lane, 2.0, 100.0, 0.5f, static_cast<int>(BreakpointCurve::Hold));
        doc.setLaneRecordMode(lane, static_cast<int>(synth::LaneRecordMode::Touch));
    }
};
} // namespace

TEST_F(RetargetFixture, RetargetRescalesEveryPointByItsPositionInTheRange) {
    const auto revision = doc.getRevision();
    ASSERT_TRUE(doc.retargetLane(lane, "uuid-b", "pitch", makeRange(-10.0f, 10.0f, 0.0f), 3));

    EXPECT_EQ(doc.getRevision(), revision + 1) << "one mutation";
    const auto* l = doc.getLane(lane);
    ASSERT_NE(l, nullptr);
    EXPECT_EQ(l->nodeUuid, "uuid-b");
    EXPECT_EQ(l->paramId, "pitch");
    EXPECT_EQ(l->paramIndexHint, 3);
    EXPECT_FLOAT_EQ(l->range.minValue, -10.0f);
    EXPECT_FLOAT_EQ(l->range.maxValue, 10.0f);
    ASSERT_EQ(l->points.size(), 3u);
    EXPECT_DOUBLE_EQ(l->points[0].value, -10.0);
    EXPECT_DOUBLE_EQ(l->points[1].value, -5.0); // a quarter of the way up either range
    EXPECT_DOUBLE_EQ(l->points[2].value, 10.0);
    EXPECT_DOUBLE_EQ(l->points[1].beat, 1.0);
    EXPECT_FLOAT_EQ(l->points[2].tension, 0.5f);
    EXPECT_EQ(l->points[2].curve, static_cast<int>(BreakpointCurve::Hold));
    EXPECT_EQ(l->recordMode, static_cast<int>(synth::LaneRecordMode::Touch));
    EXPECT_EQ(doc.getLaneForParam("uuid-a", "cutoff"), nullptr) << "the old parameter is free again";
    EXPECT_EQ(doc.getLaneForParam("uuid-b", "pitch"), l);
}

TEST_F(RetargetFixture, RetargetOntoAParameterThatHasAnotherLaneIsRefused) {
    const auto other = doc.addLane(track, "uuid-b", "pitch", makeRange(0.0f, 1.0f, 0.0f));
    ASSERT_TRUE(other.isValid());
    const auto revision = doc.getRevision();

    EXPECT_FALSE(doc.retargetLane(lane, "uuid-b", "pitch", makeRange(0.0f, 1.0f, 0.0f)));

    EXPECT_EQ(doc.getRevision(), revision);
    EXPECT_EQ(doc.getLane(lane)->paramId, "cutoff");
    EXPECT_EQ(doc.getLane(lane)->points.size(), 3u);
}

TEST_F(RetargetFixture, RetargetRejectsBadInputAndIsANoOpForTheSameBinding) {
    const auto revision = doc.getRevision();
    EXPECT_FALSE(doc.retargetLane(LaneId{999}, "u", "p", makeRange(0.0f, 1.0f, 0.0f)));
    EXPECT_FALSE(doc.retargetLane(lane, "", "p", makeRange(0.0f, 1.0f, 0.0f)));
    EXPECT_FALSE(doc.retargetLane(lane, "u", "", makeRange(0.0f, 1.0f, 0.0f)));
    EXPECT_FALSE(doc.retargetLane(lane, "u", "p", makeRange(1.0f, 0.0f, 0.0f)));
    EXPECT_TRUE(doc.retargetLane(lane, "uuid-a", "cutoff", makeRange(0.0f, 100.0f, 50.0f)));
    EXPECT_EQ(doc.getRevision(), revision) << "nothing changed, so nothing was published";
}

TEST_F(RetargetFixture, ALaneWithNoPointsTakesTheNewParametersDefaultAndAZeroWidthRangeMapsToIt) {
    const auto empty = doc.addLane(track, "uuid-c", "res", makeRange(0.0f, 1.0f, 0.2f));
    ASSERT_TRUE(doc.retargetLane(empty, "uuid-d", "gain", makeRange(-60.0f, 6.0f, 0.0f)));
    EXPECT_FLOAT_EQ(doc.getLane(empty)->range.defaultValue, 0.0f);

    const auto flat = doc.addLane(track, "uuid-e", "flat", makeRange(5.0f, 5.0f, 5.0f));
    doc.addBreakpoint(flat, 0.0, 5.0);
    ASSERT_TRUE(doc.retargetLane(flat, "uuid-f", "wide", makeRange(0.0f, 10.0f, 7.0f)));
    EXPECT_DOUBLE_EQ(doc.getLane(flat)->points[0].value, 7.0);
}

TEST_F(RetargetFixture, DuplicateLandsDirectlyBelowItsSourceWithTheSamePointsAndRecordMode) {
    const auto third = doc.addLane(track, "uuid-z", "last", makeRange(0.0f, 1.0f, 0.0f));
    const auto revision = doc.getRevision();

    const auto copy = doc.duplicateLane(lane, "uuid-b", "res", makeRange(0.0f, 100.0f, 50.0f));

    ASSERT_TRUE(copy.isValid());
    EXPECT_EQ(doc.getRevision(), revision + 1) << "one mutation";
    const auto& lanes = doc.getTrack(track)->lanes;
    ASSERT_EQ(lanes.size(), 3u);
    EXPECT_EQ(lanes[0].id, lane);
    EXPECT_EQ(lanes[1].id, copy) << "directly below the source, not at the end";
    EXPECT_EQ(lanes[2].id, third);
    const auto& source = *doc.getLane(lane);
    const auto& made = *doc.getLane(copy);
    ASSERT_EQ(made.points.size(), source.points.size());
    for (size_t i = 0; i < source.points.size(); ++i) {
        EXPECT_DOUBLE_EQ(made.points[i].beat, source.points[i].beat);
        EXPECT_DOUBLE_EQ(made.points[i].value, source.points[i].value);
        EXPECT_FLOAT_EQ(made.points[i].tension, source.points[i].tension);
        EXPECT_EQ(made.points[i].curve, source.points[i].curve);
    }
    EXPECT_EQ(made.recordMode, source.recordMode);
    EXPECT_EQ(source.paramId, "cutoff") << "the source is untouched";
}

TEST_F(RetargetFixture, DuplicateOntoABoundParameterOrWithBadInputCreatesNothing) {
    const auto revision = doc.getRevision();
    EXPECT_FALSE(doc.duplicateLane(lane, "uuid-a", "cutoff", makeRange(0.0f, 100.0f, 50.0f)).isValid());
    EXPECT_FALSE(doc.duplicateLane(LaneId{999}, "u", "p", makeRange(0.0f, 1.0f, 0.0f)).isValid());
    EXPECT_FALSE(doc.duplicateLane(lane, "", "p", makeRange(0.0f, 1.0f, 0.0f)).isValid());
    EXPECT_FALSE(doc.duplicateLane(lane, "u", "p", makeRange(1.0f, 0.0f, 0.0f)).isValid());
    EXPECT_EQ(doc.getRevision(), revision);
    EXPECT_EQ(doc.getTrack(track)->lanes.size(), 1u);
}

TEST_F(RetargetFixture, DuplicateRescalesOntoAParameterWithOtherBounds) {
    const auto copy = doc.duplicateLane(lane, "uuid-b", "pitch", makeRange(0.0f, 1.0f, 0.0f));
    ASSERT_TRUE(copy.isValid());
    EXPECT_DOUBLE_EQ(doc.getLane(copy)->points[1].value, 0.25);
    EXPECT_DOUBLE_EQ(doc.getLane(lane)->points[1].value, 25.0) << "the source keeps its own values";
}
