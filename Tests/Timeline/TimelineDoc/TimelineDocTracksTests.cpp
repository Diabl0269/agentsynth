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

TEST_F(TimelineDocTest, ResetTrackHeightScalesIsOneNotificationAndNoneWhenAllDefault) {
    const auto a = doc.addTrack(TrackKind::Midi, "A");
    const auto b = doc.addTrack(TrackKind::Midi, "B");
    CountingListener listener;
    doc.addListener(&listener);
    EXPECT_FALSE(doc.resetTrackHeightScales());
    EXPECT_EQ(listener.calls, 0);
    ASSERT_TRUE(doc.setTrackHeightScale(a, 2.0));
    ASSERT_TRUE(doc.setTrackHeightScale(b, 0.6));
    const int before = listener.calls;
    EXPECT_TRUE(doc.resetTrackHeightScales());
    EXPECT_EQ(listener.calls, before + 1);
    EXPECT_EQ(doc.getTrack(a)->heightScale, 1.0);
    EXPECT_EQ(doc.getTrack(b)->heightScale, 1.0);
}

// A track's own row height: display data saved with the project, clamped, and left out of the file
// at the default so older projects and fixtures stay byte-identical.
TEST_F(TimelineDocTest, SetTrackHeightScaleClampsAndNotifiesOnlyOnAChange) {
    const auto id = doc.addTrack(TrackKind::Midi, "Bass");
    CountingListener listener;
    doc.addListener(&listener);
    EXPECT_TRUE(doc.setTrackHeightScale(id, 1.0));
    EXPECT_EQ(listener.calls, 0) << "the value already stored: no notification";
    EXPECT_TRUE(doc.setTrackHeightScale(id, 2.0));
    EXPECT_EQ(doc.getTrack(id)->heightScale, 2.0);
    EXPECT_EQ(listener.calls, 1);
    EXPECT_TRUE(doc.setTrackHeightScale(id, 99.0));
    EXPECT_EQ(doc.getTrack(id)->heightScale, Track::kMaxHeightScale);
    EXPECT_TRUE(doc.setTrackHeightScale(id, 0.01));
    EXPECT_EQ(doc.getTrack(id)->heightScale, Track::kMinHeightScale);
    EXPECT_FALSE(doc.setTrackHeightScale(id, std::numeric_limits<double>::quiet_NaN()));
    EXPECT_FALSE(doc.setTrackHeightScale(TrackId{9999}, 2.0));
    doc.removeListener(&listener);
}

TEST_F(TimelineDocTest, TrackHeightScaleRoundTripsAndIsLeftOutAtTheDefault) {
    const auto tall = doc.addTrack(TrackKind::Midi, "Tall");
    const auto plain = doc.addTrack(TrackKind::Midi, "Plain");
    ASSERT_TRUE(doc.setTrackHeightScale(tall, 1.75));

    const auto var = doc.toVar();
    const auto& tracks = *var.getProperty("tracks", {}).getArray();
    EXPECT_EQ((double)tracks[0].getProperty("heightScale", {}), 1.75);
    EXPECT_FALSE(tracks[1].getDynamicObject()->hasProperty("heightScale")) << "default: not written";

    TimelineDoc loaded;
    ASSERT_TRUE(loaded.fromVar(var));
    EXPECT_EQ(loaded.getTrack(tall)->heightScale, 1.75);
    EXPECT_EQ(loaded.getTrack(plain)->heightScale, 1.0);
}

TEST_F(TimelineDocTest, ALoadedTrackHeightScaleIsClampedAndANonNumberIsRefused) {
    const auto id = doc.addTrack(TrackKind::Midi, "Bass");
    auto var = doc.toVar();
    var.getProperty("tracks", {}).getArray()->getReference(0).getDynamicObject()->setProperty("heightScale", 40.0);
    TimelineDoc loaded;
    ASSERT_TRUE(loaded.fromVar(var));
    EXPECT_EQ(loaded.getTrack(id)->heightScale, Track::kMaxHeightScale);

    var.getProperty("tracks", {}).getArray()->getReference(0).getDynamicObject()->setProperty("heightScale", "tall");
    TimelineDoc rejected;
    EXPECT_FALSE(rejected.fromVar(var));
}

// ------------------------------------------------------ duplicateTrack ---

namespace {

// A track with two clips (one with notes), a lane on node "n1" and one on node "shared".
TrackId buildDuplicableTrack(TimelineDoc& doc) {
    const auto track = doc.addTrack(TrackKind::Midi, "Lead");
    doc.setTrackBinding(track, "n1");
    doc.setTrackMuted(track, true);
    doc.setTrackSoloed(track, true);
    doc.setTrackArmed(track, true);
    doc.setTrackHeightScale(track, 2.0);
    const auto clip = doc.addClip(track, 4.0, 2.0, "Verse");
    doc.addNote(clip, makeNote(0.0, 60));
    doc.addNote(clip, makeNote(1.0, 64));
    doc.addClip(track, 12.0, 4.0, "Chorus");
    const auto lane = doc.addLane(track, "n1", "cutoff", makeRange(0.0f, 1.0f, 0.5f));
    doc.addBreakpoint(lane, 0.0, 0.25);
    doc.addBreakpoint(lane, 2.0, 0.75);
    doc.addLane(track, "shared", "rate", makeRange(0.0f, 1.0f, 0.0f));
    return track;
}

} // namespace

TEST_F(TimelineDocTest, DuplicateTrackLandsDirectlyBelowTheSourceWithFreshIdsAndTheSameContent) {
    const auto first = doc.addTrack(TrackKind::Midi, "First");
    const auto source = buildDuplicableTrack(doc);
    const auto last = doc.addTrack(TrackKind::Midi, "Last");

    const auto copy = doc.duplicateTrack(source, "Lead copy", 0xff102030, {{"n1", "n1-copy"}});
    ASSERT_TRUE(copy.isValid());

    ASSERT_EQ(doc.getTracks().size(), 4u);
    EXPECT_EQ(doc.getTracks()[0].id, first);
    EXPECT_EQ(doc.getTracks()[1].id, source);
    EXPECT_EQ(doc.getTracks()[2].id, copy);
    EXPECT_EQ(doc.getTracks()[3].id, last);

    const auto& original = *doc.getTrack(source);
    const auto& dup = *doc.getTrack(copy);
    EXPECT_EQ(dup.name, "Lead copy");
    EXPECT_EQ(dup.colourArgb, 0xff102030u);
    EXPECT_EQ(dup.kind, original.kind);
    EXPECT_TRUE(dup.muted);
    EXPECT_FALSE(dup.soloed) << "a copy never starts soloed";
    EXPECT_FALSE(dup.armed) << "a copy never starts armed";
    EXPECT_DOUBLE_EQ(dup.heightScale, 2.0);
    EXPECT_EQ(dup.bindingUuid, "n1-copy");

    ASSERT_EQ(dup.clips.size(), original.clips.size());
    for (size_t i = 0; i < dup.clips.size(); ++i) {
        EXPECT_NE(dup.clips[i].id, original.clips[i].id);
        EXPECT_EQ(dup.clips[i].name, original.clips[i].name);
        EXPECT_DOUBLE_EQ(dup.clips[i].startBeat, original.clips[i].startBeat);
        ASSERT_EQ(dup.clips[i].notes.size(), original.clips[i].notes.size());
        for (size_t n = 0; n < dup.clips[i].notes.size(); ++n) {
            EXPECT_NE(dup.clips[i].notes[n].id, original.clips[i].notes[n].id);
            EXPECT_EQ(dup.clips[i].notes[n].pitch, original.clips[i].notes[n].pitch);
        }
    }
}

TEST_F(TimelineDocTest, DuplicateTrackRebindsLanesThroughTheMapAndLeavesUnmappedOnesOff) {
    const auto source = buildDuplicableTrack(doc);
    const auto copy = doc.duplicateTrack(source, "Lead copy", 0xff102030, {{"n1", "n1-copy"}});
    ASSERT_TRUE(copy.isValid());

    const auto& dup = *doc.getTrack(copy);
    ASSERT_EQ(dup.lanes.size(), 1u) << "the lane on a shared node stays with the original: one lane per (node, param)";
    EXPECT_EQ(dup.lanes[0].nodeUuid, "n1-copy");
    EXPECT_EQ(dup.lanes[0].paramId, "cutoff");
    ASSERT_EQ(dup.lanes[0].points.size(), 2u);
    EXPECT_DOUBLE_EQ(dup.lanes[0].points[1].value, 0.75);
    EXPECT_NE(dup.lanes[0].id, doc.getTrack(source)->lanes[0].id);

    const auto& original = *doc.getTrack(source);
    EXPECT_EQ(original.lanes.size(), 2u) << "the original keeps every lane";
    EXPECT_EQ(original.lanes[0].nodeUuid, "n1");
    EXPECT_EQ(doc.getLaneForParam("n1", "cutoff")->id, original.lanes[0].id);
}

TEST_F(TimelineDocTest, DuplicateTrackIsOneMutation) {
    const auto source = buildDuplicableTrack(doc);
    CountingListener listener;
    doc.addListener(&listener);
    const int revisionBefore = doc.getRevision();

    ASSERT_TRUE(doc.duplicateTrack(source, "Lead copy", 0xff102030, {{"n1", "n1-copy"}}).isValid());
    EXPECT_EQ(doc.getRevision(), revisionBefore + 1);
    EXPECT_EQ(listener.calls, 1);
    doc.removeListener(&listener);
}

TEST_F(TimelineDocTest, DuplicateTrackRefusesAMissingSourceTheAutomationTrackAndAFullDoc) {
    EXPECT_FALSE(doc.duplicateTrack(TrackId{999}, "x", 0, {}).isValid());

    const auto automation = doc.addTrack(TrackKind::Automation, "Automation");
    EXPECT_FALSE(doc.duplicateTrack(automation, "x", 0, {}).isValid());

    const auto midi = doc.addTrack(TrackKind::Midi, "M");
    while (static_cast<int>(doc.getTracks().size()) < TimelineDoc::kMaxTracks)
        ASSERT_TRUE(doc.addTrack(TrackKind::Midi, "filler").isValid());
    const int revisionBefore = doc.getRevision();
    EXPECT_FALSE(doc.duplicateTrack(midi, "x", 0, {}).isValid());
    EXPECT_EQ(doc.getRevision(), revisionBefore) << "a refused duplicate changes nothing";
}

TEST_F(TimelineDocTest, DuplicateTrackKeepsTheAutomationTrackLast) {
    const auto midi = doc.addTrack(TrackKind::Midi, "M");
    doc.addTrack(TrackKind::Automation, "Automation");
    const auto copy = doc.duplicateTrack(midi, "M copy", 0, {});
    ASSERT_TRUE(copy.isValid());
    ASSERT_EQ(doc.getTracks().size(), 3u);
    EXPECT_EQ(doc.getTracks()[1].id, copy);
    EXPECT_EQ(doc.getTracks()[2].kind, TrackKind::Automation);
}
