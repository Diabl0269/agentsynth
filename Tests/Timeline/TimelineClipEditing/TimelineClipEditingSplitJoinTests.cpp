// TimelineClipEditing split/join/duplicate tests: straddling-note split, out-of-range rejection, join
// rebasing/merge/rejects, and duplicate.

#include "TimelineClipEditingTestHelpers.h"

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
