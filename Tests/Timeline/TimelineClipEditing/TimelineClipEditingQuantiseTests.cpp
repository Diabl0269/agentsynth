// TimelineClipEditing quantise tests: full/half strength, no-op when nothing moves, invalid grid rejection.

#include "TimelineClipEditingTestHelpers.h"

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
