// Note identity + the note-editing/clip-operations API on TimelineDoc: removeNote/moveNote/
// resizeNote/setNoteVelocity/quantiseNotes and splitClip/joinClips/duplicateClip.
//
// Plus (second half of the file) the CLIP-LANE EDIT TOOLS those doc operations back — Split, Glue,
// Erase, Mute and Draw driven through synth::ui::TimelineClipLaneArea with synthetic mouse events,
// the Select tool's Alt-copy and cross-track drags, the Split tool's hover preview repaint budget,
// and the inline rename's commit path. Every one of those tests configures the view state (snap
// division, snap switch, zoom, scroll) EXPLICITLY: a tool's whole behaviour is defined against the
// grid, so inheriting a default — let alone a persisted user setting — would make the file's
// results machine-dependent.
#include "../Source/AppUndoManager.h"
#include "../Source/UI/ClipSelectionModel.h"
#include "../Source/UI/EditTool.h"
#include "../Source/UI/TimelineClipLaneArea.h"
#include "../Source/UI/TimelineViewState.h"
#include "Timeline/TimelineDoc.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>
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

// ============================================================================
// Clip-lane EDIT TOOLS (synth::ui::TimelineClipLaneArea)
// ============================================================================

namespace {

using synth::ui::EditTool;
using synth::ui::TimelineClipLaneArea;
using synth::ui::TimelineViewState;

// 40 px/beat, 1-beat snap grid, nothing scrolled, no vertical zoom — every coordinate below is
// derived from these, and every one of them is set explicitly (see the file header).
struct ToolLaneFixture {
    TimelineDoc doc;
    TimelineViewState state;
    synth::ui::ClipSelectionModel selection;
    AppUndoManager undo;
    TimelineClipLaneArea lane{state, selection};

    ToolLaneFixture() {
        state.pixelsPerBeat = 40.0;
        state.firstVisibleBeat = 0.0;
        state.snap = TimelineViewState::Snap::Quarter; // a quarter note == 1 beat
        state.snapEnabled = true;
        state.rowHeightScale = 1.0;
        state.trackScrollY = 0.0;
        lane.setTimelineDoc(&doc);
        lane.setUndoManager(&undo);
        lane.setSize(1200, 400);
    }

    // The vertical centre of track row `index`, headless (no themed LookAndFeel => the row height
    // is TimelineTrackHeaderComponent::kRowHeight).
    float rowCentreY(int index) const {
        const int rowHeight = lane.getRowHeight();
        return (float)(index * rowHeight + rowHeight / 2);
    }
    float rowHeightF() const { return (float)lane.getRowHeight(); }
};

// Hand-built MouseEvents, same pattern as TimelineClipLaneTests.cpp/GraphEditorTests.cpp — no OS
// mouse source exists headlessly, and `mouseWasDragged` is the constructor's own bool.
juce::MouseEvent makeToolMouseEvent(juce::Component& comp, juce::Point<float> position, juce::ModifierKeys mods,
                                    bool mouseWasDragged, juce::Point<float> mouseDownPos) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), position, mods, 0.0f, 0.0f, 0.0f, 0.0f,
                            0.0f, &comp, &comp, juce::Time::getCurrentTime(), mouseDownPos,
                            juce::Time::getCurrentTime(), 1, mouseWasDragged);
}

juce::MouseEvent toolClick(juce::Component& comp, juce::Point<float> pos, int extraFlags = 0) {
    return makeToolMouseEvent(comp, pos, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | extraFlags), false,
                              pos);
}

juce::MouseEvent toolDrag(juce::Component& comp, juce::Point<float> pos, juce::Point<float> anchor,
                          int extraFlags = 0) {
    return makeToolMouseEvent(comp, pos, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | extraFlags), true,
                              anchor);
}

juce::MouseEvent hoverAt(juce::Component& comp, juce::Point<float> pos) {
    return makeToolMouseEvent(comp, pos, juce::ModifierKeys(), false, pos);
}

juce::Point<float> clipCentre(const TimelineClipLaneArea& lane, ClipId id) {
    const auto rect = lane.getClipRect(id);
    return {(float)rect.getCentreX(), (float)rect.getCentreY()};
}

// One press-release with no movement — what "clicking with a tool" means for Split/Glue/Erase/
// Mute (they all act on the press; the release is what proves it does not act twice).
void clickWithTool(TimelineClipLaneArea& lane, juce::Point<float> pos, int extraFlags = 0) {
    lane.mouseDown(toolClick(lane, pos, extraFlags));
    lane.mouseUp(toolClick(lane, pos, extraFlags));
}

} // namespace

// ------------------------------------------------------------- Split tool --

TEST(TimelineClipToolTest, SplitToolClickSplitsAtTheSnappedBeat) {
    ToolLaneFixture f;
    const auto track = f.doc.addTrack(TrackKind::Midi, "T");
    const auto clip = f.doc.addClip(track, 0.0, 8.0, "c"); // x in [0, 320)
    ASSERT_TRUE(clip.isValid());
    f.lane.setActiveTool(EditTool::Split);

    // x = 140 -> beat 3.5 -> snapped (ties round up) to 4.0.
    clickWithTool(f.lane, {140.0f, f.rowCentreY(0)});

    const auto& clips = f.doc.getTrack(track)->clips;
    ASSERT_EQ(clips.size(), 2u);
    EXPECT_DOUBLE_EQ(clips[0].startBeat, 0.0);
    EXPECT_DOUBLE_EQ(clips[0].lengthBeats, 4.0);
    EXPECT_DOUBLE_EQ(clips[1].startBeat, 4.0);
    EXPECT_DOUBLE_EQ(clips[1].lengthBeats, 4.0);

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.doc.getTrack(track)->clips.size(), 1u);
    EXPECT_FALSE(f.undo.canUndo()) << "one click was ONE undo step";
}

TEST(TimelineClipToolTest, SplitToolClickOnEmptyLaneSpaceDoesNothing) {
    ToolLaneFixture f;
    const auto track = f.doc.addTrack(TrackKind::Midi, "T");
    ASSERT_TRUE(f.doc.addClip(track, 0.0, 4.0, "c").isValid());
    f.lane.setActiveTool(EditTool::Split);
    const auto revisionBefore = f.doc.getRevision();

    clickWithTool(f.lane, {900.0f, f.rowCentreY(0)}); // far right of the clip

    EXPECT_EQ(f.doc.getRevision(), revisionBefore);
    EXPECT_FALSE(f.undo.canUndo());
    EXPECT_TRUE(f.selection.isEmpty()) << "a tool click must not select either";
}

TEST(TimelineClipToolTest, SplitToolClickAtTheClipBoundaryDoesNothing) {
    ToolLaneFixture f;
    const auto track = f.doc.addTrack(TrackKind::Midi, "T");
    const auto clip = f.doc.addClip(track, 0.0, 8.0, "c"); // x in [0, 320)
    ASSERT_TRUE(clip.isValid());
    f.lane.setActiveTool(EditTool::Split);
    const auto revisionBefore = f.doc.getRevision();

    // x = 1 -> beat 0.025 -> snapped to 0.0, exactly the clip's own start: not strictly inside.
    clickWithTool(f.lane, {1.0f, f.rowCentreY(0)});
    EXPECT_EQ(f.doc.getRevision(), revisionBefore);
    EXPECT_FALSE(f.undo.canUndo());
    EXPECT_EQ(f.doc.getTrack(track)->clips.size(), 1u);

    // x = 319 -> beat 7.975 -> snapped to 8.0, exactly the clip's own end: not strictly inside.
    clickWithTool(f.lane, {319.0f, f.rowCentreY(0)});
    EXPECT_EQ(f.doc.getRevision(), revisionBefore);
    EXPECT_FALSE(f.undo.canUndo());
    EXPECT_EQ(f.doc.getTrack(track)->clips.size(), 1u);
}

// -------------------------------------------------------------- Glue tool --

TEST(TimelineClipToolTest, GlueToolJoinsWithTheNextClipAcrossAGap) {
    ToolLaneFixture f;
    const auto track = f.doc.addTrack(TrackKind::Midi, "T");
    const auto a = f.doc.addClip(track, 0.0, 4.0, "a");
    const auto b = f.doc.addClip(track, 6.0, 4.0, "b"); // gap [4, 6): legal, becomes silence
    ASSERT_TRUE(a.isValid() && b.isValid());
    EXPECT_EQ(f.lane.findGlueTarget(a), b);
    f.lane.setActiveTool(EditTool::Glue);

    clickWithTool(f.lane, clipCentre(f.lane, a));

    ASSERT_EQ(f.doc.getTrack(track)->clips.size(), 1u);
    const auto* joined = f.doc.getClip(a);
    ASSERT_NE(joined, nullptr);
    EXPECT_DOUBLE_EQ(joined->startBeat, 0.0);
    EXPECT_DOUBLE_EQ(joined->lengthBeats, 10.0);

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.doc.getTrack(track)->clips.size(), 2u);
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(TimelineClipToolTest, GlueToolPrunesTheSwallowedClipFromTheSelection) {
    ToolLaneFixture f;
    const auto track = f.doc.addTrack(TrackKind::Midi, "T");
    const auto a = f.doc.addClip(track, 0.0, 4.0, "a");
    const auto b = f.doc.addClip(track, 6.0, 4.0, "b"); // gap [4, 6): legal, becomes silence
    ASSERT_TRUE(a.isValid() && b.isValid());
    EXPECT_EQ(f.lane.findGlueTarget(a), b);
    f.selection.setSelection({a, b});
    f.lane.setActiveTool(EditTool::Glue);

    clickWithTool(f.lane, clipCentre(f.lane, a));

    ASSERT_EQ(f.doc.getTrack(track)->clips.size(), 1u);
    EXPECT_TRUE(f.selection.contains(a)) << "the survivor stays selected";
    EXPECT_FALSE(f.selection.contains(b)) << "the absorbed clip leaves the selection";
}

TEST(TimelineClipToolTest, GlueToolWithNothingAfterItLeavesNoUndoEntry) {
    ToolLaneFixture f;
    const auto track = f.doc.addTrack(TrackKind::Midi, "T");
    const auto only = f.doc.addClip(track, 0.0, 4.0, "only");
    ASSERT_TRUE(only.isValid());
    EXPECT_FALSE(f.lane.findGlueTarget(only).isValid());
    f.lane.setActiveTool(EditTool::Glue);
    const auto revisionBefore = f.doc.getRevision();

    clickWithTool(f.lane, clipCentre(f.lane, only));

    EXPECT_EQ(f.doc.getRevision(), revisionBefore);
    EXPECT_FALSE(f.undo.canUndo()) << "a refused glue must not push an empty undo step";
}

TEST(TimelineClipToolTest, GlueTargetSkipsAnOverlappingClip) {
    ToolLaneFixture f;
    const auto track = f.doc.addTrack(TrackKind::Midi, "T");
    const auto a = f.doc.addClip(track, 0.0, 4.0, "a");
    ASSERT_TRUE(f.doc.addClip(track, 2.0, 4.0, "overlapping").isValid()); // joinClips refuses this
    const auto after = f.doc.addClip(track, 8.0, 2.0, "after");
    ASSERT_TRUE(a.isValid() && after.isValid());

    EXPECT_EQ(f.lane.findGlueTarget(a), after) << "the first NON-overlapping clip is the target";
}

// ------------------------------------------------------------- Erase tool --

TEST(TimelineClipToolTest, EraseToolClickDeletesTheClipItHits) {
    ToolLaneFixture f;
    const auto track = f.doc.addTrack(TrackKind::Midi, "T");
    const auto a = f.doc.addClip(track, 0.0, 4.0, "a");
    const auto b = f.doc.addClip(track, 8.0, 4.0, "b");
    ASSERT_TRUE(a.isValid() && b.isValid());
    // Selection-independent: b is selected, and clicking a still erases a.
    f.selection.setSelection({b});
    f.lane.setActiveTool(EditTool::Erase);

    clickWithTool(f.lane, clipCentre(f.lane, a));

    EXPECT_EQ(f.doc.getClip(a), nullptr);
    ASSERT_NE(f.doc.getClip(b), nullptr);
    EXPECT_TRUE(f.selection.contains(b));

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.doc.getTrack(track)->clips.size(), 2u);
    EXPECT_FALSE(f.undo.canUndo());
}

// -------------------------------------------------------------- Mute tool --

TEST(TimelineClipToolTest, MuteToolClickTogglesTheClipFlagBothWays) {
    ToolLaneFixture f;
    const auto track = f.doc.addTrack(TrackKind::Midi, "T");
    const auto clip = f.doc.addClip(track, 0.0, 4.0, "c");
    ASSERT_TRUE(clip.isValid());
    ASSERT_FALSE(f.doc.getClip(clip)->muted);
    f.lane.setActiveTool(EditTool::Mute);

    clickWithTool(f.lane, clipCentre(f.lane, clip));
    EXPECT_TRUE(f.doc.getClip(clip)->muted) << "one press, one toggle — the release must not toggle back";

    clickWithTool(f.lane, clipCentre(f.lane, clip));
    EXPECT_FALSE(f.doc.getClip(clip)->muted);

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_TRUE(f.doc.getClip(clip)->muted) << "each toggle is its own undo step";
}

// -------------------------------------------------------------- Draw tool --

TEST(TimelineClipToolTest, DrawToolDragCreatesAClipOfTheDraggedLength) {
    ToolLaneFixture f;
    const auto track = f.doc.addTrack(TrackKind::Midi, "T");
    f.lane.setActiveTool(EditTool::Draw);

    const juce::Point<float> anchor(40.0f, f.rowCentreY(0));   // beat 1.0, floor-snapped to 1.0
    const juce::Point<float> release(200.0f, f.rowCentreY(0)); // beat 5.0, ceil-snapped to 5.0
    f.lane.mouseDown(toolClick(f.lane, anchor));
    f.lane.mouseDrag(toolDrag(f.lane, release, anchor));
    EXPECT_FALSE(f.lane.getDrawGhostRectForTest().isEmpty()) << "the drag previews a ghost";
    f.lane.mouseUp(toolDrag(f.lane, release, anchor));

    const auto& clips = f.doc.getTrack(track)->clips;
    ASSERT_EQ(clips.size(), 1u);
    EXPECT_DOUBLE_EQ(clips[0].startBeat, 1.0);
    EXPECT_DOUBLE_EQ(clips[0].lengthBeats, 4.0);
    EXPECT_TRUE(f.selection.contains(clips[0].id));
    EXPECT_TRUE(f.lane.getDrawGhostRectForTest().isEmpty()) << "the ghost is gone once the clip is real";

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_TRUE(f.doc.getTrack(track)->clips.empty());
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(TimelineClipToolTest, DrawToolPlainClickCreatesTheOneBarClip) {
    ToolLaneFixture f;
    const auto track = f.doc.addTrack(TrackKind::Midi, "T");
    ClipId opened;
    f.lane.onClipDoubleClicked = [&opened](ClipId id) { opened = id; };
    f.lane.setActiveTool(EditTool::Draw);

    clickWithTool(f.lane, {40.0f, f.rowCentreY(0)}); // beat 1.0, no drag

    const auto& clips = f.doc.getTrack(track)->clips;
    ASSERT_EQ(clips.size(), 1u);
    EXPECT_DOUBLE_EQ(clips[0].startBeat, 1.0);
    EXPECT_DOUBLE_EQ(clips[0].lengthBeats, 4.0) << "one bar at the no-transport 4/4 fallback";
    EXPECT_EQ(opened, clips[0].id) << "the pencil click lands in the note editor, like the double-click";
}

TEST(TimelineClipToolTest, DrawToolIsInertOnAnAudioRow) {
    ToolLaneFixture f;
    const auto audio = f.doc.addTrack(TrackKind::Audio, "A");
    ASSERT_TRUE(audio.isValid());
    f.lane.setActiveTool(EditTool::Draw);
    const auto revisionBefore = f.doc.getRevision();

    const juce::Point<float> anchor(40.0f, f.rowCentreY(0));
    const juce::Point<float> release(200.0f, f.rowCentreY(0));
    f.lane.mouseDown(toolClick(f.lane, anchor));
    f.lane.mouseDrag(toolDrag(f.lane, release, anchor));
    f.lane.mouseUp(toolDrag(f.lane, release, anchor));

    EXPECT_TRUE(f.doc.getTrack(audio)->clips.empty()) << "a pencil cannot draw an asset";
    EXPECT_EQ(f.doc.getRevision(), revisionBefore);
    EXPECT_FALSE(f.undo.canUndo());
}

// ------------------------------------------------ Split tool hover preview --

namespace {
// The repaint-count seam, same subclass-and-count idiom PianoRollComponent's playhead strip uses.
class CountingToolLane : public TimelineClipLaneArea {
public:
    using TimelineClipLaneArea::TimelineClipLaneArea;
    int previewRepaints = 0;

protected:
    void requestToolPreviewRepaint(juce::Rectangle<int> region) override {
        ++previewRepaints;
        TimelineClipLaneArea::requestToolPreviewRepaint(region);
    }
};
} // namespace

TEST(TimelineClipToolTest, SplitPreviewRepaintsOnlyWhenTheSnappedBeatChanges) {
    TimelineDoc doc;
    TimelineViewState state;
    state.pixelsPerBeat = 40.0;
    state.firstVisibleBeat = 0.0;
    state.snap = TimelineViewState::Snap::Quarter;
    state.snapEnabled = true;
    state.rowHeightScale = 1.0;
    state.trackScrollY = 0.0;
    synth::ui::ClipSelectionModel selection;
    CountingToolLane lane{state, selection};
    lane.setTimelineDoc(&doc);
    lane.setSize(1200, 400);

    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto clip = doc.addClip(track, 0.0, 8.0, "c"); // x in [0, 320)
    ASSERT_TRUE(clip.isValid());
    lane.setActiveTool(EditTool::Split);
    lane.previewRepaints = 0;

    const float y = (float)(lane.getRowHeight() / 2);

    lane.mouseMove(hoverAt(lane, {100.0f, y})); // beat 2.5 -> snapped 3.0
    ASSERT_TRUE(lane.getSplitPreviewForTest().has_value());
    EXPECT_DOUBLE_EQ(lane.getSplitPreviewForTest()->beat, 3.0);
    EXPECT_EQ(lane.previewRepaints, 1);

    // Both of these still snap to 3.0 — the preview is unchanged, so nothing repaints.
    lane.mouseMove(hoverAt(lane, {105.0f, y})); // beat 2.625
    lane.mouseMove(hoverAt(lane, {125.0f, y})); // beat 3.125
    EXPECT_EQ(lane.previewRepaints, 1) << "movement inside one snap cell must cost zero repaints";
    EXPECT_DOUBLE_EQ(lane.getSplitPreviewForTest()->beat, 3.0);

    lane.mouseMove(hoverAt(lane, {140.0f, y})); // beat 3.5 -> snapped 4.0
    EXPECT_EQ(lane.previewRepaints, 2) << "crossing into the next cell costs exactly one";
    EXPECT_DOUBLE_EQ(lane.getSplitPreviewForTest()->beat, 4.0);

    // Leaving the lanes drops the line (one more repaint, over where it was).
    lane.mouseExit(hoverAt(lane, {140.0f, y}));
    EXPECT_FALSE(lane.getSplitPreviewForTest().has_value());
    EXPECT_EQ(lane.previewRepaints, 3);
}

// ------------------------------------------------------- Alt-drag copy ------

TEST(TimelineClipToolTest, AltDragCopiesTheSelectionInOneUndoStep) {
    ToolLaneFixture f;
    const auto track = f.doc.addTrack(TrackKind::Midi, "T");
    const auto clip = f.doc.addClip(track, 0.0, 4.0, "c");
    ASSERT_TRUE(clip.isValid());
    ASSERT_TRUE(f.doc.addNote(clip, makeNote(1.0, 64)).isValid());
    f.selection.setSelection({clip});

    const auto anchor = clipCentre(f.lane, clip);
    const juce::Point<float> dragged(anchor.x + 80.0f, anchor.y); // +2.0 beats at 40 px/beat
    const int alt = juce::ModifierKeys::altModifier;

    f.lane.mouseDown(toolClick(f.lane, anchor, alt));
    f.lane.mouseDrag(toolDrag(f.lane, dragged, anchor, alt));
    EXPECT_TRUE(f.lane.isCopyDragForTest());
    EXPECT_DOUBLE_EQ(f.doc.getClip(clip)->startBeat, 0.0) << "the original must not move mid-drag";
    f.lane.mouseUp(toolDrag(f.lane, dragged, anchor, alt));

    const auto& clips = f.doc.getTrack(track)->clips;
    ASSERT_EQ(clips.size(), 2u);
    EXPECT_DOUBLE_EQ(clips[0].startBeat, 0.0) << "the original stayed exactly where it was";
    EXPECT_DOUBLE_EQ(clips[1].startBeat, 2.0);
    EXPECT_EQ(clips[1].notes.size(), 1u) << "a copy is a deep copy";

    const auto selected = f.selection.getSelected();
    ASSERT_EQ(selected.size(), 1u);
    EXPECT_EQ(selected[0], clips[1].id) << "the COPY ends up selected";

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.doc.getTrack(track)->clips.size(), 1u);
    EXPECT_FALSE(f.undo.canUndo()) << "the whole copy-drag was ONE undo step";
}

TEST(TimelineClipToolTest, AltClickWithoutADragCopiesNothing) {
    ToolLaneFixture f;
    const auto track = f.doc.addTrack(TrackKind::Midi, "T");
    const auto clip = f.doc.addClip(track, 0.0, 4.0, "c");
    ASSERT_TRUE(clip.isValid());
    f.selection.setSelection({clip});

    clickWithTool(f.lane, clipCentre(f.lane, clip), juce::ModifierKeys::altModifier);

    EXPECT_EQ(f.doc.getTrack(track)->clips.size(), 1u);
    EXPECT_FALSE(f.undo.canUndo());
}

// The user-visible half of the Alt copy-drag, and the one the doc assertions above could not
// see: what is on SCREEN between mouseDown and mouseUp. The originals must not move on either
// axis (the doc is untouched mid-drag either way, so only the effective geometry can prove it),
// and the ghosts must carry the delta instead.
TEST(TimelineClipToolTest, AltDragLeavesEveryOriginalInPlaceWhileTheGhostsCarryTheDelta) {
    ToolLaneFixture f;
    const auto track = f.doc.addTrack(TrackKind::Midi, "T");
    const auto first = f.doc.addClip(track, 0.0, 4.0, "a");
    const auto second = f.doc.addClip(track, 8.0, 2.0, "b");
    ASSERT_TRUE(first.isValid());
    ASSERT_TRUE(second.isValid());
    f.selection.setSelection({first, second});

    const auto anchor = clipCentre(f.lane, first);
    const juce::Point<float> dragged(anchor.x + 80.0f, anchor.y); // +2.0 beats at 40 px/beat
    const int alt = juce::ModifierKeys::altModifier;

    f.lane.mouseDown(toolClick(f.lane, anchor, alt));
    f.lane.mouseDrag(toolDrag(f.lane, dragged, anchor, alt));
    ASSERT_TRUE(f.lane.isCopyDragForTest());

    // Both originals paint at their DOC geometry — this is the regression: before the fix the
    // start followed previewDeltaBeats_ while only the row was copy-guarded, so the original
    // slid sideways under the pointer and the gesture read as a move.
    const auto firstGeometry = f.lane.getEffectiveGeometryForTest(first);
    ASSERT_TRUE(firstGeometry.has_value());
    EXPECT_DOUBLE_EQ(firstGeometry->first, 0.0) << "an Alt-dragged original must not move mid-drag";
    EXPECT_DOUBLE_EQ(firstGeometry->second, 4.0);
    const auto secondGeometry = f.lane.getEffectiveGeometryForTest(second);
    ASSERT_TRUE(secondGeometry.has_value());
    EXPECT_DOUBLE_EQ(secondGeometry->first, 8.0) << "every original in the selection, not just the grabbed one";
    EXPECT_DOUBLE_EQ(secondGeometry->second, 2.0);

    // The ghosts are what moved — one per dragged clip, each at origin + the shared delta.
    const int rowHeight = f.lane.getRowHeight();
    const auto ghosts = f.lane.getDragGhostRectsForTest();
    ASSERT_EQ(ghosts.size(), 2u);
    EXPECT_EQ(ghosts[0], synth::ui::TimelineClipLaneArea::computeClipRect(f.state, 0, 2.0, 4.0, rowHeight));
    EXPECT_EQ(ghosts[1], synth::ui::TimelineClipLaneArea::computeClipRect(f.state, 0, 10.0, 2.0, rowHeight));

    f.lane.mouseUp(toolDrag(f.lane, dragged, anchor, alt));
    EXPECT_TRUE(f.lane.getDragGhostRectsForTest().empty()) << "the ghosts end with the gesture";
}

TEST(TimelineClipToolTest, AltDragAcrossRowsPutsOnlyTheGhostOnTheDestinationRow) {
    ToolLaneFixture f;
    const auto upper = f.doc.addTrack(TrackKind::Midi, "Upper");
    ASSERT_TRUE(f.doc.addTrack(TrackKind::Midi, "Lower").isValid());
    const auto clip = f.doc.addClip(upper, 0.0, 4.0, "c");
    ASSERT_TRUE(clip.isValid());
    f.selection.setSelection({clip});

    const auto anchor = clipCentre(f.lane, clip);
    const juce::Point<float> dragged(anchor.x + 80.0f, anchor.y + f.rowHeightF()); // one row down, +2 beats
    const int alt = juce::ModifierKeys::altModifier;

    f.lane.mouseDown(toolClick(f.lane, anchor, alt));
    f.lane.mouseDrag(toolDrag(f.lane, dragged, anchor, alt));
    ASSERT_TRUE(f.lane.isCopyDragForTest());
    EXPECT_EQ(f.lane.getPreviewRowDeltaForTest(), 1);

    const auto geometry = f.lane.getEffectiveGeometryForTest(clip);
    ASSERT_TRUE(geometry.has_value());
    EXPECT_DOUBLE_EQ(geometry->first, 0.0) << "the vertical half of the drag must not move the original either";

    const auto ghosts = f.lane.getDragGhostRectsForTest();
    ASSERT_EQ(ghosts.size(), 1u);
    EXPECT_EQ(ghosts[0], synth::ui::TimelineClipLaneArea::computeClipRect(f.state, 1, 2.0, 4.0, f.lane.getRowHeight()))
        << "the ghost is the only thing on the destination row";
}

// The other side of the same guard: a PLAIN move-drag still previews on the original, which is
// what makes a normal drag look like the clip following the pointer.
TEST(TimelineClipToolTest, PlainMoveDragStillPreviewsTheDeltaOnTheOriginal) {
    ToolLaneFixture f;
    const auto track = f.doc.addTrack(TrackKind::Midi, "T");
    const auto clip = f.doc.addClip(track, 0.0, 4.0, "c");
    ASSERT_TRUE(clip.isValid());
    f.selection.setSelection({clip});

    const auto anchor = clipCentre(f.lane, clip);
    const juce::Point<float> dragged(anchor.x + 80.0f, anchor.y);

    f.lane.mouseDown(toolClick(f.lane, anchor));
    f.lane.mouseDrag(toolDrag(f.lane, dragged, anchor));
    EXPECT_FALSE(f.lane.isCopyDragForTest());

    const auto geometry = f.lane.getEffectiveGeometryForTest(clip);
    ASSERT_TRUE(geometry.has_value());
    EXPECT_DOUBLE_EQ(geometry->first, 2.0) << "a plain drag previews on the clip itself";
    EXPECT_DOUBLE_EQ(geometry->second, 4.0);
    EXPECT_TRUE(f.lane.getDragGhostRectsForTest().empty()) << "and draws no ghosts at all";

    EXPECT_DOUBLE_EQ(f.doc.getClip(clip)->startBeat, 0.0) << "the DOC still only changes on mouseUp";
    f.lane.mouseUp(toolDrag(f.lane, dragged, anchor));
    EXPECT_DOUBLE_EQ(f.doc.getClip(clip)->startBeat, 2.0);
}

// ------------------------------------------------------- Cross-track drag ---

TEST(TimelineClipToolTest, CrossTrackDragMovesTheClipToTheRowBelow) {
    ToolLaneFixture f;
    const auto upper = f.doc.addTrack(TrackKind::Midi, "Upper");
    const auto lower = f.doc.addTrack(TrackKind::Midi, "Lower");
    const auto clip = f.doc.addClip(upper, 0.0, 4.0, "c");
    ASSERT_TRUE(clip.isValid());
    const auto note = f.doc.addNote(clip, makeNote(1.0, 64, 2.0, 90, 3));
    ASSERT_TRUE(note.isValid());
    f.selection.setSelection({clip});

    const auto anchor = clipCentre(f.lane, clip);
    const juce::Point<float> dropped(anchor.x + 80.0f, anchor.y + f.rowHeightF()); // one row down, +2 beats
    f.lane.mouseDown(toolClick(f.lane, anchor));
    f.lane.mouseDrag(toolDrag(f.lane, dropped, anchor));
    EXPECT_EQ(f.lane.getPreviewRowDeltaForTest(), 1);
    f.lane.mouseUp(toolDrag(f.lane, dropped, anchor));

    EXPECT_TRUE(f.doc.getTrack(upper)->clips.empty());
    ASSERT_EQ(f.doc.getTrack(lower)->clips.size(), 1u);
    const auto& moved = f.doc.getTrack(lower)->clips[0];
    EXPECT_EQ(moved.id, clip) << "a cross-track move keeps the clip's identity";
    EXPECT_DOUBLE_EQ(moved.startBeat, 2.0);
    EXPECT_DOUBLE_EQ(moved.lengthBeats, 4.0);
    EXPECT_EQ(moved.name, "c");
    ASSERT_EQ(moved.notes.size(), 1u);
    EXPECT_EQ(moved.notes[0].id, note);
    EXPECT_DOUBLE_EQ(moved.notes[0].startBeat, 1.0) << "notes are clip-relative, so they travel untouched";
    EXPECT_EQ(moved.notes[0].velocity, 90);

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.doc.getTrack(upper)->clips.size(), 1u);
}

TEST(TimelineClipToolTest, CrossTrackDragOntoAKindMismatchClampsToTheSameLane) {
    ToolLaneFixture f;
    const auto midi = f.doc.addTrack(TrackKind::Midi, "Midi");
    const auto audio = f.doc.addTrack(TrackKind::Audio, "Audio");
    const auto clip = f.doc.addClip(midi, 0.0, 4.0, "c"); // no assetRef -> a MIDI clip
    ASSERT_TRUE(clip.isValid());
    f.selection.setSelection({clip});

    const auto anchor = clipCentre(f.lane, clip);
    const juce::Point<float> dropped(anchor.x + 80.0f, anchor.y + f.rowHeightF()); // onto the Audio row
    f.lane.mouseDown(toolClick(f.lane, anchor));
    f.lane.mouseDrag(toolDrag(f.lane, dropped, anchor));
    EXPECT_EQ(f.lane.getPreviewRowDeltaForTest(), 0) << "an illegal drop clamps the row delta, it does not refuse";
    f.lane.mouseUp(toolDrag(f.lane, dropped, anchor));

    EXPECT_TRUE(f.doc.getTrack(audio)->clips.empty());
    ASSERT_EQ(f.doc.getTrack(midi)->clips.size(), 1u);
    EXPECT_DOUBLE_EQ(f.doc.getTrack(midi)->clips[0].startBeat, 2.0) << "the horizontal half of the drag still lands";
}

TEST(TimelineClipToolTest, AudioClipDragsOntoAnotherAudioTrack) {
    ToolLaneFixture f;
    const auto first = f.doc.addTrack(TrackKind::Audio, "A1");
    const auto second = f.doc.addTrack(TrackKind::Audio, "A2");
    const auto clip = f.doc.addClip(first, 0.0, 4.0, "take");
    ASSERT_TRUE(clip.isValid());
    ASSERT_TRUE(f.doc.setClipAsset(clip, "Audio/take-1.wav", 1.5));
    ASSERT_TRUE(f.doc.setClipGainDb(clip, -3.0));
    f.selection.setSelection({clip});

    const auto anchor = clipCentre(f.lane, clip);
    const juce::Point<float> dropped(anchor.x, anchor.y + f.rowHeightF());
    f.lane.mouseDown(toolClick(f.lane, anchor));
    f.lane.mouseDrag(toolDrag(f.lane, dropped, anchor));
    f.lane.mouseUp(toolDrag(f.lane, dropped, anchor));

    ASSERT_EQ(f.doc.getTrack(second)->clips.size(), 1u);
    const auto& moved = f.doc.getTrack(second)->clips[0];
    EXPECT_EQ(moved.assetRef, "Audio/take-1.wav");
    EXPECT_DOUBLE_EQ(moved.sourceStartSeconds, 1.5);
    EXPECT_DOUBLE_EQ(moved.gainDb, -3.0);
}

TEST(TimelineClipToolTest, MixedKindGroupDragClampsTheWholeGroupToItsOriginalTracks) {
    ToolLaneFixture f;
    const auto midiTrack = f.doc.addTrack(TrackKind::Midi, "Midi");
    const auto audioTrack = f.doc.addTrack(TrackKind::Audio, "Audio1");
    const auto audioTrack2 = f.doc.addTrack(TrackKind::Audio, "Audio2");
    const auto midiClip = f.doc.addClip(midiTrack, 0.0, 4.0, "m");
    const auto audioClip = f.doc.addClip(audioTrack, 0.0, 4.0, "a");
    ASSERT_TRUE(midiClip.isValid() && audioClip.isValid());
    ASSERT_TRUE(f.doc.setClipAsset(audioClip, "Audio/take.wav", 0.0));
    // Selected together: the MIDI clip's one-row-down destination (the Audio track) is a kind
    // mismatch, which clamps the row delta for the WHOLE group, even though the audio clip's own
    // one-row-down destination (the second Audio track) would have been legal on its own.
    f.selection.setSelection({midiClip, audioClip});

    const auto anchor = clipCentre(f.lane, midiClip);
    const juce::Point<float> dropped(anchor.x, anchor.y + f.rowHeightF()); // one row down, no horizontal move
    f.lane.mouseDown(toolClick(f.lane, anchor));
    f.lane.mouseDrag(toolDrag(f.lane, dropped, anchor));
    EXPECT_EQ(f.lane.getPreviewRowDeltaForTest(), 0) << "one mismatched clip in the group clamps them all";
    f.lane.mouseUp(toolDrag(f.lane, dropped, anchor));

    ASSERT_EQ(f.doc.getTrack(midiTrack)->clips.size(), 1u);
    EXPECT_DOUBLE_EQ(f.doc.getTrack(midiTrack)->clips[0].startBeat, 0.0);
    ASSERT_EQ(f.doc.getTrack(audioTrack)->clips.size(), 1u);
    EXPECT_DOUBLE_EQ(f.doc.getTrack(audioTrack)->clips[0].startBeat, 0.0);
    EXPECT_TRUE(f.doc.getTrack(audioTrack2)->clips.empty()) << "neither clip crossed tracks";
}

// -------------------------------------------------------------- Rename ------

TEST(TimelineClipToolTest, RenameCommitsAndABlankNameKeepsTheOldOne) {
    ToolLaneFixture f;
    const auto track = f.doc.addTrack(TrackKind::Midi, "T");
    const auto clip = f.doc.addClip(track, 0.0, 4.0, "Original");
    ASSERT_TRUE(clip.isValid());

    f.lane.renameClip(clip, "  Chorus  ");
    EXPECT_EQ(f.doc.getClip(clip)->name, "Chorus") << "setClipName trims";
    ASSERT_TRUE(f.undo.canUndo());

    f.lane.renameClip(clip, "   ");
    EXPECT_EQ(f.doc.getClip(clip)->name, "Chorus") << "a blank name is refused, not stored";

    f.undo.undo();
    EXPECT_EQ(f.doc.getClip(clip)->name, "Original") << "the refusal pushed no second undo step";
}

TEST(TimelineClipToolTest, RenameContextChoiceIsInert) {
    ToolLaneFixture f;
    const auto track = f.doc.addTrack(TrackKind::Midi, "T");
    const auto clip = f.doc.addClip(track, 0.0, 4.0, "Original");
    ASSERT_TRUE(clip.isValid());
    const auto revisionBefore = f.doc.getRevision();

    // The enum entry exists so the menu's vocabulary is enumerable; the editor it opens is the
    // real path, and renameClip() above is its commit half.
    f.lane.applyClipContextChoice(clip, TimelineClipLaneArea::ClipContextChoice::Rename, 0.0);

    EXPECT_EQ(f.doc.getRevision(), revisionBefore);
    EXPECT_EQ(f.doc.getClip(clip)->name, "Original");
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(TimelineClipToolTest, MuteAndGlueAreReachableFromTheContextMenuHook) {
    ToolLaneFixture f;
    const auto track = f.doc.addTrack(TrackKind::Midi, "T");
    const auto a = f.doc.addClip(track, 0.0, 4.0, "a");
    const auto b = f.doc.addClip(track, 4.0, 4.0, "b");
    ASSERT_TRUE(a.isValid() && b.isValid());

    f.lane.applyClipContextChoice(a, TimelineClipLaneArea::ClipContextChoice::ToggleMute, 0.0);
    EXPECT_TRUE(f.doc.getClip(a)->muted);

    f.lane.applyClipContextChoice(a, TimelineClipLaneArea::ClipContextChoice::GlueWithNext, 0.0);
    ASSERT_EQ(f.doc.getTrack(track)->clips.size(), 1u);
    EXPECT_DOUBLE_EQ(f.doc.getClip(a)->lengthBeats, 8.0);
    EXPECT_TRUE(f.doc.getClip(a)->muted) << "the survivor's mute flag survives with it";
}

// ---------------------------------------------- the shared time-grid colour policy --
//
// synth::ui::gridLineAlphaFor / gridLineColourFor / gridLevelIsReadable (TimelineClipLaneArea.h) are
// the ONE policy both grid painters read — TimelinePanelComponent for the lanes and
// PianoRollComponent for the roll. They are pure, so the two properties that actually matter (a
// monotonic hierarchy and the density guard) are asserted directly here rather than screenshotted.

TEST(TimelineGridHierarchyTest, AlphaHierarchyIsMonotonicAndBarIsStrongest) {
    using synth::ui::gridLineAlphaFor;
    using synth::ui::GridLineLevel;

    const float sub = gridLineAlphaFor(GridLineLevel::Subdivision);
    const float beat = gridLineAlphaFor(GridLineLevel::Beat);
    const float bar = gridLineAlphaFor(GridLineLevel::Bar);

    EXPECT_GT(beat, sub) << "a beat line must read as stronger than a snap subdivision";
    EXPECT_GT(bar, beat) << "and a bar line stronger than a beat";
    EXPECT_LE(bar, 1.0f);
}

// The regression this policy exists for: the sub-beat level used to be a 0.14 hairline, invisible on
// every dark theme. It is a HINT, not a whisper — pinned well clear of zero so a future tweak cannot
// quietly walk it back there.
TEST(TimelineGridHierarchyTest, SubdivisionAlphaIsAVisibleHintNotAHairline) {
    using synth::ui::gridLineAlphaFor;
    using synth::ui::GridLineLevel;

    EXPECT_GE(gridLineAlphaFor(GridLineLevel::Subdivision), 0.2f);
    EXPECT_LT(gridLineAlphaFor(GridLineLevel::Subdivision), gridLineAlphaFor(GridLineLevel::Bar));
}

// Density guard: a level whose lines would land closer than kMinGridLinePixels apart is dropped
// wholesale rather than drawn as a solid block that swamps the beat and bar lines above it.
TEST(TimelineGridHierarchyTest, DensityGuardDropsLevelsTighterThanTheMinimumSpacing) {
    using synth::ui::gridLevelIsReadable;
    using synth::ui::kMinGridLinePixels;

    // Exactly at the threshold is readable; a hair under it is not.
    EXPECT_TRUE(gridLevelIsReadable(1.0, kMinGridLinePixels));
    EXPECT_FALSE(gridLevelIsReadable(1.0, kMinGridLinePixels - 0.01));

    // A sixteenth grid: fine at 40 px/beat (10 px apart), gone at the minimum zoom (0.375 px apart).
    EXPECT_TRUE(gridLevelIsReadable(0.25, 40.0));
    EXPECT_FALSE(gridLevelIsReadable(0.25, synth::ui::TimelineViewState::kMinPixelsPerBeat));

    // Snap::Off reports a 0.0 division, and a garbage spacing must not sneak past either.
    EXPECT_FALSE(gridLevelIsReadable(0.0, 512.0));
    EXPECT_FALSE(gridLevelIsReadable(-1.0, 512.0));
    EXPECT_FALSE(gridLevelIsReadable(std::numeric_limits<double>::quiet_NaN(), 512.0));
    EXPECT_FALSE(gridLevelIsReadable(1.0, std::numeric_limits<double>::quiet_NaN()));
}

// Raising alpha alone cannot rescue a dark theme, where the `border` token is a shade off the
// background: the line is lifted TOWARDS the background's contrasting end first. Asserted in both
// directions, because a light theme has to get darker lines, not brighter ones.
TEST(TimelineGridHierarchyTest, GridLineColourLiftsAwayFromTheBackgroundOnDarkAndLightThemes) {
    using synth::ui::gridLineAlphaFor;
    using synth::ui::gridLineColourFor;
    using synth::ui::GridLineLevel;

    const juce::Colour darkBg(0xff101014);
    const juce::Colour darkBorder(0xff1e1e24); // a real dark-theme border: barely off the background

    const auto barOnDark = gridLineColourFor(GridLineLevel::Bar, darkBorder, darkBg);
    EXPECT_GT(barOnDark.getBrightness(), darkBorder.getBrightness())
        << "on a dark theme the line has to get brighter than the token it came from";
    // juce::Colour stores alpha as a uint8, so the round-trip is only accurate to 1/255.
    EXPECT_NEAR(barOnDark.getFloatAlpha(), gridLineAlphaFor(GridLineLevel::Bar), 1.0f / 255.0f);

    const juce::Colour lightBg(0xfff5f5f7);
    const juce::Colour lightBorder(0xffe2e2e6);
    const auto barOnLight = gridLineColourFor(GridLineLevel::Bar, lightBorder, lightBg);
    EXPECT_LT(barOnLight.getBrightness(), lightBorder.getBrightness())
        << "and darker on a light one — contrast, not brightness";

    // The hierarchy survives the colour derivation: same base, same background, monotonic alpha.
    const auto subOnDark = gridLineColourFor(GridLineLevel::Subdivision, darkBorder, darkBg);
    const auto beatOnDark = gridLineColourFor(GridLineLevel::Beat, darkBorder, darkBg);
    EXPECT_LT(subOnDark.getFloatAlpha(), beatOnDark.getFloatAlpha());
    EXPECT_LT(beatOnDark.getFloatAlpha(), barOnDark.getFloatAlpha());
    EXPECT_EQ(subOnDark.withAlpha(1.0f), barOnDark.withAlpha(1.0f)) << "one colour, three opacities";
}
