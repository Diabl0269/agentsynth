// TimelineDoc note tests: sort order, validation rejects, and clearing an already-empty clip.

#include "TimelineDocTestHelpers.h"

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
