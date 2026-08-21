#include "Timeline/TimelineDoc.h"
#include <gtest/gtest.h>
#include <limits>

using synth::AutomationLane;
using synth::BreakpointCurve;
using synth::Clip;
using synth::ClipId;
using synth::LaneId;
using synth::MidiNote;
using synth::NoteId;
using synth::TimelineDoc;
using synth::Track;
using synth::TrackId;
using synth::TrackKind;

namespace {

class CountingListener : public TimelineDoc::Listener {
public:
    void timelineChanged(const TimelineDoc&) override { ++calls; }
    int calls = 0;
};

MidiNote makeNote(double startBeat, int pitch, double lengthBeats = 1.0, int velocity = 100, int channel = 1) {
    MidiNote note;
    note.startBeat = startBeat;
    note.lengthBeats = lengthBeats;
    note.pitch = pitch;
    note.velocity = velocity;
    note.channel = channel;
    return note;
}

AutomationLane::RangeSnapshot makeRange(float minValue, float maxValue, float defaultValue) {
    AutomationLane::RangeSnapshot range;
    range.minValue = minValue;
    range.maxValue = maxValue;
    range.defaultValue = defaultValue;
    return range;
}

// Deep equality by serialised form: both docs emit their properties in the same order, so an
// identical JSON string means identical content (ids, counters and all).
juce::String dump(const TimelineDoc& doc) { return juce::JSON::toString(doc.toVar()); }

// Two tracks, clips out of order, notes, and two lanes with breakpoints — the fixture the
// round-trip tests serialise.
void buildPopulatedDoc(TimelineDoc& doc) {
    const auto lead = doc.addTrack(TrackKind::Midi, "Lead");
    const auto bass = doc.addTrack(TrackKind::Midi, "Bass");
    doc.setTrackColour(lead, 0xff112233);
    doc.setTrackBinding(lead, "uuid-track-in-1");
    doc.setTrackMuted(bass, true);

    const auto clipB = doc.addClip(lead, 8.0, 4.0, "B");
    const auto clipA = doc.addClip(lead, 0.0, 4.0, "A");
    doc.addNote(clipA, makeNote(2.0, 67));
    doc.addNote(clipA, makeNote(0.0, 60, 0.5, 90, 2));
    doc.addNote(clipB, makeNote(1.25, 72, 2.0, 1, 16));

    doc.addClip(bass, 4.0, 8.0, "Bassline");

    const auto cutoff = doc.addLane(lead, "uuid-filter", "cutoff", makeRange(20.0f, 20000.0f, 1000.0f));
    doc.addBreakpoint(cutoff, 4.0, 8000.0);
    doc.addBreakpoint(cutoff, 0.0, 500.0, 0.25f, static_cast<int>(BreakpointCurve::Hold));

    const auto res = doc.addLane(bass, "uuid-filter", "resonance", makeRange(0.0f, 1.0f, 0.5f));
    doc.addBreakpoint(res, 2.0, 0.75, -0.5f, static_cast<int>(BreakpointCurve::Bezier));
}

} // namespace

class TimelineDocTest : public ::testing::Test {
protected:
    TimelineDoc doc;
    CountingListener listener;

    void SetUp() override { doc.addListener(&listener); }
    void TearDown() override { doc.removeListener(&listener); }
};

// ------------------------------------------------------------------ 1. fresh --

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

// ------------------------------------------------------------ 3. enum values --

static_assert(static_cast<int>(TrackKind::Midi) == 0, "TrackKind values are file format");
static_assert(static_cast<int>(TrackKind::Audio) == 1, "TrackKind values are file format");
static_assert(static_cast<int>(TrackKind::Automation) == 2, "TrackKind values are file format");
static_assert(static_cast<int>(BreakpointCurve::Hold) == 0, "BreakpointCurve values are file format");
static_assert(static_cast<int>(BreakpointCurve::Linear) == 1, "BreakpointCurve values are file format");
static_assert(static_cast<int>(BreakpointCurve::Bezier) == 2, "BreakpointCurve values are file format");

TEST(TimelineDocEnums, SerialisedValuesArePinned) {
    EXPECT_EQ(static_cast<int>(TrackKind::Midi), 0);
    EXPECT_EQ(static_cast<int>(TrackKind::Audio), 1);
    EXPECT_EQ(static_cast<int>(TrackKind::Automation), 2);
    EXPECT_EQ(static_cast<int>(BreakpointCurve::Hold), 0);
    EXPECT_EQ(static_cast<int>(BreakpointCurve::Linear), 1);
    EXPECT_EQ(static_cast<int>(BreakpointCurve::Bezier), 2);
}

TEST_F(TimelineDocTest, UnknownTrackKindIsRejected) {
    EXPECT_FALSE(doc.addTrack(static_cast<TrackKind>(7), "reserved").isValid());
    EXPECT_EQ(doc.getRevision(), 0);
    EXPECT_EQ(listener.calls, 0);
}

// ------------------------------------------------------------------ 4. clips --

TEST_F(TimelineDocTest, ClipsStaySortedByStartBeat) {
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    doc.addClip(track, 8.0, 4.0, "late");
    doc.addClip(track, 0.0, 4.0, "early");
    doc.addClip(track, 4.0, 4.0, "middle");

    const auto* t = doc.getTrack(track);
    ASSERT_NE(t, nullptr);
    ASSERT_EQ(t->clips.size(), 3u);
    EXPECT_EQ(t->clips[0].name, "early");
    EXPECT_EQ(t->clips[1].name, "middle");
    EXPECT_EQ(t->clips[2].name, "late");
}

TEST_F(TimelineDocTest, ClipTiesAreBrokenById) {
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto a = doc.addClip(track, 2.0, 1.0, "a");
    const auto b = doc.addClip(track, 2.0, 1.0, "b");
    const auto c = doc.addClip(track, 2.0, 1.0, "c");

    const auto* t = doc.getTrack(track);
    ASSERT_EQ(t->clips.size(), 3u);
    EXPECT_EQ(t->clips[0].id, a);
    EXPECT_EQ(t->clips[1].id, b);
    EXPECT_EQ(t->clips[2].id, c);
}

TEST_F(TimelineDocTest, MoveClipReSortsAndKeepsNotes) {
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto first = doc.addClip(track, 0.0, 4.0, "first");
    const auto second = doc.addClip(track, 4.0, 4.0, "second");
    ASSERT_TRUE(doc.addNote(first, makeNote(1.0, 60)).isValid());

    ASSERT_TRUE(doc.moveClip(first, 16.0));
    const auto* t = doc.getTrack(track);
    ASSERT_EQ(t->clips.size(), 2u);
    EXPECT_EQ(t->clips[0].id, second);
    EXPECT_EQ(t->clips[1].id, first);
    ASSERT_EQ(t->clips[1].notes.size(), 1u); // notes are clip-relative, they ride along
    EXPECT_DOUBLE_EQ(t->clips[1].notes[0].startBeat, 1.0);
    EXPECT_DOUBLE_EQ(doc.getClip(first)->startBeat, 16.0);
}

TEST_F(TimelineDocTest, ResizeClipDoesNotChangeOrder) {
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto first = doc.addClip(track, 0.0, 4.0, "first");
    const auto second = doc.addClip(track, 4.0, 4.0, "second");

    ASSERT_TRUE(doc.resizeClip(first, 64.0));
    const auto* t = doc.getTrack(track);
    EXPECT_EQ(t->clips[0].id, first);
    EXPECT_EQ(t->clips[1].id, second);
    EXPECT_DOUBLE_EQ(doc.getClip(first)->lengthBeats, 64.0);
}

TEST_F(TimelineDocTest, InvalidClipEditsAreRejected) {
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto clip = doc.addClip(track, 0.0, 4.0, "c");
    const auto revisionBefore = doc.getRevision();

    EXPECT_FALSE(doc.addClip(track, -1.0, 4.0, "negative").isValid());
    EXPECT_FALSE(doc.addClip(track, 0.0, 0.0, "zero length").isValid());
    EXPECT_FALSE(doc.addClip(TrackId{999}, 0.0, 4.0, "no track").isValid());
    EXPECT_FALSE(doc.moveClip(clip, -0.5));
    EXPECT_FALSE(doc.resizeClip(clip, -4.0));
    EXPECT_FALSE(doc.removeClip(ClipId{999}));
    EXPECT_EQ(doc.getRevision(), revisionBefore);
}

TEST_F(TimelineDocTest, ClipLookupHelpersResolveOwner) {
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto clip = doc.addClip(track, 0.0, 4.0, "c");
    ASSERT_NE(doc.getTrackForClip(clip), nullptr);
    EXPECT_EQ(doc.getTrackForClip(clip)->id, track);
    EXPECT_EQ(doc.getTrackForClip(ClipId{999}), nullptr);
    ASSERT_TRUE(doc.removeClip(clip));
    EXPECT_EQ(doc.getClip(clip), nullptr);
}

// ------------------------------------------------------------------ 5. notes --

TEST_F(TimelineDocTest, NotesStaySortedByStartThenPitch) {
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto clip = doc.addClip(track, 0.0, 4.0, "c");
    ASSERT_TRUE(doc.addNote(clip, makeNote(2.0, 60)).isValid());
    ASSERT_TRUE(doc.addNote(clip, makeNote(0.0, 72)).isValid());
    ASSERT_TRUE(doc.addNote(clip, makeNote(0.0, 64)).isValid());
    ASSERT_TRUE(doc.addNote(clip, makeNote(1.0, 61)).isValid());

    const auto& notes = doc.getClip(clip)->notes;
    ASSERT_EQ(notes.size(), 4u);
    EXPECT_EQ(notes[0].pitch, 64);
    EXPECT_EQ(notes[1].pitch, 72);
    EXPECT_EQ(notes[2].pitch, 61);
    EXPECT_EQ(notes[3].pitch, 60);
    for (size_t i = 1; i < notes.size(); ++i)
        EXPECT_LE(notes[i - 1].startBeat, notes[i].startBeat);
}

TEST_F(TimelineDocTest, InvalidNotesAreRejectedWithoutMutating) {
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto clip = doc.addClip(track, 0.0, 4.0, "c");
    const auto revisionBefore = doc.getRevision();
    const auto callsBefore = listener.calls;

    EXPECT_FALSE(doc.addNote(clip, makeNote(0.0, 60, 0.0)).isValid());      // zero length
    EXPECT_FALSE(doc.addNote(clip, makeNote(0.0, 60, -1.0)).isValid());     // negative length
    EXPECT_FALSE(doc.addNote(clip, makeNote(0.0, -1)).isValid());           // pitch below range
    EXPECT_FALSE(doc.addNote(clip, makeNote(0.0, 128)).isValid());          // pitch above range
    EXPECT_FALSE(doc.addNote(clip, makeNote(0.0, 60, 1.0, 0)).isValid());   // velocity 0 is a note-off
    EXPECT_FALSE(doc.addNote(clip, makeNote(0.0, 60, 1.0, 128)).isValid()); // velocity above range
    EXPECT_FALSE(doc.addNote(clip, makeNote(0.0, 60, 1.0, 100, 0)).isValid());
    EXPECT_FALSE(doc.addNote(clip, makeNote(0.0, 60, 1.0, 100, 17)).isValid());
    EXPECT_FALSE(doc.addNote(clip, makeNote(-1.0, 60)).isValid()); // before the clip start
    EXPECT_FALSE(doc.addNote(ClipId{999}, makeNote(0.0, 60)).isValid());

    EXPECT_TRUE(doc.getClip(clip)->notes.empty());
    EXPECT_EQ(doc.getRevision(), revisionBefore);
    EXPECT_EQ(listener.calls, callsBefore);
}

TEST_F(TimelineDocTest, ClearNotesIsANoOpOnAnEmptyClip) {
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto clip = doc.addClip(track, 0.0, 4.0, "c");
    ASSERT_TRUE(doc.addNote(clip, makeNote(0.0, 60)).isValid());
    const auto revisionAfterNote = doc.getRevision();

    ASSERT_TRUE(doc.clearNotes(clip));
    EXPECT_EQ(doc.getRevision(), revisionAfterNote + 1);
    EXPECT_TRUE(doc.getClip(clip)->notes.empty());

    EXPECT_TRUE(doc.clearNotes(clip)); // nothing left to clear
    EXPECT_EQ(doc.getRevision(), revisionAfterNote + 1);
}

// ------------------------------------------------------------------ 6. lanes --

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

TEST_F(TimelineDocTest, ToVarFromVarRoundTripsExactly) {
    buildPopulatedDoc(doc);
    const auto before = dump(doc);

    TimelineDoc loaded;
    CountingListener loadedListener;
    loaded.addListener(&loadedListener);
    ASSERT_TRUE(loaded.fromVar(doc.toVar()));
    EXPECT_EQ(loadedListener.calls, 1); // exactly one notification for the whole load
    EXPECT_EQ(loaded.getRevision(), 1);
    EXPECT_EQ(dump(loaded), before);

    // Spot-check a few fields rather than trusting the string alone.
    ASSERT_EQ(loaded.getTracks().size(), 2u);
    const auto& lead = loaded.getTracks()[0];
    EXPECT_EQ(lead.name, "Lead");
    EXPECT_EQ(lead.colourArgb, 0xff112233u);
    EXPECT_EQ(lead.bindingUuid, "uuid-track-in-1");
    ASSERT_EQ(lead.clips.size(), 2u);
    EXPECT_EQ(lead.clips[0].name, "A");
    ASSERT_EQ(lead.clips[0].notes.size(), 2u);
    EXPECT_EQ(lead.clips[0].notes[0].pitch, 60);
    EXPECT_EQ(lead.clips[0].notes[0].channel, 2);
    ASSERT_EQ(lead.lanes.size(), 1u);
    EXPECT_EQ(lead.lanes[0].paramId, "cutoff");
    ASSERT_EQ(lead.lanes[0].points.size(), 2u);
    EXPECT_DOUBLE_EQ(lead.lanes[0].points[0].beat, 0.0);
    EXPECT_EQ(loaded.getTracks()[1].muted, true);

    loaded.removeListener(&loadedListener);
}

TEST_F(TimelineDocTest, IdCountersSurviveTheRoundTrip) {
    buildPopulatedDoc(doc);
    const auto lastTrack = doc.getTracks().back().id;
    const auto lastClip = doc.getTracks().back().clips.back().id;

    TimelineDoc loaded;
    ASSERT_TRUE(loaded.fromVar(doc.toVar()));

    const auto newTrack = loaded.addTrack(TrackKind::Midi, "After load");
    EXPECT_GT(newTrack.value, lastTrack.value);
    EXPECT_EQ(loaded.getTrack(newTrack)->id, newTrack);

    const auto newClip = loaded.addClip(newTrack, 0.0, 1.0, "c");
    EXPECT_GT(newClip.value, lastClip.value);
    // The new id collides with nothing that came off the file.
    int matches = 0;
    for (const auto& track : loaded.getTracks())
        for (const auto& clip : track.clips)
            if (clip.id == newClip)
                ++matches;
    EXPECT_EQ(matches, 1);
    EXPECT_EQ(loaded.getTrackForClip(newClip)->id, newTrack);
}

TEST_F(TimelineDocTest, ToVarCarriesTheFormatVersion) {
    const auto state = doc.toVar();
    ASSERT_NE(state.getDynamicObject(), nullptr);
    EXPECT_EQ(static_cast<int>(state.getDynamicObject()->getProperty("version")), TimelineDoc::kFormatVersion);
    EXPECT_TRUE(state.getDynamicObject()->getProperty("tracks").isArray());
}

TEST_F(TimelineDocTest, RoundTripSurvivesJsonText) {
    buildPopulatedDoc(doc);
    const auto text = juce::JSON::toString(doc.toVar());

    TimelineDoc loaded;
    ASSERT_TRUE(loaded.fromVar(juce::JSON::parse(text)));
    EXPECT_EQ(dump(loaded), dump(doc));
}

// ------------------------------------------------------- 11. all-or-nothing --

TEST_F(TimelineDocTest, FromVarLeavesTheDocUntouchedOnMalformedInput) {
    buildPopulatedDoc(doc);
    const auto before = dump(doc);
    const auto revisionBefore = doc.getRevision();
    const auto callsBefore = listener.calls;

    const juce::StringArray malformed{
        // a note pitch outside 0..127
        R"({"version":1,"tracks":[{"id":1,"clips":[{"id":1,"startBeat":0.0,"lengthBeats":4.0,
            "notes":[{"startBeat":0.0,"lengthBeats":1.0,"pitch":200,"velocity":100,"channel":1}]}]}]})",
        // a string where a number belongs
        R"({"version":1,"tracks":[{"id":1,"clips":[{"id":1,"startBeat":"soon","lengthBeats":4.0}]}]})",
        // wrong format version
        R"({"version":2,"tracks":[]})",
        // missing version
        R"({"tracks":[]})",
        // a reserved track kind this build can't represent
        R"({"version":1,"tracks":[{"id":1,"kind":7}]})",
        // duplicate track ids
        R"({"version":1,"tracks":[{"id":1},{"id":1}]})",
        // duplicate clip ids across tracks
        R"({"version":1,"tracks":[{"id":1,"clips":[{"id":9,"startBeat":0.0,"lengthBeats":1.0}]},
            {"id":2,"clips":[{"id":9,"startBeat":0.0,"lengthBeats":1.0}]}]})",
        // two lanes bound to the same (nodeUuid, paramId)
        R"({"version":1,"tracks":[{"id":1,"lanes":[{"id":1,"nodeUuid":"u","paramId":"p"},
            {"id":2,"nodeUuid":"u","paramId":"p"}]}]})",
        // a lane with no parameter binding
        R"({"version":1,"tracks":[{"id":1,"lanes":[{"id":1,"nodeUuid":"","paramId":"p"}]}]})",
        // a zero-length clip
        R"({"version":1,"tracks":[{"id":1,"clips":[{"id":1,"startBeat":0.0,"lengthBeats":0.0}]}]})",
        // an id of zero (the invalid sentinel)
        R"({"version":1,"tracks":[{"id":0}]})",
        // an id above kMaxIdValue: the next allocation off it (id + 1) must never signed-overflow
        R"({"version":1,"tracks":[{"id":2000000000000000}]})",
        // a next-id counter above kMaxIdValue, same overflow reason
        R"({"version":1,"nextTrackId":2000000000000000,"tracks":[]})",
        // a breakpoint curve beyond Bezier
        R"({"version":1,"tracks":[{"id":1,"lanes":[{"id":1,"nodeUuid":"u","paramId":"p",
            "points":[{"beat":0.0,"value":0.5,"curve":9}]}]}]})",
        // tracks isn't an array
        R"({"version":1,"tracks":42})",
        // a track that isn't an object
        R"({"version":1,"tracks":[7]})",
    };

    for (const auto& text : malformed) {
        const auto state = juce::JSON::parse(text);
        ASSERT_FALSE(state.isVoid()) << "test fixture JSON failed to parse: " << text;
        EXPECT_FALSE(doc.fromVar(state)) << text;
        EXPECT_EQ(dump(doc), before) << text;
        EXPECT_EQ(doc.getRevision(), revisionBefore) << text;
        EXPECT_EQ(listener.calls, callsBefore) << text;
    }

    // Not an object at all.
    EXPECT_FALSE(doc.fromVar(juce::var()));
    EXPECT_FALSE(doc.fromVar(juce::var(7)));
    EXPECT_EQ(dump(doc), before);
    EXPECT_EQ(doc.getRevision(), revisionBefore);
}

TEST_F(TimelineDocTest, FromVarRejectsCapViolations) {
    juce::Array<juce::var> tracksVar;
    for (int i = 0; i < TimelineDoc::kMaxTracks + 1; ++i) {
        juce::DynamicObject::Ptr t = new juce::DynamicObject();
        t->setProperty("id", i + 1);
        tracksVar.add(juce::var(t.get()));
    }
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("version", TimelineDoc::kFormatVersion);
    root->setProperty("tracks", tracksVar);

    EXPECT_FALSE(doc.fromVar(juce::var(root.get())));
    EXPECT_TRUE(doc.isEmpty());
    EXPECT_EQ(doc.getRevision(), 0);
}

TEST_F(TimelineDocTest, FromVarFloorsIdCountersAboveTheHighestIdInTheFile) {
    // A hand-edited file whose counters are stale must not be able to reissue a live id.
    const auto* text = R"({"version":1,"nextTrackId":1,"nextClipId":1,"nextLaneId":1,
        "tracks":[{"id":50,"clips":[{"id":70,"startBeat":0.0,"lengthBeats":4.0}],
        "lanes":[{"id":90,"nodeUuid":"u","paramId":"p"}]}]})";
    ASSERT_TRUE(doc.fromVar(juce::JSON::parse(text)));

    const auto track = doc.getTracks()[0].id;
    EXPECT_EQ(track.value, 50);
    EXPECT_GT(doc.addTrack(TrackKind::Midi, "next").value, 50);
    EXPECT_GT(doc.addClip(track, 0.0, 1.0, "c").value, 70);
    EXPECT_GT(doc.addLane(track, "u2", "p2", makeRange(0.0f, 1.0f, 0.0f)).value, 90);
}

// -------------------------------------------------------------- 12. re-sort --

TEST_F(TimelineDocTest, FromVarReSortsMisOrderedFileContent) {
    const auto* text = R"({
        "version": 1,
        "tracks": [{
            "id": 1, "kind": 0, "name": "T",
            "clips": [
                { "id": 2, "name": "late", "startBeat": 8.0, "lengthBeats": 4.0, "notes": [] },
                { "id": 1, "name": "early", "startBeat": 0.0, "lengthBeats": 4.0, "notes": [
                    { "id": 1, "startBeat": 3.0, "lengthBeats": 1.0, "pitch": 60, "velocity": 100, "channel": 1 },
                    { "id": 2, "startBeat": 1.0, "lengthBeats": 1.0, "pitch": 64, "velocity": 100, "channel": 1 },
                    { "id": 3, "startBeat": 1.0, "lengthBeats": 1.0, "pitch": 62, "velocity": 100, "channel": 1 }
                ]}
            ],
            "lanes": [{
                "id": 1, "nodeUuid": "u", "paramId": "p",
                "range": { "minValue": 0.0, "maxValue": 1.0, "defaultValue": 0.0 },
                "points": [
                    { "beat": 4.0, "value": 0.9, "tension": 0.0, "curve": 1 },
                    { "beat": 0.0, "value": 0.1, "tension": 0.0, "curve": 1 },
                    { "beat": 4.0, "value": 0.4, "tension": 0.0, "curve": 0 }
                ]
            }]
        }]
    })";

    ASSERT_TRUE(doc.fromVar(juce::JSON::parse(text)));
    const auto& track = doc.getTracks()[0];

    ASSERT_EQ(track.clips.size(), 2u);
    EXPECT_EQ(track.clips[0].name, "early");
    EXPECT_EQ(track.clips[1].name, "late");

    const auto& notes = track.clips[0].notes;
    ASSERT_EQ(notes.size(), 3u);
    EXPECT_DOUBLE_EQ(notes[0].startBeat, 1.0);
    EXPECT_EQ(notes[0].pitch, 62);
    EXPECT_EQ(notes[1].pitch, 64);
    EXPECT_DOUBLE_EQ(notes[2].startBeat, 3.0);

    // Two points on the same beat collapse to one, last-in-file wins — the same rule
    // addBreakpoint applies.
    ASSERT_EQ(track.lanes.size(), 1u);
    const auto& points = track.lanes[0].points;
    ASSERT_EQ(points.size(), 2u);
    EXPECT_DOUBLE_EQ(points[0].beat, 0.0);
    EXPECT_DOUBLE_EQ(points[1].beat, 4.0);
    EXPECT_DOUBLE_EQ(points[1].value, 0.4);
    EXPECT_EQ(points[1].curve, static_cast<int>(BreakpointCurve::Hold));
}

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

TEST_F(TimelineDocTest, NewClipDefaultsToNoAsset) {
    const auto track = doc.addTrack(TrackKind::Audio, "A");
    const auto clip = doc.addClip(track, 0.0, 4.0, "c");
    const auto* c = doc.getClip(clip);
    ASSERT_NE(c, nullptr);
    EXPECT_TRUE(c->assetRef.isEmpty());
    EXPECT_DOUBLE_EQ(c->gainDb, 0.0);
    EXPECT_DOUBLE_EQ(c->fadeInBeats, 0.0);
    EXPECT_DOUBLE_EQ(c->fadeOutBeats, 0.0);
    EXPECT_DOUBLE_EQ(c->sourceStartSeconds, 0.0);
}

TEST_F(TimelineDocTest, SetClipAssetGainAndFadesMutateOnce) {
    const auto track = doc.addTrack(TrackKind::Audio, "A");
    const auto clip = doc.addClip(track, 0.0, 4.0, "c");
    const auto revisionAfterAdd = doc.getRevision();
    const int callsAfterAdd = listener.calls;

    ASSERT_TRUE(doc.setClipAsset(clip, "Audio/take-1.wav", 0.25));
    ASSERT_TRUE(doc.setClipGainDb(clip, -3.5));
    ASSERT_TRUE(doc.setClipFades(clip, 0.5, 1.5));

    const auto* c = doc.getClip(clip);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->assetRef, "Audio/take-1.wav");
    EXPECT_DOUBLE_EQ(c->sourceStartSeconds, 0.25);
    EXPECT_DOUBLE_EQ(c->gainDb, -3.5);
    EXPECT_DOUBLE_EQ(c->fadeInBeats, 0.5);
    EXPECT_DOUBLE_EQ(c->fadeOutBeats, 1.5);

    // Three effective mutations, three notifications.
    EXPECT_EQ(doc.getRevision(), revisionAfterAdd + 3);
    EXPECT_EQ(listener.calls, callsAfterAdd + 3);

    // Setting the value already stored is a no-op on all three: no bump, no notification.
    const auto revision = doc.getRevision();
    const int calls = listener.calls;
    EXPECT_TRUE(doc.setClipAsset(clip, "Audio/take-1.wav", 0.25));
    EXPECT_TRUE(doc.setClipGainDb(clip, -3.5));
    EXPECT_TRUE(doc.setClipFades(clip, 0.5, 1.5));
    EXPECT_EQ(doc.getRevision(), revision);
    EXPECT_EQ(listener.calls, calls);
}

TEST_F(TimelineDocTest, SetClipAssetRejectsEscapingPaths) {
    const auto track = doc.addTrack(TrackKind::Audio, "A");
    const auto clip = doc.addClip(track, 0.0, 4.0, "c");
    const auto revision = doc.getRevision();

    // Every one of these would let a bundle read a file outside itself on whoever opens it.
    const juce::StringArray escaping = {
        "/etc/passwd",           "/Users/someone/take.wav",   "../take.wav",
        "Audio/../../take.wav",  "Audio/../../../etc/passwd", "..\\take.wav",
        "C:\\Windows\\take.wav", "C:/Windows/take.wav",       "\\\\server\\share\\take.wav"};
    for (const auto& ref : escaping) {
        EXPECT_FALSE(doc.setClipAsset(clip, ref, 0.0)) << "accepted escaping assetRef: " << ref;
        EXPECT_FALSE(TimelineDoc::isValidAssetRef(ref)) << "isValidAssetRef accepted: " << ref;
    }

    // Legal ones: bundle-relative, and the empty "no asset" value every MIDI clip carries.
    for (const auto& ref : juce::StringArray{"Audio/take-1.wav", "take.wav", "Audio/Nested/take..wav", ""})
        EXPECT_TRUE(TimelineDoc::isValidAssetRef(ref)) << "rejected legal assetRef: " << ref;

    // A non-finite source offset and a non-finite gain/fade are rejected too.
    EXPECT_FALSE(doc.setClipAsset(clip, "Audio/take-1.wav", std::numeric_limits<double>::quiet_NaN()));
    EXPECT_FALSE(doc.setClipAsset(clip, "Audio/take-1.wav", -1.0));
    EXPECT_FALSE(doc.setClipGainDb(clip, std::numeric_limits<double>::infinity()));
    EXPECT_FALSE(doc.setClipFades(clip, -0.1, 0.0));
    EXPECT_FALSE(doc.setClipFades(clip, 0.0, std::numeric_limits<double>::quiet_NaN()));

    // Nothing was mutated by any rejection.
    EXPECT_EQ(doc.getRevision(), revision);
    EXPECT_TRUE(doc.getClip(clip)->assetRef.isEmpty());
}

TEST_F(TimelineDocTest, AudioClipFieldsSurviveRoundTrip) {
    const auto track = doc.addTrack(TrackKind::Audio, "A");
    const auto clip = doc.addClip(track, 4.0, 8.0, "Take 1");
    ASSERT_TRUE(doc.setClipAsset(clip, "Audio/take-1.wav", 1.5));
    ASSERT_TRUE(doc.setClipGainDb(clip, -6.0));
    ASSERT_TRUE(doc.setClipFades(clip, 0.25, 0.75));

    TimelineDoc reloaded;
    ASSERT_TRUE(reloaded.fromVar(doc.toVar()));
    EXPECT_EQ(dump(reloaded), dump(doc));

    ASSERT_EQ(reloaded.getTracks().size(), 1u);
    EXPECT_EQ(reloaded.getTracks()[0].kind, TrackKind::Audio);
    ASSERT_EQ(reloaded.getTracks()[0].clips.size(), 1u);
    const auto& c = reloaded.getTracks()[0].clips[0];
    EXPECT_EQ(c.assetRef, "Audio/take-1.wav");
    EXPECT_DOUBLE_EQ(c.sourceStartSeconds, 1.5);
    EXPECT_DOUBLE_EQ(c.gainDb, -6.0);
    EXPECT_DOUBLE_EQ(c.fadeInBeats, 0.25);
    EXPECT_DOUBLE_EQ(c.fadeOutBeats, 0.75);
}

TEST_F(TimelineDocTest, FromVarDefaultsAudioFieldsWhenAbsent) {
    // Exactly what an older file with no audio keys at all looks like, version still 1.
    const auto* text = R"({"version":1,"tracks":[{"id":1,"kind":1,"name":"A",
        "clips":[{"id":1,"name":"c","startBeat":0.0,"lengthBeats":4.0,"notes":[]}]}]})";
    ASSERT_TRUE(doc.fromVar(juce::JSON::parse(text)));

    const auto& c = doc.getTracks()[0].clips[0];
    EXPECT_TRUE(c.assetRef.isEmpty());
    EXPECT_DOUBLE_EQ(c.gainDb, 0.0);
    EXPECT_DOUBLE_EQ(c.fadeInBeats, 0.0);
    EXPECT_DOUBLE_EQ(c.fadeOutBeats, 0.0);
    EXPECT_DOUBLE_EQ(c.sourceStartSeconds, 0.0);
}

TEST_F(TimelineDocTest, FromVarRejectsEscapingOrMalformedAudioFields) {
    // A hand-edited bundle must not be the way around setClipAsset's check.
    const juce::StringArray bad = {
        R"({"version":1,"tracks":[{"id":1,"clips":[{"id":1,"assetRef":"../../etc/passwd"}]}]})",
        R"({"version":1,"tracks":[{"id":1,"clips":[{"id":1,"assetRef":"/etc/passwd"}]}]})",
        R"({"version":1,"tracks":[{"id":1,"clips":[{"id":1,"assetRef":"C:/Windows/take.wav"}]}]})",
        R"({"version":1,"tracks":[{"id":1,"clips":[{"id":1,"assetRef":17}]}]})",
        R"({"version":1,"tracks":[{"id":1,"clips":[{"id":1,"fadeInBeats":-1.0}]}]})",
        R"({"version":1,"tracks":[{"id":1,"clips":[{"id":1,"sourceStartSeconds":-0.5}]}]})",
    };
    for (const auto& text : bad) {
        TimelineDoc target;
        EXPECT_FALSE(target.fromVar(juce::JSON::parse(text))) << "accepted malformed clip: " << text;
        EXPECT_TRUE(target.isEmpty()) << "a rejected load must leave the doc untouched";
    }
}

TEST_F(TimelineDocTest, DuplicateAndSplitCarryAudioFields) {
    const auto track = doc.addTrack(TrackKind::Audio, "A");
    const auto clip = doc.addClip(track, 0.0, 8.0, "Take");
    ASSERT_TRUE(doc.setClipAsset(clip, "Audio/take-1.wav", 2.0));
    ASSERT_TRUE(doc.setClipGainDb(clip, -2.0));
    ASSERT_TRUE(doc.setClipFades(clip, 0.5, 1.0));

    // Duplicate is an exact copy of every audio field (no tempo map needed).
    const auto dup = doc.duplicateClip(clip);
    ASSERT_TRUE(dup.isValid());
    const auto* d = doc.getClip(dup);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->assetRef, "Audio/take-1.wav");
    EXPECT_DOUBLE_EQ(d->sourceStartSeconds, 2.0);
    EXPECT_DOUBLE_EQ(d->gainDb, -2.0);
    EXPECT_DOUBLE_EQ(d->fadeInBeats, 0.5);
    EXPECT_DOUBLE_EQ(d->fadeOutBeats, 1.0);

    // Split: both halves keep the asset and gain, each keeps the fade at the edge it still owns,
    // and the right half's sourceStartSeconds is deliberately NOT advanced (no tempo map here —
    // see splitClip's comment).
    const auto halves = doc.splitClip(clip, 4.0);
    ASSERT_TRUE(halves.first.isValid());
    ASSERT_TRUE(halves.second.isValid());
    const auto* left = doc.getClip(halves.first);
    const auto* right = doc.getClip(halves.second);
    ASSERT_NE(left, nullptr);
    ASSERT_NE(right, nullptr);
    EXPECT_EQ(left->assetRef, "Audio/take-1.wav");
    EXPECT_EQ(right->assetRef, "Audio/take-1.wav");
    EXPECT_DOUBLE_EQ(left->gainDb, -2.0);
    EXPECT_DOUBLE_EQ(right->gainDb, -2.0);
    EXPECT_DOUBLE_EQ(left->fadeInBeats, 0.5);
    EXPECT_DOUBLE_EQ(left->fadeOutBeats, 0.0);
    EXPECT_DOUBLE_EQ(right->fadeInBeats, 0.0);
    EXPECT_DOUBLE_EQ(right->fadeOutBeats, 1.0);
    EXPECT_DOUBLE_EQ(right->sourceStartSeconds, 2.0);
}

// -------------------------------------------------- 14. mute, rename, retrack --
// The data layer behind the Cubase-style Mute tool and the cross-lane clip drag. Mute is
// persistent state, not a transient UI mode: it round-trips, it survives every structural edit,
// and (in TimelineSnapshotTests) it is what makes content vanish from the flatten.

TEST_F(TimelineDocTest, ClipAndNoteMuteSurviveRoundTrip) {
    const auto track = doc.addTrack(TrackKind::Midi, "Lead");
    const auto audible = doc.addClip(track, 0.0, 4.0, "Audible");
    const auto silent = doc.addClip(track, 8.0, 4.0, "Silent");
    const auto keptNote = doc.addNote(audible, makeNote(0.0, 60));
    const auto mutedNote = doc.addNote(audible, makeNote(2.0, 67));
    ASSERT_TRUE(doc.setClipMuted(silent, true));
    ASSERT_TRUE(doc.setNoteMuted(mutedNote, true));

    TimelineDoc reloaded;
    ASSERT_TRUE(reloaded.fromVar(doc.toVar()));
    EXPECT_EQ(dump(reloaded), dump(doc));

    ASSERT_NE(reloaded.getClip(silent), nullptr);
    EXPECT_TRUE(reloaded.getClip(silent)->muted);
    EXPECT_FALSE(reloaded.getClip(audible)->muted);
    ASSERT_NE(reloaded.getNote(mutedNote), nullptr);
    EXPECT_TRUE(reloaded.getNote(mutedNote)->muted);
    EXPECT_FALSE(reloaded.getNote(keptNote)->muted);
}

TEST_F(TimelineDocTest, FromVarDefaultsMutedWhenAbsent) {
    // Exactly what a file written before per-clip / per-note mute existed looks like: no "muted"
    // key anywhere, version still 1. Absent must mean audible, or every older project loads silent.
    const auto* text = R"({"version":1,"tracks":[{"id":1,"kind":0,"name":"A",
        "clips":[{"id":1,"name":"c","startBeat":0.0,"lengthBeats":4.0,
                  "notes":[{"id":1,"startBeat":0.0,"lengthBeats":1.0,"pitch":60}]}]}]})";
    ASSERT_TRUE(doc.fromVar(juce::JSON::parse(text)));

    const auto& clip = doc.getTracks()[0].clips[0];
    EXPECT_FALSE(clip.muted);
    ASSERT_EQ(clip.notes.size(), 1u);
    EXPECT_FALSE(clip.notes[0].muted);

    // Present but mistyped is malformed, not something to coerce — same rule every other optional
    // field here follows.
    const juce::StringArray bad = {
        R"({"version":1,"tracks":[{"id":1,"clips":[{"id":1,"muted":"yes"}]}]})",
        R"({"version":1,"tracks":[{"id":1,"clips":[{"id":1,"muted":1}]}]})",
        R"({"version":1,"tracks":[{"id":1,"clips":[{"id":1,
            "notes":[{"id":1,"pitch":60,"muted":"yes"}]}]}]})",
    };
    for (const auto& malformed : bad) {
        TimelineDoc target;
        EXPECT_FALSE(target.fromVar(juce::JSON::parse(malformed))) << "accepted mistyped muted: " << malformed;
        EXPECT_TRUE(target.isEmpty()) << "a rejected load must leave the doc untouched";
    }
}

TEST_F(TimelineDocTest, SetClipMutedAndSetNoteMutedMutateOnce) {
    const auto track = doc.addTrack(TrackKind::Midi, "Lead");
    const auto clip = doc.addClip(track, 0.0, 4.0, "c");
    const auto note = doc.addNote(clip, makeNote(0.0, 60));
    const auto revisionAfterSetup = doc.getRevision();
    const int callsAfterSetup = listener.calls;

    EXPECT_TRUE(doc.setClipMuted(clip, true));
    EXPECT_TRUE(doc.setNoteMuted(note, true));
    EXPECT_TRUE(doc.getClip(clip)->muted);
    EXPECT_TRUE(doc.getNote(note)->muted);
    EXPECT_EQ(doc.getRevision(), revisionAfterSetup + 2);
    EXPECT_EQ(listener.calls, callsAfterSetup + 2);

    // Setting the flag already stored is a no-op: true, but no bump and no notification.
    EXPECT_TRUE(doc.setClipMuted(clip, true));
    EXPECT_TRUE(doc.setNoteMuted(note, true));
    EXPECT_EQ(doc.getRevision(), revisionAfterSetup + 2);
    EXPECT_EQ(listener.calls, callsAfterSetup + 2);

    // An id that doesn't resolve is a rejection, not a no-op — and mutates nothing either way.
    EXPECT_FALSE(doc.setClipMuted(ClipId{}, false));
    EXPECT_FALSE(doc.setClipMuted(ClipId{9999}, false));
    EXPECT_FALSE(doc.setNoteMuted(NoteId{}, false));
    EXPECT_FALSE(doc.setNoteMuted(NoteId{9999}, false));
    EXPECT_EQ(doc.getRevision(), revisionAfterSetup + 2);
    EXPECT_EQ(listener.calls, callsAfterSetup + 2);

    // Un-muting is symmetrical.
    EXPECT_TRUE(doc.setClipMuted(clip, false));
    EXPECT_TRUE(doc.setNoteMuted(note, false));
    EXPECT_FALSE(doc.getClip(clip)->muted);
    EXPECT_FALSE(doc.getNote(note)->muted);
    EXPECT_EQ(doc.getRevision(), revisionAfterSetup + 4);
}

TEST_F(TimelineDocTest, SetClipNameTrimsAndRejectsBlank) {
    const auto track = doc.addTrack(TrackKind::Midi, "Lead");
    const auto clip = doc.addClip(track, 0.0, 4.0, "A");

    const auto revisionBefore = doc.getRevision();
    EXPECT_TRUE(doc.setClipName(clip, "  Verse 1\t"));
    EXPECT_EQ(doc.getClip(clip)->name, "Verse 1") << "the stored name must be trimmed";
    EXPECT_EQ(doc.getRevision(), revisionBefore + 1);

    // Unchanged after trimming is a no-op, whatever whitespace the caller wrapped it in.
    const auto revisionAfterRename = doc.getRevision();
    const int callsAfterRename = listener.calls;
    EXPECT_TRUE(doc.setClipName(clip, "Verse 1"));
    EXPECT_TRUE(doc.setClipName(clip, "   Verse 1   "));
    EXPECT_EQ(doc.getRevision(), revisionAfterRename);
    EXPECT_EQ(listener.calls, callsAfterRename);

    // Empty after trimming is REJECTED, not stored: a blank clip title reads as a broken lane.
    for (const auto& blank : juce::StringArray{"", " ", "\t", "\n", "   \t\n  "})
        EXPECT_FALSE(doc.setClipName(clip, blank)) << "accepted a blank clip name";
    EXPECT_FALSE(doc.setClipName(ClipId{}, "x"));
    EXPECT_FALSE(doc.setClipName(ClipId{9999}, "x"));

    EXPECT_EQ(doc.getClip(clip)->name, "Verse 1");
    EXPECT_EQ(doc.getRevision(), revisionAfterRename);
    EXPECT_EQ(listener.calls, callsAfterRename);
}

TEST_F(TimelineDocTest, MoveClipToTrackCarriesEverythingAcross) {
    const auto from = doc.addTrack(TrackKind::Midi, "From");
    const auto to = doc.addTrack(TrackKind::Midi, "To");
    // Two clips already on the destination, so the arrival has to land in sorted position rather
    // than simply being appended.
    doc.addClip(to, 0.0, 4.0, "First");
    doc.addClip(to, 20.0, 4.0, "Last");

    const auto clip = doc.addClip(from, 0.0, 8.0, "Travelling");
    const auto low = doc.addNote(clip, makeNote(0.5, 60, 1.0, 90, 2));
    const auto high = doc.addNote(clip, makeNote(4.0, 72, 2.0, 30, 5));
    ASSERT_TRUE(doc.setNoteMuted(high, true));
    ASSERT_TRUE(doc.setClipMuted(clip, true));
    ASSERT_TRUE(doc.setClipGainDb(clip, -4.5));
    ASSERT_TRUE(doc.setClipFades(clip, 0.25, 0.75));

    const auto revisionBefore = doc.getRevision();
    const int callsBefore = listener.calls;
    ASSERT_TRUE(doc.moveClipToTrack(clip, to, 12.0));
    EXPECT_EQ(doc.getRevision(), revisionBefore + 1) << "a cross-track move is exactly one mutation";
    EXPECT_EQ(listener.calls, callsBefore + 1);

    EXPECT_TRUE(doc.getTrack(from)->clips.empty()) << "the clip must leave its old track";
    const auto* destTrack = doc.getTrack(to);
    ASSERT_EQ(destTrack->clips.size(), 3u);
    EXPECT_EQ(destTrack->clips[1].id, clip) << "the arrival must be re-seated in sorted position";
    EXPECT_EQ(doc.getTrackForClip(clip), destTrack);

    const auto* moved = doc.getClip(clip);
    ASSERT_NE(moved, nullptr);
    EXPECT_DOUBLE_EQ(moved->startBeat, 12.0);
    EXPECT_DOUBLE_EQ(moved->lengthBeats, 8.0);
    EXPECT_EQ(moved->name, "Travelling");
    EXPECT_TRUE(moved->muted);
    EXPECT_DOUBLE_EQ(moved->gainDb, -4.5);
    EXPECT_DOUBLE_EQ(moved->fadeInBeats, 0.25);
    EXPECT_DOUBLE_EQ(moved->fadeOutBeats, 0.75);

    // Notes are clip-relative, so the payload rides along completely untouched — same ids, same
    // clip-relative beats, same per-note mute.
    ASSERT_EQ(moved->notes.size(), 2u);
    EXPECT_EQ(moved->notes[0].id, low);
    EXPECT_DOUBLE_EQ(moved->notes[0].startBeat, 0.5);
    EXPECT_EQ(moved->notes[0].velocity, 90);
    EXPECT_EQ(moved->notes[0].channel, 2);
    EXPECT_FALSE(moved->notes[0].muted);
    EXPECT_EQ(moved->notes[1].id, high);
    EXPECT_DOUBLE_EQ(moved->notes[1].startBeat, 4.0);
    EXPECT_TRUE(moved->notes[1].muted);

    // An audio clip crosses to another Audio track with its asset intact.
    const auto audioFrom = doc.addTrack(TrackKind::Audio, "Take A");
    const auto audioTo = doc.addTrack(TrackKind::Audio, "Take B");
    const auto audioClip = doc.addClip(audioFrom, 0.0, 4.0, "Take");
    ASSERT_TRUE(doc.setClipAsset(audioClip, "Audio/take-1.wav", 1.5));
    ASSERT_TRUE(doc.moveClipToTrack(audioClip, audioTo, 6.0));
    EXPECT_EQ(doc.getTrackForClip(audioClip), doc.getTrack(audioTo));
    EXPECT_EQ(doc.getClip(audioClip)->assetRef, "Audio/take-1.wav");
    EXPECT_DOUBLE_EQ(doc.getClip(audioClip)->sourceStartSeconds, 1.5);
}

TEST_F(TimelineDocTest, MoveClipToTrackOnItsOwnTrackBehavesLikeMoveClip) {
    const auto track = doc.addTrack(TrackKind::Midi, "Lead");
    const auto early = doc.addClip(track, 0.0, 4.0, "Early");
    doc.addClip(track, 8.0, 4.0, "Late");

    const auto revisionBefore = doc.getRevision();
    ASSERT_TRUE(doc.moveClipToTrack(early, track, 12.0));
    EXPECT_DOUBLE_EQ(doc.getClip(early)->startBeat, 12.0);
    EXPECT_EQ(doc.getTrack(track)->clips[1].id, early) << "an in-place move still re-sorts";
    EXPECT_EQ(doc.getRevision(), revisionBefore + 1);

    // Moving to where it already is is the same no-op moveClip is: true, no bump.
    const int callsAfterMove = listener.calls;
    EXPECT_TRUE(doc.moveClipToTrack(early, track, 12.0));
    EXPECT_EQ(doc.getRevision(), revisionBefore + 1);
    EXPECT_EQ(listener.calls, callsAfterMove);

    // And the kind check is skipped in place: the model tolerates an assetRef on a clip that sits
    // on a MIDI track, so dragging that clip along its own lane must not suddenly fail.
    ASSERT_TRUE(doc.setClipAsset(early, "Audio/stray.wav", 0.0));
    EXPECT_TRUE(doc.moveClipToTrack(early, track, 2.0));
    EXPECT_DOUBLE_EQ(doc.getClip(early)->startBeat, 2.0);
}

TEST_F(TimelineDocTest, MoveClipToTrackRejectsBadDestinationsWithoutMutating) {
    const auto midiFrom = doc.addTrack(TrackKind::Midi, "Midi From");
    const auto midiTo = doc.addTrack(TrackKind::Midi, "Midi To");
    const auto audioFrom = doc.addTrack(TrackKind::Audio, "Audio From");
    const auto audioTo = doc.addTrack(TrackKind::Audio, "Audio To");
    const auto automation = doc.addTrack(TrackKind::Automation, "Automation");

    const auto midiClip = doc.addClip(midiFrom, 0.0, 4.0, "Midi");
    const auto audioClip = doc.addClip(audioFrom, 0.0, 4.0, "Audio");
    ASSERT_TRUE(doc.setClipAsset(audioClip, "Audio/take-1.wav", 0.0));

    const auto revisionBefore = doc.getRevision();
    const int callsBefore = listener.calls;

    // A destination that doesn't exist.
    EXPECT_FALSE(doc.moveClipToTrack(midiClip, TrackId{}, 4.0));
    EXPECT_FALSE(doc.moveClipToTrack(midiClip, TrackId{9999}, 4.0));
    // A clip that doesn't exist.
    EXPECT_FALSE(doc.moveClipToTrack(ClipId{}, midiTo, 4.0));
    EXPECT_FALSE(doc.moveClipToTrack(ClipId{9999}, midiTo, 4.0));
    // Kind mismatch, both directions — and an Automation track takes neither.
    EXPECT_FALSE(doc.moveClipToTrack(audioClip, midiTo, 4.0)) << "an audio clip must not land on a MIDI track";
    EXPECT_FALSE(doc.moveClipToTrack(midiClip, audioTo, 4.0)) << "a MIDI clip must not land on an audio track";
    EXPECT_FALSE(doc.moveClipToTrack(midiClip, automation, 4.0));
    EXPECT_FALSE(doc.moveClipToTrack(audioClip, automation, 4.0));
    // The same start-beat validation moveClip applies.
    EXPECT_FALSE(doc.moveClipToTrack(midiClip, midiTo, -1.0));
    EXPECT_FALSE(doc.moveClipToTrack(midiClip, midiTo, std::numeric_limits<double>::quiet_NaN()));
    EXPECT_FALSE(doc.moveClipToTrack(midiClip, midiTo, std::numeric_limits<double>::infinity()));

    EXPECT_EQ(doc.getRevision(), revisionBefore) << "every rejection must leave the revision alone";
    EXPECT_EQ(listener.calls, callsBefore);
    EXPECT_EQ(doc.getTrackForClip(midiClip), doc.getTrack(midiFrom));
    EXPECT_EQ(doc.getTrackForClip(audioClip), doc.getTrack(audioFrom));

    // Emptying the assetRef is what makes the same clip legal on a MIDI track: the PAYLOAD decides,
    // not the track it came from.
    ASSERT_TRUE(doc.setClipAsset(audioClip, "", 0.0));
    EXPECT_TRUE(doc.moveClipToTrack(audioClip, midiTo, 4.0));
    EXPECT_EQ(doc.getTrackForClip(audioClip), doc.getTrack(midiTo));
}

TEST_F(TimelineDocTest, SplitDuplicateAndJoinCarryMuteFlags) {
    const auto track = doc.addTrack(TrackKind::Midi, "Lead");
    const auto clip = doc.addClip(track, 0.0, 8.0, "Muted");
    const auto early = doc.addNote(clip, makeNote(1.0, 60));           // entirely left of the cut
    const auto straddling = doc.addNote(clip, makeNote(3.5, 64, 1.0)); // crosses the cut at 4.0
    const auto late = doc.addNote(clip, makeNote(5.0, 67));            // entirely right of the cut
    ASSERT_TRUE(doc.setClipMuted(clip, true));
    ASSERT_TRUE(doc.setNoteMuted(straddling, true));

    // Split: BOTH halves inherit the clip's mute, and the straddling note's two halves both keep
    // the note's own mute (the left half keeps the original id, the right half gets a fresh one).
    const auto halves = doc.splitClip(clip, 4.0);
    ASSERT_TRUE(halves.first.isValid());
    ASSERT_TRUE(halves.second.isValid());
    const auto* left = doc.getClip(halves.first);
    const auto* right = doc.getClip(halves.second);
    ASSERT_NE(left, nullptr);
    ASSERT_NE(right, nullptr);
    EXPECT_TRUE(left->muted);
    EXPECT_TRUE(right->muted) << "cutting a muted clip in two must not make half of it audible";

    ASSERT_EQ(left->notes.size(), 2u);
    EXPECT_EQ(left->notes[0].id, early);
    EXPECT_FALSE(left->notes[0].muted);
    EXPECT_EQ(left->notes[1].id, straddling);
    EXPECT_TRUE(left->notes[1].muted);
    ASSERT_EQ(right->notes.size(), 2u);
    EXPECT_DOUBLE_EQ(right->notes[0].startBeat, 0.0);
    EXPECT_EQ(right->notes[0].pitch, 64);
    EXPECT_NE(right->notes[0].id, straddling) << "the right half of a split note gets a fresh id";
    EXPECT_TRUE(right->notes[0].muted) << "a note's mute travels with it through the re-basing";
    EXPECT_EQ(right->notes[1].id, late);
    EXPECT_FALSE(right->notes[1].muted);

    // Duplicate: the copy is muted too, and each copied note keeps its own flag.
    const auto dup = doc.duplicateClip(halves.first);
    ASSERT_TRUE(dup.isValid());
    const auto* copy = doc.getClip(dup);
    ASSERT_NE(copy, nullptr);
    EXPECT_TRUE(copy->muted);
    ASSERT_EQ(copy->notes.size(), 2u);
    EXPECT_FALSE(copy->notes[0].muted);
    EXPECT_TRUE(copy->notes[1].muted);
    EXPECT_NE(copy->notes[1].id, straddling) << "a duplicate's notes get fresh ids";

    // Join: the SURVIVOR's flag wins, whichever way round the two clips are muted.
    TimelineDoc joinDoc;
    const auto joinTrack = joinDoc.addTrack(TrackKind::Midi, "Join");
    const auto audibleA = joinDoc.addClip(joinTrack, 0.0, 4.0, "A");
    const auto mutedB = joinDoc.addClip(joinTrack, 4.0, 4.0, "B");
    const auto noteInB = joinDoc.addNote(mutedB, makeNote(1.0, 72));
    ASSERT_TRUE(joinDoc.setClipMuted(mutedB, true));
    ASSERT_TRUE(joinDoc.setNoteMuted(noteInB, true));
    ASSERT_TRUE(joinDoc.joinClips(audibleA, mutedB));
    const auto* joined = joinDoc.getClip(audibleA);
    ASSERT_NE(joined, nullptr);
    EXPECT_FALSE(joined->muted) << "joinClips keeps a's muted flag, not b's";
    ASSERT_EQ(joined->notes.size(), 1u);
    EXPECT_TRUE(joined->notes[0].muted) << "a merged note keeps its own mute, whatever the clips did";

    // ...and the mirror image: a muted `a` stays muted after swallowing an audible `b`.
    const auto mutedC = joinDoc.addClip(joinTrack, 16.0, 4.0, "C");
    const auto audibleD = joinDoc.addClip(joinTrack, 20.0, 4.0, "D");
    ASSERT_TRUE(joinDoc.setClipMuted(mutedC, true));
    ASSERT_TRUE(joinDoc.joinClips(mutedC, audibleD));
    EXPECT_TRUE(joinDoc.getClip(mutedC)->muted);
}
