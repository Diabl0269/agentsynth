// TimelineDoc track tests: add/remove/move/rename/colour/mute/solo/arm/binding, the arrangement-end-beat aggregate, and
// the Audio track kind.

#include "TimelineDocTestHelpers.h"

TEST_F(TimelineDocTest, AddTrackAssignsIncreasingIds) {
    const auto first = doc.addTrack(TrackKind::Midi, "One");
    const auto second = doc.addTrack(TrackKind::Midi, "Two");
    ASSERT_TRUE(first.isValid());
    ASSERT_TRUE(second.isValid());
    EXPECT_LT(first.value, second.value);
    EXPECT_EQ(doc.getTracks().size(), 2u);
    EXPECT_EQ(doc.getRevision(), 2);
}

TEST_F(TimelineDocTest, RemovedTrackIdIsNeverReused) {
    const auto first = doc.addTrack(TrackKind::Midi, "One");
    ASSERT_TRUE(doc.removeTrack(first));
    EXPECT_EQ(doc.getTrack(first), nullptr);

    const auto second = doc.addTrack(TrackKind::Midi, "Two");
    EXPECT_NE(second, first);
    EXPECT_GT(second.value, first.value);
    EXPECT_FALSE(doc.removeTrack(first)); // already gone: rejected, no bump
    EXPECT_FALSE(doc.setTrackName(first, "x"));
    EXPECT_EQ(doc.getRevision(), 3);
}

TEST_F(TimelineDocTest, RemovingATrackRemovesItsClipsAndLanes) {
    const auto track = doc.addTrack(TrackKind::Midi, "One");
    const auto clip = doc.addClip(track, 0.0, 4.0, "clip");
    const auto lane = doc.addLane(track, "uuid", "param", makeRange(0.0f, 1.0f, 0.0f));
    ASSERT_TRUE(doc.removeTrack(track));
    EXPECT_EQ(doc.getClip(clip), nullptr);
    EXPECT_EQ(doc.getLane(lane), nullptr);
    // The parameter is free again, so a new lane for it gets a fresh id.
    const auto other = doc.addTrack(TrackKind::Midi, "Two");
    const auto reborn = doc.addLane(other, "uuid", "param", makeRange(0.0f, 1.0f, 0.0f));
    EXPECT_NE(reborn, lane);
}

// ------------------------------------------------------------ 2b. moveTrack ---

TEST_F(TimelineDocTest, MoveTrackReordersWithoutTouchingIdentityOrContent) {
    const auto a = doc.addTrack(TrackKind::Midi, "A");
    const auto b = doc.addTrack(TrackKind::Midi, "B");
    const auto c = doc.addTrack(TrackKind::Midi, "C");
    const auto d = doc.addTrack(TrackKind::Midi, "D");
    doc.addClip(a, 0.0, 4.0, "clip-on-a");
    const int revisionBefore = doc.getRevision();
    const int callsBefore = listener.calls;

    ASSERT_TRUE(doc.moveTrack(a, 2)); // A: [A,B,C,D] -> [B,C,A,D]

    const auto& tracks = doc.getTracks();
    ASSERT_EQ(tracks.size(), 4u);
    EXPECT_EQ(tracks[0].id, b);
    EXPECT_EQ(tracks[1].id, c);
    EXPECT_EQ(tracks[2].id, a);
    EXPECT_EQ(tracks[3].id, d);

    // Identity and content travel with the track, not the slot.
    EXPECT_EQ(tracks[2].name, "A");
    ASSERT_EQ(tracks[2].clips.size(), 1u);
    EXPECT_EQ(tracks[2].clips[0].name, "clip-on-a");

    EXPECT_GT(doc.getRevision(), revisionBefore);
    EXPECT_GT(listener.calls, callsBefore);
}

TEST_F(TimelineDocTest, MoveTrackTowardTheFrontShiftsTheOthersDown) {
    const auto a = doc.addTrack(TrackKind::Midi, "A");
    const auto b = doc.addTrack(TrackKind::Midi, "B");
    const auto c = doc.addTrack(TrackKind::Midi, "C");
    const auto d = doc.addTrack(TrackKind::Midi, "D");

    ASSERT_TRUE(doc.moveTrack(c, 0)); // [A,B,C,D] -> [C,A,B,D]

    const auto& tracks = doc.getTracks();
    EXPECT_EQ(tracks[0].id, c);
    EXPECT_EQ(tracks[1].id, a);
    EXPECT_EQ(tracks[2].id, b);
    EXPECT_EQ(tracks[3].id, d);
}

TEST_F(TimelineDocTest, MoveTrackClampsAnOutOfRangeIndex) {
    const auto a = doc.addTrack(TrackKind::Midi, "A");
    const auto b = doc.addTrack(TrackKind::Midi, "B");

    ASSERT_TRUE(doc.moveTrack(a, 1000)); // clamps to the last valid index (1)
    EXPECT_EQ(doc.getTracks()[0].id, b);
    EXPECT_EQ(doc.getTracks()[1].id, a);

    ASSERT_TRUE(doc.moveTrack(a, -50)); // clamps to 0
    EXPECT_EQ(doc.getTracks()[0].id, a);
    EXPECT_EQ(doc.getTracks()[1].id, b);
}

TEST_F(TimelineDocTest, MoveTrackToItsOwnSlotIsANoOp) {
    const auto a = doc.addTrack(TrackKind::Midi, "A");
    doc.addTrack(TrackKind::Midi, "B");
    const int revisionBefore = doc.getRevision();
    const int callsBefore = listener.calls;

    EXPECT_TRUE(doc.moveTrack(a, 0)); // already there
    EXPECT_EQ(doc.getRevision(), revisionBefore);
    EXPECT_EQ(listener.calls, callsBefore);
}

TEST_F(TimelineDocTest, MoveTrackRejectsAnUnresolvedId) {
    doc.addTrack(TrackKind::Midi, "A");
    const int revisionBefore = doc.getRevision();

    EXPECT_FALSE(doc.moveTrack(TrackId{}, 0));
    EXPECT_FALSE(doc.moveTrack(TrackId{12345}, 0));
    EXPECT_EQ(doc.getRevision(), revisionBefore);
}

// ------------------------------------------------------------ 3. enum values --

static_assert(static_cast<int>(TrackKind::Midi) == 0, "TrackKind values are file format");
static_assert(static_cast<int>(TrackKind::Audio) == 1, "TrackKind values are file format");
static_assert(static_cast<int>(TrackKind::Automation) == 2, "TrackKind values are file format");
static_assert(static_cast<int>(BreakpointCurve::Hold) == 0, "BreakpointCurve values are file format");
static_assert(static_cast<int>(BreakpointCurve::Linear) == 1, "BreakpointCurve values are file format");
static_assert(static_cast<int>(BreakpointCurve::Bezier) == 2, "BreakpointCurve values are file format");

TEST_F(TimelineDocTest, UnknownTrackKindIsRejected) {
    EXPECT_FALSE(doc.addTrack(static_cast<TrackKind>(7), "reserved").isValid());
    EXPECT_EQ(doc.getRevision(), 0);
    EXPECT_EQ(listener.calls, 0);
}

// ------------------------------------------------------------------ 4. clips --

TEST_F(TimelineDocTest, ArrangementEndBeatIsZeroWithNoClips) {
    EXPECT_DOUBLE_EQ(doc.getArrangementEndBeat(), 0.0);

    doc.addTrack(TrackKind::Midi, "empty track");
    EXPECT_DOUBLE_EQ(doc.getArrangementEndBeat(), 0.0);
}

TEST_F(TimelineDocTest, ArrangementEndBeatIsTheLastClipEndAcrossTracks) {
    const auto lead = doc.addTrack(TrackKind::Midi, "Lead");
    const auto bass = doc.addTrack(TrackKind::Midi, "Bass");

    doc.addClip(lead, 0.0, 4.0, "A");  // ends at 4
    doc.addClip(bass, 2.0, 10.0, "B"); // ends at 12 - the longest
    doc.addClip(lead, 20.0, 1.0, "C"); // starts late but is short - ends at 21, still the max

    EXPECT_DOUBLE_EQ(doc.getArrangementEndBeat(), 21.0);
}

TEST_F(TimelineDocTest, AudioTrackKindIsFullyUsable) {
    const auto track = doc.addTrack(TrackKind::Audio, "Audio 1");
    ASSERT_TRUE(track.isValid());
    ASSERT_NE(doc.getTrack(track), nullptr);
    EXPECT_EQ(doc.getTrack(track)->kind, TrackKind::Audio);

    // Clips, arming and binding all work on an Audio track exactly as on a MIDI one — nothing in
    // the model is MIDI-only.
    const auto clip = doc.addClip(track, 4.0, 8.0, "Take");
    ASSERT_TRUE(clip.isValid());
    EXPECT_TRUE(doc.setTrackArmed(track, true));
    EXPECT_TRUE(doc.setTrackBinding(track, "uuid-audio-1"));
}
