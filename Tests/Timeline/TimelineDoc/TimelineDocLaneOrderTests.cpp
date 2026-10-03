// TimelineDoc::moveLane: a lane changes its place within its own track only, the index is where it ends up (clamped),
// a move that changes nothing is not a mutation, and the order survives a save and a load.

#include "TimelineDocTestHelpers.h"

namespace {
struct LaneOrderFixture : TimelineDocTest {
    TrackId track, other;
    LaneId a, b, c, otherLane;

    void SetUp() override {
        TimelineDocTest::SetUp();
        track = doc.addTrack(TrackKind::Midi, "T");
        other = doc.addTrack(TrackKind::Midi, "Other");
        a = doc.addLane(track, "u-a", "a", makeRange(0.0f, 1.0f, 0.0f));
        b = doc.addLane(track, "u-b", "b", makeRange(0.0f, 1.0f, 0.0f));
        c = doc.addLane(track, "u-c", "c", makeRange(0.0f, 1.0f, 0.0f));
        otherLane = doc.addLane(other, "u-o", "o", makeRange(0.0f, 1.0f, 0.0f));
        doc.addBreakpoint(a, 1.0, 0.5);
    }

    std::vector<LaneId> order(TrackId t) {
        std::vector<LaneId> ids;
        for (const auto& lane : doc.getTrack(t)->lanes)
            ids.push_back(lane.id);
        return ids;
    }
};
} // namespace

TEST_F(LaneOrderFixture, TheIndexIsWhereTheLaneEndsUpWhicheverWayItMoved) {
    ASSERT_TRUE(doc.moveLane(a, 2));
    EXPECT_EQ(order(track), (std::vector<LaneId>{b, c, a}));
    ASSERT_TRUE(doc.moveLane(a, 0));
    EXPECT_EQ(order(track), (std::vector<LaneId>{a, b, c}));
    ASSERT_TRUE(doc.moveLane(c, 1));
    EXPECT_EQ(order(track), (std::vector<LaneId>{a, c, b}));
}

TEST_F(LaneOrderFixture, TheIndexIsClampedAndAMoveThatChangesNothingIsNotAMutation) {
    ASSERT_TRUE(doc.moveLane(a, 99));
    EXPECT_EQ(order(track), (std::vector<LaneId>{b, c, a})) << "clamped to the last place";
    ASSERT_TRUE(doc.moveLane(a, -5));
    EXPECT_EQ(order(track), (std::vector<LaneId>{a, b, c})) << "clamped to the first place";

    const auto revision = doc.getRevision();
    const auto calls = listener.calls;
    EXPECT_FALSE(doc.moveLane(b, 1)) << "already there";
    EXPECT_FALSE(doc.moveLane(c, 50)) << "clamps onto where it is";
    EXPECT_FALSE(doc.moveLane(LaneId{999}, 0));
    EXPECT_EQ(doc.getRevision(), revision);
    EXPECT_EQ(listener.calls, calls);
}

TEST_F(LaneOrderFixture, OneMoveIsOneMutationAndOtherTracksAreUntouched) {
    const auto revision = doc.getRevision();
    ASSERT_TRUE(doc.moveLane(a, 2));
    EXPECT_EQ(doc.getRevision(), revision + 1);
    EXPECT_EQ(order(other), (std::vector<LaneId>{otherLane}));
    EXPECT_EQ(doc.getLane(a)->points.size(), 1u) << "the lane keeps its points";
    EXPECT_EQ(doc.getTrackForLane(a)->id, track);
}

TEST_F(LaneOrderFixture, TheOrderSurvivesASaveAndALoad) {
    ASSERT_TRUE(doc.moveLane(a, 2));
    ASSERT_TRUE(doc.moveLane(c, 0));
    const auto expected = order(track);

    TimelineDoc loaded;
    ASSERT_TRUE(loaded.fromVar(doc.toVar()));

    std::vector<LaneId> got;
    for (const auto& lane : loaded.getTrack(track)->lanes)
        got.push_back(lane.id);
    EXPECT_EQ(got, expected);
}
