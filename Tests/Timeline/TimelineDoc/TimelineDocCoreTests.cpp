// TimelineDoc core tests: fresh-doc invariants, listener notification, clear(), the document-wide caps, and the pinned
// serialised enum values.

#include "TimelineDocTestHelpers.h"

TEST_F(TimelineDocTest, FreshDocIsEmptyAtRevisionZero) {
    EXPECT_TRUE(doc.isEmpty());
    EXPECT_TRUE(doc.getTracks().empty());
    EXPECT_EQ(doc.getRevision(), 0);
    EXPECT_EQ(listener.calls, 0);
    EXPECT_FALSE(TrackId{}.isValid());
    EXPECT_FALSE(ClipId{}.isValid());
    EXPECT_FALSE(LaneId{}.isValid());
}

// ------------------------------------------------------------------- 2. ids ---

TEST(TimelineDocEnums, SerialisedValuesArePinned) {
    EXPECT_EQ(static_cast<int>(TrackKind::Midi), 0);
    EXPECT_EQ(static_cast<int>(TrackKind::Audio), 1);
    EXPECT_EQ(static_cast<int>(TrackKind::Automation), 2);
    EXPECT_EQ(static_cast<int>(BreakpointCurve::Hold), 0);
    EXPECT_EQ(static_cast<int>(BreakpointCurve::Linear), 1);
    EXPECT_EQ(static_cast<int>(BreakpointCurve::Bezier), 2);
}

TEST_F(TimelineDocTest, ListenerFiresExactlyOncePerEffectiveMutation) {
    EXPECT_EQ(listener.calls, 0);

    const auto track = doc.addTrack(TrackKind::Midi, "T");
    EXPECT_EQ(listener.calls, 1);
    const auto clip = doc.addClip(track, 0.0, 4.0, "c");
    EXPECT_EQ(listener.calls, 2);
    ASSERT_TRUE(doc.addNote(clip, makeNote(0.0, 60)).isValid());
    EXPECT_EQ(listener.calls, 3);
    const auto lane = doc.addLane(track, "uuid", "cutoff", makeRange(0.0f, 1.0f, 0.0f));
    EXPECT_EQ(listener.calls, 4);
    ASSERT_TRUE(doc.addBreakpoint(lane, 0.0, 0.5));
    EXPECT_EQ(listener.calls, 5);

    // No-ops and rejections: silent.
    EXPECT_TRUE(doc.setTrackName(track, "T"));    // same name
    EXPECT_TRUE(doc.setTrackMuted(track, false)); // already unmuted
    EXPECT_TRUE(doc.moveClip(clip, 0.0));         // already there
    EXPECT_TRUE(doc.resizeClip(clip, 4.0));
    EXPECT_EQ(doc.addLane(track, "uuid", "cutoff", makeRange(0.0f, 1.0f, 0.0f)), lane);
    EXPECT_FALSE(doc.addNote(clip, makeNote(0.0, 200)).isValid());
    EXPECT_FALSE(doc.setTrackName(TrackId{999}, "ghost"));
    doc.clear();
    EXPECT_EQ(listener.calls, 6); // only the clear() did anything
    doc.clear();                  // already empty
    EXPECT_EQ(listener.calls, 6);

    EXPECT_EQ(doc.getRevision(), static_cast<std::int64_t>(listener.calls));
}

TEST_F(TimelineDocTest, RemovedListenerStopsHearingChanges) {
    doc.removeListener(&listener);
    doc.addTrack(TrackKind::Midi, "T");
    EXPECT_EQ(listener.calls, 0);
    EXPECT_EQ(doc.getRevision(), 1);
    doc.addListener(&listener);
}

TEST_F(TimelineDocTest, ClearKeepsIdCountersMovingForward) {
    const auto first = doc.addTrack(TrackKind::Midi, "T");
    doc.clear();
    const auto second = doc.addTrack(TrackKind::Midi, "T2");
    EXPECT_GT(second.value, first.value);
}

// ------------------------------------------------------------------- 9. caps --

TEST_F(TimelineDocTest, TrackCapIsEnforced) {
    for (int i = 0; i < TimelineDoc::kMaxTracks; ++i)
        ASSERT_TRUE(doc.addTrack(TrackKind::Midi, "T").isValid()) << "track " << i;
    EXPECT_EQ(static_cast<int>(doc.getTracks().size()), TimelineDoc::kMaxTracks);

    EXPECT_FALSE(doc.addTrack(TrackKind::Midi, "one too many").isValid());
    EXPECT_EQ(static_cast<int>(doc.getTracks().size()), TimelineDoc::kMaxTracks);
    EXPECT_EQ(doc.getRevision(), TimelineDoc::kMaxTracks);
    EXPECT_EQ(listener.calls, TimelineDoc::kMaxTracks);
}

TEST_F(TimelineDocTest, ClipNoteLaneAndBreakpointCapsAreEnforced) {
    const auto track = doc.addTrack(TrackKind::Midi, "T");

    const auto clip = doc.addClip(track, 0.0, 1.0, "c");
    for (int i = 0; i < TimelineDoc::kMaxNotesPerClip; ++i)
        ASSERT_TRUE(doc.addNote(clip, makeNote(static_cast<double>(i), 60)).isValid()) << "note " << i;
    EXPECT_FALSE(doc.addNote(clip, makeNote(1e9, 60)).isValid());
    EXPECT_EQ(static_cast<int>(doc.getClip(clip)->notes.size()), TimelineDoc::kMaxNotesPerClip);

    for (int i = 1; i < TimelineDoc::kMaxClipsPerTrack; ++i)
        ASSERT_TRUE(doc.addClip(track, static_cast<double>(i), 1.0, "c").isValid()) << "clip " << i;
    EXPECT_FALSE(doc.addClip(track, 1e9, 1.0, "one too many").isValid());
    EXPECT_EQ(static_cast<int>(doc.getTrack(track)->clips.size()), TimelineDoc::kMaxClipsPerTrack);

    for (int i = 0; i < TimelineDoc::kMaxLanesPerTrack; ++i)
        ASSERT_TRUE(doc.addLane(track, "uuid", "param" + juce::String(i), makeRange(0.0f, 1.0f, 0.0f)).isValid())
            << "lane " << i;
    EXPECT_FALSE(doc.addLane(track, "uuid", "one too many", makeRange(0.0f, 1.0f, 0.0f)).isValid());
    EXPECT_EQ(static_cast<int>(doc.getTrack(track)->lanes.size()), TimelineDoc::kMaxLanesPerTrack);

    const auto lane = doc.getTrack(track)->lanes.front().id;
    for (int i = 0; i < TimelineDoc::kMaxBreakpointsPerLane; ++i)
        ASSERT_TRUE(doc.addBreakpoint(lane, static_cast<double>(i), 0.5)) << "breakpoint " << i;
    EXPECT_FALSE(doc.addBreakpoint(lane, 1e9, 0.5));
    // Replacing an existing beat is still allowed at the cap — it doesn't grow the lane.
    EXPECT_TRUE(doc.addBreakpoint(lane, 0.0, 0.25));
    EXPECT_EQ(static_cast<int>(doc.getLane(lane)->points.size()), TimelineDoc::kMaxBreakpointsPerLane);
}

// ------------------------------------------------------------ 10. round trip --
