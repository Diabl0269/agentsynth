// PianoRoll edit-tools tests: the Split/Glue/Erase/Mute/Draw tools' single-click gesture, their
// no-op cases (which must leave the undo stack alone), the fact that none of them can start a
// Select-tool drag, and the QUANTIZE verb (its header chip and both Option+Q shortcuts).
// Shared PianoRollFixture and mouse-gesture helpers live in PianoRollTestHelpers.h.

#include "PianoRollTestHelpers.h"

#include "UI/Timeline/EditTool.h"

// ============================================================================
// 4. Edit tools
// ============================================================================

TEST(PianoRollToolTest, DefaultsToSelectAndTakesTheToolFromItsOwner) {
    PianoRollFixture f;
    EXPECT_EQ(f.roll.getActiveTool(), EditTool::Select);
    f.roll.setActiveTool(EditTool::Erase);
    EXPECT_EQ(f.roll.getActiveTool(), EditTool::Erase);
    f.roll.setActiveTool(EditTool::Select);
    EXPECT_EQ(f.roll.getActiveTool(), EditTool::Select);
}

// The tool DIGITS belong to the panel: the roll must leave them unconsumed or the two would fight
// over which tool is active (see setActiveTool).
TEST(PianoRollToolTest, DigitKeysAreLeftForThePanel) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);
    ASSERT_TRUE(f.doc.addNote(clipId, makeNote(1.0, 60)).isValid());

    EXPECT_FALSE(f.roll.keyPressed(juce::KeyPress('1')));
    EXPECT_FALSE(f.roll.keyPressed(juce::KeyPress('3')));
    EXPECT_FALSE(f.roll.keyPressed(juce::KeyPress('8')));
    EXPECT_EQ(f.roll.getActiveTool(), EditTool::Select) << "the roll never switches tool by itself";
}

// ---- Split ----

TEST(PianoRollToolTest, SplitCutsANoteInTwoAndTheRightHalfInheritsEveryField) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);

    auto n = makeNote(1.0, 60, 2.0); // [1, 3)
    n.velocity = 77;
    n.channel = 3;
    const auto id = f.doc.addNote(clipId, n);
    ASSERT_TRUE(id.isValid());
    ASSERT_TRUE(f.doc.setNoteMuted(id, true));
    ASSERT_FALSE(f.undo.canUndo()) << "doc setup happened outside the undo manager";

    f.roll.setActiveTool(EditTool::Split);
    f.roll.mouseDown(leftClick(f.roll, pointAt(f.roll, 2.0, 60)));

    const auto* clip = f.doc.getClip(clipId);
    ASSERT_EQ(clip->notes.size(), 2u);
    const auto& left = clip->notes[0]; // notes stay sorted by startBeat
    const auto& right = clip->notes[1];

    EXPECT_EQ(left.id, id) << "the left half keeps the original note's identity";
    EXPECT_DOUBLE_EQ(left.startBeat, 1.0);
    EXPECT_DOUBLE_EQ(left.lengthBeats, 1.0);
    EXPECT_DOUBLE_EQ(right.startBeat, 2.0);
    EXPECT_DOUBLE_EQ(right.lengthBeats, 1.0);
    EXPECT_EQ(right.pitch, 60);
    EXPECT_EQ(right.velocity, 77);
    EXPECT_EQ(right.channel, 3);
    EXPECT_TRUE(right.muted) << "a split divides a note — the right half inherits mute too";

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    ASSERT_EQ(f.doc.getClip(clipId)->notes.size(), 1u);
    EXPECT_DOUBLE_EQ(f.doc.getNote(id)->lengthBeats, 2.0);
    EXPECT_FALSE(f.undo.canUndo()) << "resize + add were ONE undo step";
}

TEST(PianoRollToolTest, SplitTooCloseToAnEdgeIsANoOpWithNoUndoStep) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);

    const auto id = f.doc.addNote(clipId, makeNote(1.0, 60, 1.0)); // [1, 2): every snapped cut is an edge
    ASSERT_TRUE(id.isValid());
    f.roll.setActiveTool(EditTool::Split);

    f.roll.mouseDown(leftClick(f.roll, pointAt(f.roll, 1.2, 60))); // snaps back to the start
    f.roll.mouseDown(leftClick(f.roll, pointAt(f.roll, 1.8, 60))); // snaps up to the end

    EXPECT_EQ(f.doc.getClip(clipId)->notes.size(), 1u) << "neither cut leaves room on both sides";
    EXPECT_DOUBLE_EQ(f.doc.getNote(id)->lengthBeats, 1.0);
    EXPECT_FALSE(f.undo.canUndo()) << "a no-op gesture writes no undo step";
}

// The hover preview follows the SNAPPED cut and repaints only when that cut actually moves — and
// it repaints its own strip, never the playhead's.
TEST(PianoRollToolTest, SplitHoverPreviewRepaintsOnlyWhenTheCutMoves) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);
    ASSERT_TRUE(f.doc.addNote(clipId, makeNote(1.0, 60, 4.0)).isValid()); // [1, 5)

    f.roll.setActiveTool(EditTool::Split);
    EXPECT_EQ(f.roll.previewRequests, 0);

    f.roll.mouseMove(hover(f.roll, pointAt(f.roll, 2.0, 60)));
    EXPECT_TRUE(f.roll.hasSplitPreviewForTest());
    EXPECT_DOUBLE_EQ(f.roll.getSplitPreviewBeatForTest(), 2.0);
    EXPECT_EQ(f.roll.previewRequests, 1);

    f.roll.mouseMove(hover(f.roll, pointAt(f.roll, 2.2, 60)));
    EXPECT_EQ(f.roll.previewRequests, 1) << "same snapped cut — a moving pointer costs nothing";

    f.roll.mouseMove(hover(f.roll, pointAt(f.roll, 3.0, 60)));
    EXPECT_DOUBLE_EQ(f.roll.getSplitPreviewBeatForTest(), 3.0);
    EXPECT_EQ(f.roll.previewRequests, 2);

    f.roll.mouseMove(hover(f.roll, pointAt(f.roll, 6.0, 60))); // off the note entirely
    EXPECT_FALSE(f.roll.hasSplitPreviewForTest());
    EXPECT_EQ(f.roll.previewRequests, 3);

    EXPECT_EQ(f.roll.requests, 0) << "hovering must never repaint the playhead's strip";
}

// ---- Glue ----

TEST(PianoRollToolTest, GlueAbsorbsTheNextSamePitchNoteAndBridgesTheGap) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);

    const auto first = f.doc.addNote(clipId, makeNote(0.0, 60, 1.0));  // [0, 1)
    const auto second = f.doc.addNote(clipId, makeNote(2.0, 60, 1.0)); // [2, 3), a 1-beat GAP away
    ASSERT_TRUE(first.isValid());
    ASSERT_TRUE(second.isValid());
    f.roll.getSelectionForTest().setSelection({first, second});

    f.roll.setActiveTool(EditTool::Glue);
    f.roll.mouseDown(leftClick(f.roll, pointAt(f.roll, 0.5, 60)));

    ASSERT_EQ(f.doc.getClip(clipId)->notes.size(), 1u);
    ASSERT_NE(f.doc.getNote(first), nullptr);
    EXPECT_DOUBLE_EQ(f.doc.getNote(first)->startBeat, 0.0);
    EXPECT_DOUBLE_EQ(f.doc.getNote(first)->lengthBeats, 3.0) << "the gap is bridged, Cubase-style";
    EXPECT_EQ(f.doc.getNote(second), nullptr);
    EXPECT_FALSE(f.roll.getSelectionForTest().contains(second)) << "the absorbed note leaves the selection";

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.doc.getClip(clipId)->notes.size(), 2u);
    EXPECT_DOUBLE_EQ(f.doc.getNote(first)->lengthBeats, 1.0);
    EXPECT_FALSE(f.undo.canUndo()) << "resize + remove were ONE undo step";
}

TEST(PianoRollToolTest, GlueIgnoresANeighbourAtADifferentPitch) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);

    const auto first = f.doc.addNote(clipId, makeNote(0.0, 60, 1.0));
    const auto other = f.doc.addNote(clipId, makeNote(2.0, 64, 1.0)); // a different VOICE
    ASSERT_TRUE(first.isValid());
    ASSERT_TRUE(other.isValid());

    f.roll.setActiveTool(EditTool::Glue);
    f.roll.mouseDown(leftClick(f.roll, pointAt(f.roll, 0.5, 60)));

    EXPECT_EQ(f.doc.getClip(clipId)->notes.size(), 2u);
    EXPECT_DOUBLE_EQ(f.doc.getNote(first)->lengthBeats, 1.0);
    EXPECT_NE(f.doc.getNote(other), nullptr);
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(PianoRollToolTest, GlueWithNothingToAbsorbWritesNoUndoStep) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);

    const auto only = f.doc.addNote(clipId, makeNote(1.0, 60, 1.0));
    ASSERT_TRUE(only.isValid());

    f.roll.setActiveTool(EditTool::Glue);
    f.roll.mouseDown(leftClick(f.roll, pointAt(f.roll, 1.5, 60)));

    EXPECT_DOUBLE_EQ(f.doc.getNote(only)->lengthBeats, 1.0);
    EXPECT_FALSE(f.undo.canUndo());
}

// ---- Erase ----

TEST(PianoRollToolTest, EraseClickDeletesTheNoteInOneStep) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);

    const auto idA = f.doc.addNote(clipId, makeNote(1.0, 60, 1.0));
    const auto idB = f.doc.addNote(clipId, makeNote(3.0, 64, 1.0));
    ASSERT_TRUE(idA.isValid());
    ASSERT_TRUE(idB.isValid());
    f.roll.getSelectionForTest().setSelection({idA});

    f.roll.setActiveTool(EditTool::Erase);
    f.roll.mouseDown(leftClick(f.roll, pointAt(f.roll, 1.5, 60)));

    EXPECT_EQ(f.doc.getNote(idA), nullptr);
    EXPECT_NE(f.doc.getNote(idB), nullptr) << "only the clicked note goes";
    EXPECT_FALSE(f.roll.getSelectionForTest().contains(idA));

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.doc.getClip(clipId)->notes.size(), 2u);
    EXPECT_FALSE(f.undo.canUndo());

    // A click on empty grid erases nothing at all.
    f.roll.mouseDown(leftClick(f.roll, pointAt(f.roll, 6.0, 60)));
    EXPECT_EQ(f.doc.getClip(clipId)->notes.size(), 2u);
    EXPECT_FALSE(f.undo.canUndo());
}

// ---- Mute ----

TEST(PianoRollToolTest, MuteClickTogglesBothWays) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);

    const auto id = f.doc.addNote(clipId, makeNote(1.0, 60, 1.0));
    ASSERT_TRUE(id.isValid());
    ASSERT_FALSE(f.doc.getNote(id)->muted);

    f.roll.setActiveTool(EditTool::Mute);
    f.roll.mouseDown(leftClick(f.roll, pointAt(f.roll, 1.5, 60)));
    EXPECT_TRUE(f.doc.getNote(id)->muted);
    EXPECT_DOUBLE_EQ(f.doc.getNote(id)->startBeat, 1.0) << "muting never moves a note";

    f.roll.mouseDown(leftClick(f.roll, pointAt(f.roll, 1.5, 60)));
    EXPECT_FALSE(f.doc.getNote(id)->muted) << "the same click un-mutes";

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_TRUE(f.doc.getNote(id)->muted) << "each toggle is its own undo step";
    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_FALSE(f.doc.getNote(id)->muted);
    EXPECT_FALSE(f.undo.canUndo());
}

// ---- Draw ----

TEST(PianoRollToolTest, DrawDragCreatesANoteOfTheDraggedLength) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);

    const int pitch = f.roll.getFirstVisiblePitchForTest() - 2;
    const auto anchor = pointAt(f.roll, 1.6, pitch); // inside the [1, 2) grid CELL
    const auto dragged = pointAt(f.roll, 3.0, pitch);

    f.roll.setActiveTool(EditTool::Draw);
    f.roll.mouseDown(leftClick(f.roll, anchor));
    EXPECT_DOUBLE_EQ(f.roll.getDrawPreviewLengthForTest(), 1.0) << "the press arms one division";
    f.roll.mouseDrag(leftDrag(f.roll, dragged, anchor));
    EXPECT_DOUBLE_EQ(f.roll.getDrawPreviewLengthForTest(), 2.0);
    EXPECT_TRUE(f.doc.getClip(clipId)->notes.empty()) << "nothing is committed until the release";
    f.roll.mouseUp(leftDrag(f.roll, dragged, anchor));

    const auto* clip = f.doc.getClip(clipId);
    ASSERT_EQ(clip->notes.size(), 1u);
    EXPECT_DOUBLE_EQ(clip->notes[0].startBeat, 1.0) << "the pencil FLOORS to the cell it was pressed in";
    EXPECT_DOUBLE_EQ(clip->notes[0].lengthBeats, 2.0);
    EXPECT_EQ(clip->notes[0].pitch, pitch);
    EXPECT_EQ(clip->notes[0].velocity, 100);
    EXPECT_TRUE(f.roll.getSelectionForTest().contains(clip->notes[0].id));

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_TRUE(f.doc.getClip(clipId)->notes.empty()) << "the draw was ONE undo step";
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(PianoRollToolTest, DrawPlainClickCreatesAOneDivisionNote) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);

    const int pitch = f.roll.getFirstVisiblePitchForTest() - 2;
    const auto pos = pointAt(f.roll, 1.6, pitch);
    f.roll.setActiveTool(EditTool::Draw);
    f.roll.mouseDown(leftClick(f.roll, pos));
    f.roll.mouseUp(leftClick(f.roll, pos));

    ASSERT_EQ(f.doc.getClip(clipId)->notes.size(), 1u);
    EXPECT_DOUBLE_EQ(f.doc.getClip(clipId)->notes[0].startBeat, 1.0);
    EXPECT_DOUBLE_EQ(f.doc.getClip(clipId)->notes[0].lengthBeats, 1.0);

    // The division still decides the length: a sixteenth grid draws a sixteenth.
    setSnap(f, TimelineViewState::Snap::Sixteenth);
    const auto pos2 = pointAt(f.roll, 8.3, pitch - 3);
    f.roll.mouseDown(leftClick(f.roll, pos2));
    f.roll.mouseUp(leftClick(f.roll, pos2));
    ASSERT_EQ(f.doc.getClip(clipId)->notes.size(), 2u);
    EXPECT_DOUBLE_EQ(f.doc.getClip(clipId)->notes[1].startBeat, 8.25);
    EXPECT_DOUBLE_EQ(f.doc.getClip(clipId)->notes[1].lengthBeats, 0.25);
}

TEST(PianoRollToolTest, DrawNeverRedrawsOverAnExistingNote) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);
    const auto id = f.doc.addNote(clipId, makeNote(1.0, 60, 1.0));
    ASSERT_TRUE(id.isValid());

    f.roll.setActiveTool(EditTool::Draw);
    const auto pos = pointAt(f.roll, 1.5, 60);
    f.roll.mouseDown(leftClick(f.roll, pos));
    f.roll.mouseUp(leftClick(f.roll, pos));

    EXPECT_EQ(f.doc.getClip(clipId)->notes.size(), 1u);
    EXPECT_FALSE(f.undo.canUndo());
}

// ---- The tools disable the Select-tool drags entirely ----

TEST(PianoRollToolTest, NonSelectToolsNeverMoveResizeOrMarquee) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);

    const auto id = f.doc.addNote(clipId, makeNote(2.0, 60, 1.0));
    ASSERT_TRUE(id.isValid());
    f.roll.getSelectionForTest().setSelection({id});

    // Mute tool: press on the note, drag a long way, release. The note toggles and stays put.
    f.roll.setActiveTool(EditTool::Mute);
    const auto anchor = pointAt(f.roll, 2.5, 60);
    const juce::Point<float> dragged(anchor.x + 160.0f, anchor.y - 40.0f);
    f.roll.mouseDown(leftClick(f.roll, anchor));
    f.roll.mouseDrag(leftDrag(f.roll, dragged, anchor));
    f.roll.mouseUp(leftDrag(f.roll, dragged, anchor));

    const auto* note = f.doc.getNote(id);
    ASSERT_NE(note, nullptr);
    EXPECT_DOUBLE_EQ(note->startBeat, 2.0) << "no move";
    EXPECT_EQ(note->pitch, 60) << "no transpose";
    EXPECT_DOUBLE_EQ(note->lengthBeats, 1.0) << "no resize";
    EXPECT_TRUE(note->muted);

    // No drag on empty grid arms a marquee under a non-Select tool — PLAIN included, which is the
    // one that matters now that plain drag is the Select tool's marquee gesture (section 7). Shift
    // is checked alongside it because it used to be the only combination that could arm one at all.
    const auto emptyAnchor = pointAt(f.roll, 9.0, 55);
    const juce::Point<float> emptyTo(emptyAnchor.x + 120.0f, emptyAnchor.y + 20.0f);
    for (const int modifier : {0, (int)juce::ModifierKeys::shiftModifier}) {
        f.roll.mouseDown(leftClick(f.roll, emptyAnchor, modifier));
        EXPECT_FALSE(f.roll.isMarqueeActiveForTest()) << "modifier " << modifier;
        f.roll.mouseDrag(leftDrag(f.roll, emptyTo, emptyAnchor, modifier));
        EXPECT_FALSE(f.roll.isMarqueeActiveForTest()) << "modifier " << modifier << " after dragging";
        f.roll.mouseUp(leftDrag(f.roll, emptyTo, emptyAnchor, modifier));
    }
    EXPECT_EQ(f.doc.getClip(clipId)->notes.size(), 1u) << "and the Mute tool drew nothing either";
}

// Switching back to Select restores the whole original gesture table, double-click included.
TEST(PianoRollToolTest, SelectToolKeepsItsDoubleClickCreateAndDelete) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);

    const int pitch = f.roll.getFirstVisiblePitchForTest() - 2;
    const auto pos = pointAt(f.roll, 2.1, pitch);

    // Under Erase, a double-click must not create anything.
    f.roll.setActiveTool(EditTool::Erase);
    f.roll.mouseDoubleClick(leftClick(f.roll, pos));
    EXPECT_TRUE(f.doc.getClip(clipId)->notes.empty());

    f.roll.setActiveTool(EditTool::Select);
    f.roll.mouseDoubleClick(leftClick(f.roll, pos));
    ASSERT_EQ(f.doc.getClip(clipId)->notes.size(), 1u);
    const auto id = f.doc.getClip(clipId)->notes[0].id;
    EXPECT_DOUBLE_EQ(f.doc.getNote(id)->startBeat, 2.0) << "the double-click still snaps to the NEAREST division";

    f.roll.mouseDoubleClick(leftClick(f.roll, centreOf(f.roll.getNoteRect(id))));
    EXPECT_TRUE(f.doc.getClip(clipId)->notes.empty());

    // And the Select drag still moves.
    const auto moved = f.doc.addNote(clipId, makeNote(2.0, 60, 1.0));
    ASSERT_TRUE(moved.isValid());
    const auto rect = f.roll.getNoteRect(moved);
    const juce::Point<float> dragAnchor = centreOf(rect);
    const juce::Point<float> dragTo(dragAnchor.x + 40.0f, dragAnchor.y);
    f.roll.mouseDown(leftClick(f.roll, dragAnchor));
    f.roll.mouseDrag(leftDrag(f.roll, dragTo, dragAnchor));
    f.roll.mouseUp(leftDrag(f.roll, dragTo, dragAnchor));
    EXPECT_DOUBLE_EQ(f.doc.getNote(moved)->startBeat, 3.0);
}

// ============================================================================
// 22. QUANTIZE: the pitch-quantize verb, its header chip and both Option+Q shortcuts.
// ============================================================================

TEST(PianoRollPitchQuantiseTest, HeaderChipQuantisesPitchesAndIsANoOpWithoutAScale) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);

    const auto id = f.doc.addNote(clipId, makeNote(1.0, 61, 1.0)); // C#4: out of C Major
    ASSERT_TRUE(id.isValid());
    ASSERT_FALSE(f.roll.getQuantisePitchButtonBounds().isEmpty()) << "the chip has a real rect";
    EXPECT_FALSE(f.roll.isPitchQuantiseEnabled()) << "no scale chosen -> nothing to quantise INTO";

    // Clicking it with no scale is silent and writes nothing.
    f.roll.mouseDown(leftClick(f.roll, centreOf(f.roll.getQuantisePitchButtonBounds())));
    EXPECT_EQ(f.doc.getNote(id)->pitch, 61);
    EXPECT_FALSE(f.undo.canUndo());

    chooseMajorScaleForOpenClip(f);
    EXPECT_TRUE(f.roll.isPitchQuantiseEnabled());

    f.roll.mouseDown(leftClick(f.roll, centreOf(f.roll.getQuantisePitchButtonBounds())));
    EXPECT_EQ(f.doc.getNote(id)->pitch, 60) << "C# snapped to the nearest in-scale pitch (ties resolve DOWN)";
    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.doc.getNote(id)->pitch, 61);
}

// The chip is its own hit-test region — clicking it must not reach the "Q" (start-quantise) chip
// next door, and vice versa.
TEST(PianoRollPitchQuantiseTest, TheTwoQuantiseChipsDoNotOverlapAndHitIndependently) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);
    chooseMajorScaleForOpenClip(f);

    const auto startChip = f.roll.getQuantiseButtonBounds();
    const auto pitchChip = f.roll.getQuantisePitchButtonBounds();
    ASSERT_FALSE(startChip.isEmpty());
    ASSERT_FALSE(pitchChip.isEmpty());
    EXPECT_FALSE(startChip.intersects(pitchChip)) << "two chips a pixel apart would make one unclickable";
    EXPECT_GT(pitchChip.getX(), startChip.getX()) << "pitch quantize sits to the RIGHT of start quantize";

    // A note that is BOTH off-grid and out of scale: each chip must move exactly one of the two.
    const auto id = f.doc.addNote(clipId, makeNote(1.1, 61, 1.0));
    f.roll.mouseDown(leftClick(f.roll, centreOf(pitchChip)));
    EXPECT_EQ(f.doc.getNote(id)->pitch, 60) << "the pitch chip moved the pitch";
    EXPECT_DOUBLE_EQ(f.doc.getNote(id)->startBeat, 1.1) << "and left the start alone";

    f.roll.mouseDown(leftClick(f.roll, centreOf(startChip)));
    EXPECT_DOUBLE_EQ(f.doc.getNote(id)->startBeat, 1.0) << "the Q chip moved the start";
    EXPECT_EQ(f.doc.getNote(id)->pitch, 60);
}

TEST(PianoRollPitchQuantiseTest, ChipHoverIsItsOwnGatedRepaintRegion) {
    PianoRollFixture f;
    ASSERT_EQ(f.roll.headerButtonRequests, 0);

    f.roll.mouseMove(hover(f.roll, centreOf(f.roll.getQuantisePitchButtonBounds())));
    EXPECT_TRUE(f.roll.isHeaderButtonHoveredForTest(PianoRollComponent::HeaderButtonId::QuantisePitches));
    EXPECT_EQ(f.roll.headerButtonRequests, 1);
    EXPECT_EQ(f.roll.lastHeaderButtonStrip, f.roll.getQuantisePitchButtonBounds());

    // Same chip again: nothing.
    f.roll.mouseMove(hover(f.roll, centreOf(f.roll.getQuantisePitchButtonBounds()) + juce::Point<float>(1.0f, 0.0f)));
    EXPECT_EQ(f.roll.headerButtonRequests, 1);

    // Onto the neighbouring "Q" chip: the vacated rect plus the new one.
    f.roll.mouseMove(hover(f.roll, centreOf(f.roll.getQuantiseButtonBounds())));
    EXPECT_TRUE(f.roll.isHeaderButtonHoveredForTest(PianoRollComponent::HeaderButtonId::Quantise));
    EXPECT_EQ(f.roll.headerButtonRequests, 3);
}

TEST(PianoRollPitchQuantiseTest, ChipTooltipCarriesTheDynamicShortcutHint) {
    PianoRollFixture f;
    const auto tooltip = f.roll.getTooltipFor(f.roll.getQuantisePitchButtonBounds().getCentre());
    // No ShortcutManager installed, so the hint falls back to the hardcoded Option+Shift+Q.
    EXPECT_TRUE(tooltip.startsWith("Quantize note pitches into the scale (Alt + Shift + Q)")) << tooltip;
    EXPECT_TRUE(tooltip.contains("Scale Assist")) << tooltip;
}

// Both quantise keys dispatch through keyPressed with the Option-based defaults. Stored as a KEY
// CODE plus modifiers, which is what makes them survive macOS delivering Option+letter as a Unicode
// glyph — the same reasoning FocusArbitrationTests' ShiftedSymbolKeyCodes case pins for the grid
// commands.
TEST(PianoRollPitchQuantiseTest, QAndOptionShiftQDispatchTheTwoQuantiseVerbs) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);
    chooseMajorScaleForOpenClip(f);

    const auto id = f.doc.addNote(clipId, makeNote(1.1, 61, 1.0));
    ASSERT_TRUE(id.isValid());

    const juce::KeyPress bareQ('q', juce::ModifierKeys::noModifiers, 0);
    const juce::KeyPress optionShiftQ(
        'q', juce::ModifierKeys(juce::ModifierKeys::altModifier | juce::ModifierKeys::shiftModifier), 0);

    // The MORE SPECIFIC chord is matched first, so Option+Shift+Q is never swallowed by bare Q.
    EXPECT_TRUE(f.roll.keyPressed(optionShiftQ));
    EXPECT_EQ(f.doc.getNote(id)->pitch, 60);
    EXPECT_DOUBLE_EQ(f.doc.getNote(id)->startBeat, 1.1) << "pitch quantise never touches the start";

    EXPECT_TRUE(f.roll.keyPressed(bareQ));
    EXPECT_DOUBLE_EQ(f.doc.getNote(id)->startBeat, 1.0);
    EXPECT_EQ(f.doc.getNote(id)->pitch, 60) << "start quantise never touches the pitch";

    // Neither quantise key flips the snap switch — J is what does that.
    int toggles = 0;
    f.roll.onSnapToggled = [&] { ++toggles; };
    EXPECT_TRUE(f.roll.keyPressed(bareQ));
    EXPECT_EQ(toggles, 0);
    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress('j')));
    EXPECT_EQ(toggles, 1);
}

// With no scale chosen the pitch key falls THROUGH (returns false) rather than being swallowed —
// the same "the key wasn't applicable" contract the arrow keys follow with an empty selection.
TEST(PianoRollPitchQuantiseTest, OptionShiftQFallsThroughWithNoScaleChosen) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);
    ASSERT_TRUE(f.doc.addNote(clipId, makeNote(1.0, 61, 1.0)).isValid());

    const juce::KeyPress optionShiftQ(
        'q', juce::ModifierKeys(juce::ModifierKeys::altModifier | juce::ModifierKeys::shiftModifier), 0);
    EXPECT_FALSE(f.roll.keyPressed(optionShiftQ));
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(PianoRollPitchQuantiseTest, PitchQuantiseHonoursTheSelectionSubset) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);
    chooseMajorScaleForOpenClip(f);

    const auto idA = f.doc.addNote(clipId, makeNote(1.0, 61, 1.0));
    const auto idB = f.doc.addNote(clipId, makeNote(5.0, 63, 1.0));
    f.roll.getSelectionForTest().setSelection({idA});

    EXPECT_TRUE(f.roll.quantisePitchesToActiveScale());
    EXPECT_EQ(f.doc.getNote(idA)->pitch, 60);
    EXPECT_EQ(f.doc.getNote(idB)->pitch, 63) << "an unselected note is untouched";
    EXPECT_TRUE(f.roll.hasNoteSelection()) << "the selection is deliberately left exactly as it was";
}
