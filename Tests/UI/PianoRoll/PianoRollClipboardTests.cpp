// PianoRoll note-clipboard tests: copy/cut/paste-at-playhead/duplicate/repeat/select-all,
// including the cross-clip paste the member clipboard exists for.
// Shared PianoRollFixture and helpers live in PianoRollTestHelpers.h.

#include "PianoRollTestHelpers.h"

// ============================================================================
// 5. Note clipboard
// ============================================================================

TEST(PianoRollClipboardTest, CopyThenPasteAtThePlayheadInsideTheClip) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);

    const auto idA = f.doc.addNote(clipId, makeNote(1.0, 60, 1.0));
    const auto idB = f.doc.addNote(clipId, makeNote(2.0, 64, 1.0));
    ASSERT_TRUE(idA.isValid());
    ASSERT_TRUE(idB.isValid());

    EXPECT_FALSE(f.roll.canPasteNotes()) << "nothing copied yet";
    EXPECT_FALSE(f.roll.copySelectedNotes()) << "and nothing selected to copy";

    f.roll.getSelectionForTest().setSelection({idA, idB});
    EXPECT_TRUE(f.roll.hasNoteSelection());
    EXPECT_TRUE(f.roll.copySelectedNotes());
    EXPECT_EQ(f.roll.getClipboardSizeForTest(), 2);
    EXPECT_TRUE(f.roll.canPasteNotes());
    EXPECT_FALSE(f.undo.canUndo()) << "copying is not a document edit";

    f.roll.setPlayheadBeat(4.0);
    EXPECT_TRUE(f.roll.pasteNotesAtPlayhead());

    const auto* clip = f.doc.getClip(clipId);
    ASSERT_EQ(clip->notes.size(), 4u);
    EXPECT_DOUBLE_EQ(clip->notes[2].startBeat, 4.0) << "the earliest copied note lands ON the playhead";
    EXPECT_EQ(clip->notes[2].pitch, 60);
    EXPECT_DOUBLE_EQ(clip->notes[3].startBeat, 5.0) << "and the block keeps its internal shape";
    EXPECT_EQ(clip->notes[3].pitch, 64);

    const auto selected = f.roll.getSelectionForTest().getSelected();
    ASSERT_EQ(selected.size(), 2u);
    EXPECT_EQ(selected[0], clip->notes[2].id);
    EXPECT_EQ(selected[1], clip->notes[3].id) << "the pasted block becomes the selection";

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.doc.getClip(clipId)->notes.size(), 2u) << "both pasted notes came back in ONE undo";
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(PianoRollClipboardTest, PasteWithThePlayheadOutsideTheClipAnchorsAtZero) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 4.0, 4.0, "Clip"); // absolute [4, 8)
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);

    const auto idA = f.doc.addNote(clipId, makeNote(1.0, 60, 1.0));
    ASSERT_TRUE(idA.isValid());
    f.roll.getSelectionForTest().setSelection({idA});
    ASSERT_TRUE(f.roll.copySelectedNotes());

    f.roll.setPlayheadBeat(0.0); // well before the clip starts
    EXPECT_TRUE(f.roll.pasteNotesAtPlayhead());

    const auto* clip = f.doc.getClip(clipId);
    ASSERT_EQ(clip->notes.size(), 2u);
    EXPECT_DOUBLE_EQ(clip->notes[0].startBeat, 0.0) << "an out-of-clip playhead anchors the block at the clip start";
}

TEST(PianoRollClipboardTest, CopiedNotesPasteIntoADifferentClip) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipA = f.doc.addClip(trackId, 0.0, 8.0, "A");
    const auto clipB = f.doc.addClip(trackId, 8.0, 8.0, "B");
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipA);

    const auto id = f.doc.addNote(clipA, makeNote(1.0, 60, 1.0));
    ASSERT_TRUE(id.isValid());
    f.roll.getSelectionForTest().setSelection({id});
    ASSERT_TRUE(f.roll.copySelectedNotes());

    // Switching clips clears the SELECTION but never the clipboard — that is the whole reason the
    // clipboard is a member of the roll.
    f.open(clipB);
    EXPECT_TRUE(f.roll.getSelectionForTest().isEmpty());
    EXPECT_TRUE(f.roll.canPasteNotes());

    f.roll.setPlayheadBeat(10.0); // absolute -> clip-relative beat 2 inside B
    EXPECT_TRUE(f.roll.pasteNotesAtPlayhead());

    ASSERT_EQ(f.doc.getClip(clipB)->notes.size(), 1u);
    EXPECT_DOUBLE_EQ(f.doc.getClip(clipB)->notes[0].startBeat, 2.0);
    EXPECT_EQ(f.doc.getClip(clipB)->notes[0].pitch, 60);
    EXPECT_EQ(f.doc.getClip(clipA)->notes.size(), 1u) << "the source clip is untouched";
}

TEST(PianoRollClipboardTest, PasteClampsLengthAtTheClipEndAndSkipsNotesPastIt) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 4.0, "Clip"); // [0, 4)
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);

    const auto idA = f.doc.addNote(clipId, makeNote(0.0, 60, 2.0)); // offset 0, two beats long
    const auto idB = f.doc.addNote(clipId, makeNote(3.0, 64, 1.0)); // offset 3
    ASSERT_TRUE(idA.isValid());
    ASSERT_TRUE(idB.isValid());
    f.roll.getSelectionForTest().setSelection({idA, idB});
    ASSERT_TRUE(f.roll.copySelectedNotes());

    f.roll.setPlayheadBeat(3.0); // only one beat of room left
    EXPECT_TRUE(f.roll.pasteNotesAtPlayhead());

    const auto* clip = f.doc.getClip(clipId);
    ASSERT_EQ(clip->notes.size(), 3u) << "the +3 offset lands past the clip's end and is skipped";
    const auto pasted = f.roll.getSelectionForTest().getSelected();
    ASSERT_EQ(pasted.size(), 1u);
    const auto* note = f.doc.getNote(pasted[0]);
    ASSERT_NE(note, nullptr);
    EXPECT_DOUBLE_EQ(note->startBeat, 3.0);
    EXPECT_DOUBLE_EQ(note->lengthBeats, 1.0) << "clamped to the clip's end rather than overrunning it";
}

TEST(PianoRollClipboardTest, DuplicatePlacesCopiesAfterTheSelectionSpan) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);

    const auto idA = f.doc.addNote(clipId, makeNote(1.0, 60, 1.0)); // [1, 2)
    const auto idB = f.doc.addNote(clipId, makeNote(2.0, 64, 2.0)); // [2, 4)  -> span 3
    ASSERT_TRUE(idA.isValid());
    ASSERT_TRUE(idB.isValid());
    f.roll.getSelectionForTest().setSelection({idA, idB});

    EXPECT_TRUE(f.roll.duplicateSelectedNotes());

    const auto* clip = f.doc.getClip(clipId);
    ASSERT_EQ(clip->notes.size(), 4u);
    EXPECT_DOUBLE_EQ(clip->notes[2].startBeat, 4.0) << "immediately after the span's end";
    EXPECT_EQ(clip->notes[2].pitch, 60);
    EXPECT_DOUBLE_EQ(clip->notes[3].startBeat, 5.0);
    EXPECT_EQ(clip->notes[3].pitch, 64);
    EXPECT_DOUBLE_EQ(clip->notes[3].lengthBeats, 2.0);
    EXPECT_EQ(f.roll.getSelectionForTest().size(), 2) << "the copies become the selection";
    EXPECT_EQ(f.roll.getClipboardSizeForTest(), 0) << "duplicating never stomps the clipboard";

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.doc.getClip(clipId)->notes.size(), 2u) << "ONE undo step";
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(PianoRollClipboardTest, CutFillsTheClipboardAndDeletesInOneStep) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);

    const auto idA = f.doc.addNote(clipId, makeNote(1.0, 60, 1.0));
    const auto idB = f.doc.addNote(clipId, makeNote(2.0, 64, 1.0));
    ASSERT_TRUE(idA.isValid());
    ASSERT_TRUE(idB.isValid());
    f.roll.getSelectionForTest().setSelection({idA, idB});

    EXPECT_TRUE(f.roll.cutSelectedNotes());
    EXPECT_TRUE(f.doc.getClip(clipId)->notes.empty());
    EXPECT_TRUE(f.roll.getSelectionForTest().isEmpty());
    EXPECT_EQ(f.roll.getClipboardSizeForTest(), 2);
    EXPECT_TRUE(f.roll.canPasteNotes()) << "a cut is always pasteable";

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.doc.getClip(clipId)->notes.size(), 2u) << "the whole cut was ONE undo step";
    EXPECT_FALSE(f.undo.canUndo());

    // And what it captured really does paste back.
    f.doc.clearNotes(clipId);
    f.roll.setPlayheadBeat(8.0);
    EXPECT_TRUE(f.roll.pasteNotesAtPlayhead());
    ASSERT_EQ(f.doc.getClip(clipId)->notes.size(), 2u);
    EXPECT_DOUBLE_EQ(f.doc.getClip(clipId)->notes[0].startBeat, 8.0);
    EXPECT_DOUBLE_EQ(f.doc.getClip(clipId)->notes[1].startBeat, 9.0);
}

TEST(PianoRollClipboardTest, RepeatPlacesBackToBackBlocksAndStopsAtTheClipEnd) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);

    const auto id = f.doc.addNote(clipId, makeNote(0.0, 60, 1.0)); // span 1 beat
    ASSERT_TRUE(id.isValid());
    f.roll.getSelectionForTest().setSelection({id});

    EXPECT_FALSE(f.roll.repeatSelectedNotes(0)) << "a zero repeat does nothing";
    EXPECT_TRUE(f.roll.repeatSelectedNotes(2));

    const auto* clip = f.doc.getClip(clipId);
    ASSERT_EQ(clip->notes.size(), 3u);
    EXPECT_DOUBLE_EQ(clip->notes[1].startBeat, 1.0);
    EXPECT_DOUBLE_EQ(clip->notes[2].startBeat, 2.0) << "back to back, one span apart";
    EXPECT_EQ(f.roll.getSelectionForTest().size(), 2) << "both copies end up selected";

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.doc.getClip(clipId)->notes.size(), 1u) << "the whole repeat was ONE undo step";
    EXPECT_FALSE(f.undo.canUndo());

    // Asking for more copies than fit stops at the boundary instead of piling them on the last beat.
    f.roll.getSelectionForTest().setSelection({id});
    EXPECT_TRUE(f.roll.repeatSelectedNotes(20));
    const auto* filled = f.doc.getClip(clipId);
    EXPECT_EQ(filled->notes.size(), 8u) << "beats 0..7 of an 8-beat clip, and no more";
    EXPECT_DOUBLE_EQ(filled->notes.back().startBeat, 7.0);
}

TEST(PianoRollClipboardTest, SelectAllSelectsEveryNoteInTheOpenClip) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);

    EXPECT_FALSE(f.roll.selectAllNotes()) << "an empty clip has nothing to select";
    EXPECT_FALSE(f.roll.hasNoteSelection());

    ASSERT_TRUE(f.doc.addNote(clipId, makeNote(0.0, 60, 1.0)).isValid());
    ASSERT_TRUE(f.doc.addNote(clipId, makeNote(1.0, 62, 1.0)).isValid());
    ASSERT_TRUE(f.doc.addNote(clipId, makeNote(2.0, 64, 1.0)).isValid());

    EXPECT_TRUE(f.roll.selectAllNotes());
    EXPECT_EQ(f.roll.getSelectionForTest().size(), 3);
    EXPECT_FALSE(f.undo.canUndo()) << "selecting is not a document edit";
}
