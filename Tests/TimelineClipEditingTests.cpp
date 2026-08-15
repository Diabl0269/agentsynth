// TL2-3: note identity + the note-editing/clip-operations API on TimelineDoc. Split out from
// TimelineDocTests.cpp to keep that file to the container-level model; this one covers
// removeNote/moveNote/resizeNote/setNoteVelocity/quantiseNotes and splitClip/joinClips/
// duplicateClip.
#include "Timeline/TimelineDoc.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <limits>
#include <vector>

using synth::ClipId;
using synth::MidiNote;
using synth::NoteId;
using synth::TimelineDoc;
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

} // namespace

class TimelineClipEditingTest : public ::testing::Test {
protected:
    TimelineDoc doc;
    CountingListener listener;

    void SetUp() override { doc.addListener(&listener); }
    void TearDown() override { doc.removeListener(&listener); }
};

// ------------------------------------------------------------- note identity --

TEST_F(TimelineClipEditingTest, NoteIdsAreMonotonicAndNeverReused) {
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto clip = doc.addClip(track, 0.0, 8.0, "c");

    const auto n1 = doc.addNote(clip, makeNote(0.0, 60));
    const auto n2 = doc.addNote(clip, makeNote(1.0, 61));
    ASSERT_TRUE(n1.isValid());
    ASSERT_TRUE(n2.isValid());
    EXPECT_LT(n1.value, n2.value);

    ASSERT_TRUE(doc.removeNote(n1));
    const auto n3 = doc.addNote(clip, makeNote(2.0, 62));
    ASSERT_TRUE(n3.isValid());
    EXPECT_GT(n3.value, n2.value);
    EXPECT_EQ(doc.getNote(n1), nullptr); // gone, and its id is never handed out again
}

// ---------------------------------------------------------------- note edits --

TEST_F(TimelineClipEditingTest, RemoveNoteHappyPathAndRejection) {
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto clip = doc.addClip(track, 0.0, 4.0, "c");
    const auto note = doc.addNote(clip, makeNote(0.0, 60));
    ASSERT_TRUE(note.isValid());
    const auto revisionBefore = doc.getRevision();
    const auto callsBefore = listener.calls;

    EXPECT_FALSE(doc.removeNote(NoteId{999}));
    EXPECT_EQ(doc.getRevision(), revisionBefore);
    EXPECT_EQ(listener.calls, callsBefore);

    ASSERT_TRUE(doc.removeNote(note));
    EXPECT_EQ(doc.getRevision(), revisionBefore + 1);
    EXPECT_EQ(doc.getNote(note), nullptr);
    EXPECT_TRUE(doc.getClip(clip)->notes.empty());
}

TEST_F(TimelineClipEditingTest, MoveNoteHappyPathAndRejections) {
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto clip = doc.addClip(track, 0.0, 16.0, "c");
    const auto note = doc.addNote(clip, makeNote(2.0, 60));
    ASSERT_TRUE(note.isValid());
    const auto revisionBefore = doc.getRevision();
    const auto callsBefore = listener.calls;

    EXPECT_FALSE(doc.moveNote(note, -1.0, 60));                                     // negative start
    EXPECT_FALSE(doc.moveNote(note, std::numeric_limits<double>::quiet_NaN(), 60)); // non-finite
    EXPECT_FALSE(doc.moveNote(note, 4.0, -1));                                      // pitch below range
    EXPECT_FALSE(doc.moveNote(note, 4.0, 128));                                     // pitch above range
    EXPECT_FALSE(doc.moveNote(NoteId{999}, 4.0, 60));                               // unknown note
    EXPECT_EQ(doc.getRevision(), revisionBefore);
    EXPECT_EQ(listener.calls, callsBefore);

    EXPECT_TRUE(doc.moveNote(note, 2.0, 60)); // no-op: identical position
    EXPECT_EQ(doc.getRevision(), revisionBefore);
    EXPECT_EQ(listener.calls, callsBefore);

    ASSERT_TRUE(doc.moveNote(note, 10.0, 72));
    EXPECT_EQ(doc.getRevision(), revisionBefore + 1);
    EXPECT_EQ(listener.calls, callsBefore + 1);
    const auto* moved = doc.getNote(note);
    ASSERT_NE(moved, nullptr);
    EXPECT_DOUBLE_EQ(moved->startBeat, 10.0);
    EXPECT_EQ(moved->pitch, 72);
}

TEST_F(TimelineClipEditingTest, MoveNoteKeepsSortedInvariantAcrossOtherNotes) {
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto clip = doc.addClip(track, 0.0, 16.0, "c");
    const auto a = doc.addNote(clip, makeNote(0.0, 60));
    const auto b = doc.addNote(clip, makeNote(2.0, 60));
    const auto c = doc.addNote(clip, makeNote(4.0, 60));
    ASSERT_TRUE(a.isValid() && b.isValid() && c.isValid());

    ASSERT_TRUE(doc.moveNote(a, 3.0, 60)); // from the front to between b and c

    const auto& notes = doc.getClip(clip)->notes;
    ASSERT_EQ(notes.size(), 3u);
    EXPECT_EQ(notes[0].id, b);
    EXPECT_EQ(notes[1].id, a);
    EXPECT_EQ(notes[2].id, c);
    for (size_t i = 1; i < notes.size(); ++i)
        EXPECT_LE(notes[i - 1].startBeat, notes[i].startBeat);
}

TEST_F(TimelineClipEditingTest, ResizeNoteHappyPathAndRejections) {
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto clip = doc.addClip(track, 0.0, 8.0, "c");
    const auto note = doc.addNote(clip, makeNote(0.0, 60, 1.0));
    ASSERT_TRUE(note.isValid());
    const auto revisionBefore = doc.getRevision();

    EXPECT_FALSE(doc.resizeNote(note, 0.0));
    EXPECT_FALSE(doc.resizeNote(note, -1.0));
    EXPECT_FALSE(doc.resizeNote(note, std::numeric_limits<double>::infinity()));
    EXPECT_FALSE(doc.resizeNote(NoteId{999}, 2.0));
    EXPECT_EQ(doc.getRevision(), revisionBefore);

    EXPECT_TRUE(doc.resizeNote(note, 1.0)); // no-op: identical length
    EXPECT_EQ(doc.getRevision(), revisionBefore);

    ASSERT_TRUE(doc.resizeNote(note, 3.5));
    EXPECT_EQ(doc.getRevision(), revisionBefore + 1);
    EXPECT_DOUBLE_EQ(doc.getNote(note)->lengthBeats, 3.5);
}

TEST_F(TimelineClipEditingTest, SetNoteVelocityHappyPathAndRejections) {
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto clip = doc.addClip(track, 0.0, 8.0, "c");
    const auto note = doc.addNote(clip, makeNote(0.0, 60, 1.0, 100));
    ASSERT_TRUE(note.isValid());
    const auto revisionBefore = doc.getRevision();

    EXPECT_FALSE(doc.setNoteVelocity(note, 0));
    EXPECT_FALSE(doc.setNoteVelocity(note, 128));
    EXPECT_FALSE(doc.setNoteVelocity(NoteId{999}, 50));
    EXPECT_EQ(doc.getRevision(), revisionBefore);

    EXPECT_TRUE(doc.setNoteVelocity(note, 100)); // no-op: identical velocity
    EXPECT_EQ(doc.getRevision(), revisionBefore);

    ASSERT_TRUE(doc.setNoteVelocity(note, 42));
    EXPECT_EQ(doc.getRevision(), revisionBefore + 1);
    EXPECT_EQ(doc.getNote(note)->velocity, 42);
}

TEST_F(TimelineClipEditingTest, NoteLookupHelpersResolveOwner) {
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto clip = doc.addClip(track, 0.0, 8.0, "c");
    const auto note = doc.addNote(clip, makeNote(0.0, 60));
    ASSERT_TRUE(note.isValid());

    ASSERT_NE(doc.getClipForNote(note), nullptr);
    EXPECT_EQ(doc.getClipForNote(note)->id, clip);
    EXPECT_EQ(doc.getClipForNote(NoteId{999}), nullptr);
    EXPECT_EQ(doc.getNote(NoteId{999}), nullptr);
}

// ------------------------------------------------------------------ quantise --

TEST_F(TimelineClipEditingTest, QuantiseAtFullStrengthSnapsExactly) {
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto clip = doc.addClip(track, 0.0, 16.0, "c");
    const auto a = doc.addNote(clip, makeNote(0.9, 60, 2.0)); // nearest 1-beat grid -> 1.0
    const auto b = doc.addNote(clip, makeNote(2.4, 64, 0.5)); // nearest -> 2.0
    const auto c = doc.addNote(clip, makeNote(3.6, 67, 1.0)); // nearest -> 4.0
    ASSERT_TRUE(a.isValid() && b.isValid() && c.isValid());
    const auto revisionBefore = doc.getRevision();

    ASSERT_TRUE(doc.quantiseNotes(clip, 1.0, 1.0));
    EXPECT_EQ(doc.getRevision(), revisionBefore + 1); // one mutation, however many notes moved

    EXPECT_DOUBLE_EQ(doc.getNote(a)->startBeat, 1.0);
    EXPECT_DOUBLE_EQ(doc.getNote(a)->lengthBeats, 2.0); // lengths untouched
    EXPECT_DOUBLE_EQ(doc.getNote(b)->startBeat, 2.0);
    EXPECT_DOUBLE_EQ(doc.getNote(b)->lengthBeats, 0.5);
    EXPECT_DOUBLE_EQ(doc.getNote(c)->startBeat, 4.0);
    EXPECT_DOUBLE_EQ(doc.getNote(c)->lengthBeats, 1.0);

    const auto& notes = doc.getClip(clip)->notes;
    ASSERT_EQ(notes.size(), 3u);
    for (size_t i = 1; i < notes.size(); ++i)
        EXPECT_LE(notes[i - 1].startBeat, notes[i].startBeat);
}

TEST_F(TimelineClipEditingTest, QuantiseAtHalfStrengthBlendsTowardTheGrid) {
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto clip = doc.addClip(track, 0.0, 16.0, "c");
    const auto note = doc.addNote(clip, makeNote(0.8, 60)); // nearest grid 1.0; half-way -> 0.9
    ASSERT_TRUE(note.isValid());

    ASSERT_TRUE(doc.quantiseNotes(clip, 1.0, 0.5));
    EXPECT_DOUBLE_EQ(doc.getNote(note)->startBeat, 0.9);
}

TEST_F(TimelineClipEditingTest, QuantiseNoOpWhenNothingMoves) {
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto clip = doc.addClip(track, 0.0, 16.0, "c");
    ASSERT_TRUE(doc.addNote(clip, makeNote(2.0, 60)).isValid()); // already on the grid
    const auto revisionBefore = doc.getRevision();

    EXPECT_TRUE(doc.quantiseNotes(clip, 1.0, 1.0)); // exactly on-grid: no movement
    EXPECT_EQ(doc.getRevision(), revisionBefore);

    EXPECT_TRUE(doc.quantiseNotes(clip, 1.0, 0.0)); // zero strength: nothing ever moves
    EXPECT_EQ(doc.getRevision(), revisionBefore);
}

TEST_F(TimelineClipEditingTest, QuantiseRejectsInvalidGrid) {
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto clip = doc.addClip(track, 0.0, 16.0, "c");
    ASSERT_TRUE(doc.addNote(clip, makeNote(0.9, 60)).isValid());
    const auto revisionBefore = doc.getRevision();

    EXPECT_FALSE(doc.quantiseNotes(clip, 0.0, 1.0));
    EXPECT_FALSE(doc.quantiseNotes(clip, -1.0, 1.0));
    EXPECT_FALSE(doc.quantiseNotes(clip, std::numeric_limits<double>::quiet_NaN(), 1.0));
    EXPECT_FALSE(doc.quantiseNotes(ClipId{999}, 1.0, 1.0));
    EXPECT_EQ(doc.getRevision(), revisionBefore);
}

// -------------------------------------------------------------------- split --

TEST_F(TimelineClipEditingTest, SplitClipDividesStraddlingNoteAndRebasesTheRight) {
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto clip = doc.addClip(track, 4.0, 8.0, "c");             // absolute [4, 12)
    const auto left = doc.addNote(clip, makeNote(0.0, 60, 1.0));     // entirely left of atBeat=3
    const auto straddle = doc.addNote(clip, makeNote(2.0, 64, 3.0)); // [2, 5) straddles atBeat=3
    const auto right = doc.addNote(clip, makeNote(6.0, 67, 1.0));    // entirely right
    ASSERT_TRUE(left.isValid() && straddle.isValid() && right.isValid());
    const auto revisionBefore = doc.getRevision();

    const auto result = doc.splitClip(clip, 3.0);
    const auto leftId = result.first;
    const auto rightId = result.second;
    ASSERT_TRUE(leftId.isValid());
    ASSERT_TRUE(rightId.isValid());
    EXPECT_EQ(leftId, clip); // original id stays on the left part
    EXPECT_NE(rightId, clip);
    EXPECT_EQ(doc.getRevision(), revisionBefore + 1); // one mutation

    const auto* leftClip = doc.getClip(leftId);
    const auto* rightClip = doc.getClip(rightId);
    ASSERT_NE(leftClip, nullptr);
    ASSERT_NE(rightClip, nullptr);
    EXPECT_DOUBLE_EQ(leftClip->startBeat, 4.0);
    EXPECT_DOUBLE_EQ(leftClip->lengthBeats, 3.0);
    EXPECT_DOUBLE_EQ(rightClip->startBeat, 7.0);   // 4.0 + 3.0
    EXPECT_DOUBLE_EQ(rightClip->lengthBeats, 5.0); // 8.0 - 3.0

    ASSERT_EQ(leftClip->notes.size(), 2u); // entirely-left note + truncated straddle
    EXPECT_EQ(leftClip->notes[0].id, left);
    EXPECT_DOUBLE_EQ(leftClip->notes[0].startBeat, 0.0);
    EXPECT_DOUBLE_EQ(leftClip->notes[0].lengthBeats, 1.0);
    EXPECT_EQ(leftClip->notes[1].id, straddle); // keeps its id, truncated to the boundary
    EXPECT_DOUBLE_EQ(leftClip->notes[1].startBeat, 2.0);
    EXPECT_DOUBLE_EQ(leftClip->notes[1].lengthBeats, 1.0); // ends exactly at beat 3

    ASSERT_EQ(rightClip->notes.size(), 2u); // straddle's right half + entirely-right note
    EXPECT_NE(rightClip->notes[0].id, straddle);
    EXPECT_NE(rightClip->notes[0].id, left);
    EXPECT_NE(rightClip->notes[0].id, right);
    EXPECT_DOUBLE_EQ(rightClip->notes[0].startBeat, 0.0);
    EXPECT_DOUBLE_EQ(rightClip->notes[0].lengthBeats, 2.0); // remaining [3, 5) -> 2 beats
    EXPECT_EQ(rightClip->notes[0].pitch, 64);
    EXPECT_EQ(rightClip->notes[0].velocity, 100);
    EXPECT_EQ(rightClip->notes[1].id, right);             // entirely-right note, re-based
    EXPECT_DOUBLE_EQ(rightClip->notes[1].startBeat, 3.0); // 6.0 - 3.0
}

TEST_F(TimelineClipEditingTest, SplitClipRejectsOutOfRangeBeats) {
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto clip = doc.addClip(track, 0.0, 4.0, "c");
    const auto revisionBefore = doc.getRevision();

    EXPECT_FALSE(doc.splitClip(clip, 0.0).first.isValid());
    EXPECT_FALSE(doc.splitClip(clip, 4.0).first.isValid()); // == length
    EXPECT_FALSE(doc.splitClip(clip, 5.0).first.isValid()); // outside
    EXPECT_FALSE(doc.splitClip(clip, -1.0).first.isValid());
    EXPECT_FALSE(doc.splitClip(ClipId{999}, 2.0).first.isValid());
    EXPECT_EQ(doc.getRevision(), revisionBefore);
}

// --------------------------------------------------------------------- join --

TEST_F(TimelineClipEditingTest, JoinClipsRebasesAndMergesNotes) {
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto a = doc.addClip(track, 0.0, 4.0, "a");
    const auto b = doc.addClip(track, 6.0, 4.0, "b"); // gap [4, 6) becomes silence
    const auto noteA = doc.addNote(a, makeNote(1.0, 60));
    const auto noteB = doc.addNote(b, makeNote(0.5, 64));
    ASSERT_TRUE(noteA.isValid() && noteB.isValid());
    const auto revisionBefore = doc.getRevision();

    ASSERT_TRUE(doc.joinClips(a, b));
    EXPECT_EQ(doc.getRevision(), revisionBefore + 1);
    EXPECT_EQ(doc.getClip(b), nullptr); // b is gone

    const auto* joined = doc.getClip(a);
    ASSERT_NE(joined, nullptr);
    EXPECT_DOUBLE_EQ(joined->startBeat, 0.0);
    EXPECT_DOUBLE_EQ(joined->lengthBeats, 10.0); // [0, 6 + 4)

    ASSERT_EQ(joined->notes.size(), 2u);
    EXPECT_EQ(joined->notes[0].id, noteA);
    EXPECT_DOUBLE_EQ(joined->notes[0].startBeat, 1.0);
    EXPECT_EQ(joined->notes[1].id, noteB);
    EXPECT_DOUBLE_EQ(joined->notes[1].startBeat, 6.5); // 0.5 + (6.0 - 0.0)
}

TEST_F(TimelineClipEditingTest, JoinClipsRejectsOverlapCrossTrackSelfAndWrongOrder) {
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto other = doc.addTrack(TrackKind::Midi, "Other");
    const auto a = doc.addClip(track, 0.0, 4.0, "a");
    const auto overlapping = doc.addClip(track, 2.0, 4.0, "overlap"); // starts before a ends
    const auto crossTrack = doc.addClip(other, 8.0, 4.0, "cross");
    const auto revisionBefore = doc.getRevision();

    EXPECT_FALSE(doc.joinClips(a, overlapping)); // overlap rejected
    EXPECT_FALSE(doc.joinClips(overlapping, a)); // wrong order (b would start before a)
    EXPECT_FALSE(doc.joinClips(a, crossTrack));  // different tracks
    EXPECT_FALSE(doc.joinClips(a, a));           // self-join
    EXPECT_FALSE(doc.joinClips(a, ClipId{999})); // unknown clip
    EXPECT_EQ(doc.getRevision(), revisionBefore);
}

TEST_F(TimelineClipEditingTest, JoinClipsRejectsWhenMergedNoteCountExceedsCap) {
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto a = doc.addClip(track, 0.0, 1.0, "a");
    const auto b = doc.addClip(track, 2.0, 1.0, "b");
    for (int i = 0; i < TimelineDoc::kMaxNotesPerClip; ++i)
        ASSERT_TRUE(doc.addNote(a, makeNote(0.0, 60)).isValid()) << "note " << i;
    ASSERT_TRUE(doc.addNote(b, makeNote(0.0, 60)).isValid());
    const auto revisionBefore = doc.getRevision();

    EXPECT_FALSE(doc.joinClips(a, b));
    EXPECT_EQ(doc.getRevision(), revisionBefore);
}

// --------------------------------------------------------------- duplicate --

TEST_F(TimelineClipEditingTest, DuplicateClipPlacesCopyAfterWithFreshNoteIds) {
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto clip = doc.addClip(track, 0.0, 4.0, "c");
    const auto note = doc.addNote(clip, makeNote(1.0, 60, 2.0, 90, 3));
    ASSERT_TRUE(note.isValid());
    const auto revisionBefore = doc.getRevision();

    const auto dup = doc.duplicateClip(clip);
    ASSERT_TRUE(dup.isValid());
    EXPECT_NE(dup, clip);
    EXPECT_EQ(doc.getRevision(), revisionBefore + 1);

    const auto* dupClip = doc.getClip(dup);
    ASSERT_NE(dupClip, nullptr);
    EXPECT_EQ(dupClip->name, "c");
    EXPECT_DOUBLE_EQ(dupClip->startBeat, 4.0); // start + length
    EXPECT_DOUBLE_EQ(dupClip->lengthBeats, 4.0);
    ASSERT_EQ(dupClip->notes.size(), 1u);
    EXPECT_NE(dupClip->notes[0].id, note); // fresh id
    EXPECT_DOUBLE_EQ(dupClip->notes[0].startBeat, 1.0);
    EXPECT_DOUBLE_EQ(dupClip->notes[0].lengthBeats, 2.0);
    EXPECT_EQ(dupClip->notes[0].pitch, 60);
    EXPECT_EQ(dupClip->notes[0].velocity, 90);
    EXPECT_EQ(dupClip->notes[0].channel, 3);

    // Deep copy: mutating the source note doesn't affect the duplicate.
    const auto dupNoteId = dupClip->notes[0].id;
    ASSERT_TRUE(doc.setNoteVelocity(note, 42));
    EXPECT_EQ(doc.getNote(dupNoteId)->velocity, 90);
}

TEST_F(TimelineClipEditingTest, DuplicateClipRejectsUnknownIdAndRespectsCap) {
    EXPECT_FALSE(doc.duplicateClip(ClipId{999}).isValid());

    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto clip = doc.addClip(track, 0.0, 1.0, "c");
    for (int i = 1; i < TimelineDoc::kMaxClipsPerTrack; ++i)
        ASSERT_TRUE(doc.addClip(track, static_cast<double>(i) + 10.0, 1.0, "filler").isValid());
    EXPECT_EQ(static_cast<int>(doc.getTrack(track)->clips.size()), TimelineDoc::kMaxClipsPerTrack);

    EXPECT_FALSE(doc.duplicateClip(clip).isValid()); // track already at the cap
}

// ---------------------------------------------------------------- round trip --

TEST_F(TimelineClipEditingTest, RoundTripSurvivesSplitJoinAndDuplicate) {
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto clip = doc.addClip(track, 0.0, 8.0, "c");
    doc.addNote(clip, makeNote(1.0, 60));
    doc.addNote(clip, makeNote(5.0, 64));

    const auto splitResult = doc.splitClip(clip, 4.0);
    const auto leftId = splitResult.first;
    ASSERT_TRUE(leftId.isValid());
    ASSERT_TRUE(splitResult.second.isValid());

    const auto dup = doc.duplicateClip(leftId);
    ASSERT_TRUE(dup.isValid());

    const auto otherTrack = doc.addTrack(TrackKind::Midi, "T2");
    const auto a = doc.addClip(otherTrack, 0.0, 2.0, "a");
    const auto b = doc.addClip(otherTrack, 2.0, 2.0, "b");
    doc.addNote(a, makeNote(0.5, 50));
    doc.addNote(b, makeNote(0.5, 55));
    ASSERT_TRUE(doc.joinClips(a, b));

    const auto before = juce::JSON::toString(doc.toVar());

    TimelineDoc loaded;
    ASSERT_TRUE(loaded.fromVar(doc.toVar()));
    EXPECT_EQ(juce::JSON::toString(loaded.toVar()), before);

    // Id counters floor correctly: a note added after load doesn't collide with anything that
    // came off the file.
    std::vector<NoteId> allNoteIds;
    for (const auto& t : loaded.getTracks())
        for (const auto& c : t.clips)
            for (const auto& n : c.notes)
                allNoteIds.push_back(n.id);

    const auto newNote = loaded.addNote(leftId, makeNote(7.0, 30));
    ASSERT_TRUE(newNote.isValid());
    EXPECT_EQ(std::count(allNoteIds.begin(), allNoteIds.end(), newNote), 0);

    const auto newClip = loaded.addClip(track, 100.0, 1.0, "after load");
    ASSERT_TRUE(newClip.isValid());
    EXPECT_GT(newClip.value, dup.value);
}

TEST_F(TimelineClipEditingTest, FromVarRejectsDuplicateNoteIdsWithinAClip) {
    const auto* text = R"({"version":1,"tracks":[{"id":1,"clips":[{"id":1,"startBeat":0.0,"lengthBeats":4.0,
        "notes":[{"id":5,"startBeat":0.0,"lengthBeats":1.0,"pitch":60,"velocity":100,"channel":1},
                 {"id":5,"startBeat":1.0,"lengthBeats":1.0,"pitch":61,"velocity":100,"channel":1}]}]}]})";
    EXPECT_FALSE(doc.fromVar(juce::JSON::parse(text)));
    EXPECT_TRUE(doc.isEmpty());
}

TEST_F(TimelineClipEditingTest, FromVarRejectsDuplicateNoteIdsAcrossClips) {
    const auto* text = R"({"version":1,"tracks":[{"id":1,"clips":[
        {"id":1,"startBeat":0.0,"lengthBeats":4.0,
         "notes":[{"id":7,"startBeat":0.0,"lengthBeats":1.0,"pitch":60,"velocity":100,"channel":1}]},
        {"id":2,"startBeat":4.0,"lengthBeats":4.0,
         "notes":[{"id":7,"startBeat":0.0,"lengthBeats":1.0,"pitch":61,"velocity":100,"channel":1}]}
    ]}]})";
    EXPECT_FALSE(doc.fromVar(juce::JSON::parse(text)));
    EXPECT_TRUE(doc.isEmpty());
}

TEST_F(TimelineClipEditingTest, FromVarRejectsMissingNoteId) {
    const auto* text = R"({"version":1,"tracks":[{"id":1,"clips":[{"id":1,"startBeat":0.0,"lengthBeats":4.0,
        "notes":[{"startBeat":0.0,"lengthBeats":1.0,"pitch":60,"velocity":100,"channel":1}]}]}]})";
    EXPECT_FALSE(doc.fromVar(juce::JSON::parse(text)));
    EXPECT_TRUE(doc.isEmpty());
}
