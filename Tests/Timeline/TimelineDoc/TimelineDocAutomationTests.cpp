// TimelineDoc automation-lane tests: lane dedup/validation/removal, breakpoint sort/replace/clamp, and range-clamped
// values on load.

#include "TimelineDocTestHelpers.h"

TEST_F(TimelineDocTest, AddLaneDedupesOnNodeUuidAndParamId) {
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto revisionBefore = doc.getRevision();

    const auto first = doc.addLane(track, "uuid-filter", "cutoff", makeRange(20.0f, 20000.0f, 1000.0f));
    ASSERT_TRUE(first.isValid());
    EXPECT_EQ(doc.getRevision(), revisionBefore + 1);

    // Same pair, different range: the existing lane wins, nothing is mutated.
    const auto again = doc.addLane(track, "uuid-filter", "cutoff", makeRange(0.0f, 1.0f, 0.0f));
    EXPECT_EQ(again, first);
    EXPECT_EQ(doc.getRevision(), revisionBefore + 1);
    EXPECT_EQ(doc.getTrack(track)->lanes.size(), 1u);
    EXPECT_FLOAT_EQ(doc.getLane(first)->range.maxValue, 20000.0f);

    // Dedupe is doc-wide: the same parameter on another track resolves to the same lane.
    const auto other = doc.addTrack(TrackKind::Midi, "Other");
    EXPECT_EQ(doc.addLane(other, "uuid-filter", "cutoff", makeRange(20.0f, 20000.0f, 1000.0f)), first);
    EXPECT_TRUE(doc.getTrack(other)->lanes.empty());
    EXPECT_EQ(doc.getRevision(), revisionBefore + 2); // +1 for the addTrack only

    // A different parameter on the same node is a different lane.
    const auto res = doc.addLane(track, "uuid-filter", "resonance", makeRange(0.0f, 1.0f, 0.5f));
    EXPECT_NE(res, first);
    EXPECT_EQ(doc.getLaneForParam("uuid-filter", "resonance")->id, res);
    EXPECT_EQ(doc.getLaneForParam("uuid-filter", "nope"), nullptr);
}

TEST_F(TimelineDocTest, InvalidLanesAreRejected) {
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto revisionBefore = doc.getRevision();
    EXPECT_FALSE(doc.addLane(track, "", "cutoff", makeRange(0.0f, 1.0f, 0.0f)).isValid());
    EXPECT_FALSE(doc.addLane(track, "uuid", "", makeRange(0.0f, 1.0f, 0.0f)).isValid());
    EXPECT_FALSE(doc.addLane(track, "uuid", "cutoff", makeRange(1.0f, 0.0f, 0.0f)).isValid()); // min > max
    EXPECT_FALSE(doc.addLane(TrackId{999}, "uuid", "cutoff", makeRange(0.0f, 1.0f, 0.0f)).isValid());
    EXPECT_FALSE(doc.removeLane(LaneId{999}));
    EXPECT_EQ(doc.getRevision(), revisionBefore);
}

TEST_F(TimelineDocTest, RemoveLaneFreesTheParameterBinding) {
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto lane = doc.addLane(track, "uuid", "cutoff", makeRange(0.0f, 1.0f, 0.0f));
    ASSERT_TRUE(doc.removeLane(lane));
    EXPECT_EQ(doc.getLaneForParam("uuid", "cutoff"), nullptr);
    const auto fresh = doc.addLane(track, "uuid", "cutoff", makeRange(0.0f, 1.0f, 0.0f));
    EXPECT_TRUE(fresh.isValid());
    EXPECT_NE(fresh, lane);
}

// ------------------------------------------------------------ 7. breakpoints --

TEST_F(TimelineDocTest, BreakpointsSortReplaceAndClamp) {
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto lane = doc.addLane(track, "uuid", "cutoff", makeRange(100.0f, 1000.0f, 500.0f));

    ASSERT_TRUE(doc.addBreakpoint(lane, 4.0, 800.0));
    ASSERT_TRUE(doc.addBreakpoint(lane, 0.0, 200.0));
    ASSERT_TRUE(doc.addBreakpoint(lane, 2.0, 5000.0));  // above the range: clamped
    ASSERT_TRUE(doc.addBreakpoint(lane, 6.0, -5000.0)); // below the range: clamped

    const auto& points = doc.getLane(lane)->points;
    ASSERT_EQ(points.size(), 4u);
    EXPECT_DOUBLE_EQ(points[0].beat, 0.0);
    EXPECT_DOUBLE_EQ(points[1].beat, 2.0);
    EXPECT_DOUBLE_EQ(points[2].beat, 4.0);
    EXPECT_DOUBLE_EQ(points[3].beat, 6.0);
    EXPECT_DOUBLE_EQ(points[1].value, 1000.0);
    EXPECT_DOUBLE_EQ(points[3].value, 100.0);

    // Same beat replaces rather than appending.
    const auto revisionBefore = doc.getRevision();
    ASSERT_TRUE(doc.addBreakpoint(lane, 2.0, 300.0, 0.5f, static_cast<int>(BreakpointCurve::Hold)));
    EXPECT_EQ(doc.getLane(lane)->points.size(), 4u);
    EXPECT_DOUBLE_EQ(doc.getLane(lane)->points[1].value, 300.0);
    EXPECT_FLOAT_EQ(doc.getLane(lane)->points[1].tension, 0.5f);
    EXPECT_EQ(doc.getLane(lane)->points[1].curve, static_cast<int>(BreakpointCurve::Hold));
    EXPECT_EQ(doc.getRevision(), revisionBefore + 1);

    ASSERT_TRUE(doc.removeBreakpoint(lane, 2.0));
    EXPECT_EQ(doc.getLane(lane)->points.size(), 3u);
    EXPECT_FALSE(doc.removeBreakpoint(lane, 2.0));
    EXPECT_FALSE(doc.removeBreakpoint(LaneId{999}, 0.0));
}

TEST_F(TimelineDocTest, InvalidBreakpointsAreRejected) {
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto lane = doc.addLane(track, "uuid", "cutoff", makeRange(0.0f, 1.0f, 0.0f));
    const auto revisionBefore = doc.getRevision();

    EXPECT_FALSE(doc.addBreakpoint(lane, -1.0, 0.5));
    EXPECT_FALSE(doc.addBreakpoint(lane, 0.0, 0.5, 0.0f, 3)); // curve beyond Bezier
    EXPECT_FALSE(doc.addBreakpoint(lane, 0.0, 0.5, 0.0f, -1));
    EXPECT_FALSE(doc.addBreakpoint(LaneId{999}, 0.0, 0.5));
    EXPECT_TRUE(doc.getLane(lane)->points.empty());
    EXPECT_EQ(doc.getRevision(), revisionBefore);

    // Tension is clamped rather than rejected — it's a shape hint, not structure.
    ASSERT_TRUE(doc.addBreakpoint(lane, 0.0, 0.5, 9.0f));
    EXPECT_FLOAT_EQ(doc.getLane(lane)->points[0].tension, 1.0f);
}

// -------------------------------------------------------------- 8. listeners --

TEST_F(TimelineDocTest, FromVarClampsBreakpointValuesToTheRangeSnapshot) {
    const auto* text = R"({"version":1,"tracks":[{"id":1,"lanes":[{"id":1,"nodeUuid":"u","paramId":"p",
        "range":{"minValue":100.0,"maxValue":1000.0,"defaultValue":500.0},
        "points":[{"beat":0.0,"value":99999.0},{"beat":1.0,"value":-99999.0}]}]}]})";
    ASSERT_TRUE(doc.fromVar(juce::JSON::parse(text)));

    const auto& points = doc.getTracks()[0].lanes[0].points;
    ASSERT_EQ(points.size(), 2u);
    EXPECT_DOUBLE_EQ(points[0].value, 1000.0);
    EXPECT_DOUBLE_EQ(points[1].value, 100.0);
}

// ------------------------------------------------ 13. audio clip fields --
//
// The audio half of Clip: an asset reference that must stay inside the bundle, a gain, two fades
// and a source offset. Everything here is ADDITIVE — kFormatVersion stays 1, an absent field loads
// as its default, and the path rule is enforced identically by the mutation API and by fromVar.
