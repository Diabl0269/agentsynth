#include "Timeline/TimelineDoc.h"
#include <gtest/gtest.h>

using synth::AutomationLane;
using synth::BreakpointCurve;
using synth::Clip;
using synth::ClipId;
using synth::LaneId;
using synth::MidiNote;
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
    ASSERT_TRUE(doc.addNote(first, makeNote(1.0, 60)));

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
    ASSERT_TRUE(doc.addNote(clip, makeNote(2.0, 60)));
    ASSERT_TRUE(doc.addNote(clip, makeNote(0.0, 72)));
    ASSERT_TRUE(doc.addNote(clip, makeNote(0.0, 64)));
    ASSERT_TRUE(doc.addNote(clip, makeNote(1.0, 61)));

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

    EXPECT_FALSE(doc.addNote(clip, makeNote(0.0, 60, 0.0)));      // zero length
    EXPECT_FALSE(doc.addNote(clip, makeNote(0.0, 60, -1.0)));     // negative length
    EXPECT_FALSE(doc.addNote(clip, makeNote(0.0, -1)));           // pitch below range
    EXPECT_FALSE(doc.addNote(clip, makeNote(0.0, 128)));          // pitch above range
    EXPECT_FALSE(doc.addNote(clip, makeNote(0.0, 60, 1.0, 0)));   // velocity 0 is a note-off
    EXPECT_FALSE(doc.addNote(clip, makeNote(0.0, 60, 1.0, 128))); // velocity above range
    EXPECT_FALSE(doc.addNote(clip, makeNote(0.0, 60, 1.0, 100, 0)));
    EXPECT_FALSE(doc.addNote(clip, makeNote(0.0, 60, 1.0, 100, 17)));
    EXPECT_FALSE(doc.addNote(clip, makeNote(-1.0, 60))); // before the clip start
    EXPECT_FALSE(doc.addNote(ClipId{999}, makeNote(0.0, 60)));

    EXPECT_TRUE(doc.getClip(clip)->notes.empty());
    EXPECT_EQ(doc.getRevision(), revisionBefore);
    EXPECT_EQ(listener.calls, callsBefore);
}

TEST_F(TimelineDocTest, ClearNotesIsANoOpOnAnEmptyClip) {
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto clip = doc.addClip(track, 0.0, 4.0, "c");
    ASSERT_TRUE(doc.addNote(clip, makeNote(0.0, 60)));
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
    ASSERT_TRUE(doc.addNote(clip, makeNote(0.0, 60)));
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
    EXPECT_FALSE(doc.addNote(clip, makeNote(0.0, 200)));
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
        ASSERT_TRUE(doc.addNote(clip, makeNote(static_cast<double>(i), 60))) << "note " << i;
    EXPECT_FALSE(doc.addNote(clip, makeNote(1e9, 60)));
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
                    { "startBeat": 3.0, "lengthBeats": 1.0, "pitch": 60, "velocity": 100, "channel": 1 },
                    { "startBeat": 1.0, "lengthBeats": 1.0, "pitch": 64, "velocity": 100, "channel": 1 },
                    { "startBeat": 1.0, "lengthBeats": 1.0, "pitch": 62, "velocity": 100, "channel": 1 }
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
