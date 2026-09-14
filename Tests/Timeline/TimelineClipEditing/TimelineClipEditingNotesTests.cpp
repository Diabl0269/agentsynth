// TimelineClipEditing note tests: id monotonicity, remove/move/resize/velocity, and lookup helpers.

#include "TimelineClipEditingTestHelpers.h"

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
