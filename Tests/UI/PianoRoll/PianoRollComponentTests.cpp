// PianoRoll core component tests: open/close lifecycle, the gesture table (single click
// deselects, DOUBLE-click creates/deletes, drag moves/resizes, drag-from-empty marquees), note
// length following the snap division, clip-window clamping, the roll's own beat<->x mapping
// (first bar reachable, zoom around the cursor, gridline density), the local playhead's
// strip-confined repaint seam, the Q button, and a snapshot smoke test.
// Shared PianoRollFixture, CountingRoll and mouse-gesture helpers live in PianoRollTestHelpers.h.

#include "PianoRollTestHelpers.h"

// ============================================================================
// 3. PianoRollComponent
// ============================================================================

// ---- Open/close lifecycle ----

TEST(PianoRollLifecycleTest, OpenCloseLifecycle) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip A");
    ASSERT_TRUE(clipId.isValid());

    bool closeRequested = false;
    f.roll.onCloseRequested = [&] { closeRequested = true; };

    // The double-click hook (TimelineClipLaneArea::onClipDoubleClicked) forwards straight to
    // openClip via the panel's openPianoRoll — exercised directly here.
    f.roll.openClip(clipId);
    EXPECT_TRUE(f.roll.isOpen());
    EXPECT_EQ(f.roll.getClipId(), clipId);

    // Back button closes, and notifies the owner.
    f.roll.mouseDown(leftClick(f.roll, centreOf(f.roll.getBackButtonBounds())));
    EXPECT_FALSE(f.roll.isOpen());
    EXPECT_TRUE(closeRequested);

    // Escape closes when nothing is selected.
    closeRequested = false;
    f.roll.openClip(clipId);
    ASSERT_TRUE(f.roll.isOpen());
    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_FALSE(f.roll.isOpen());
    EXPECT_TRUE(closeRequested);

    // Deleting the edited clip from the doc closes the roll (refreshFromDoc's own contract).
    closeRequested = false;
    f.roll.openClip(clipId);
    ASSERT_TRUE(f.roll.isOpen());
    ASSERT_TRUE(f.doc.removeClip(clipId));
    f.roll.refreshFromDoc();
    EXPECT_FALSE(f.roll.isOpen());
    EXPECT_TRUE(closeRequested);
}

TEST(PianoRollLifecycleTest, EscapeWithSelectionClearsFirstRatherThanClosing) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    const auto id = f.doc.addNote(clipId, makeNote(1.0, 60));
    f.roll.openClip(clipId);
    f.roll.getSelectionForTest().setSelection({id});

    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_TRUE(f.roll.getSelectionForTest().isEmpty());
    EXPECT_TRUE(f.roll.isOpen()) << "first Escape only clears the selection";

    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_FALSE(f.roll.isOpen()) << "second Escape (nothing selected) closes the roll";
}

// ---- Create: DOUBLE-click on empty grid, snapped, one snap division long, one undo step ----

TEST(PianoRollEditingTest, DoubleClickCreatesSnappedNoteOneStep) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);

    const int pitch = f.roll.getFirstVisiblePitchForTest() - 2; // a row comfortably inside the grid
    const juce::Point<float> pos((float)f.roll.beatToX(2.1), (float)f.roll.yForPitch(pitch) + 5.0f);

    f.roll.mouseDoubleClick(leftClick(f.roll, pos));

    const auto* clip = f.doc.getClip(clipId);
    ASSERT_EQ(clip->notes.size(), 1u);
    const auto& note = clip->notes[0];
    EXPECT_DOUBLE_EQ(note.startBeat, 2.0);
    EXPECT_DOUBLE_EQ(note.lengthBeats, 1.0) << "one snap division (Snap::Quarter) long";
    EXPECT_EQ(note.pitch, pitch);
    EXPECT_EQ(note.velocity, 100);
    EXPECT_TRUE(f.roll.getSelectionForTest().contains(note.id)) << "the new note ends up selected";

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.doc.getClip(clipId)->notes.size(), 0u);
    EXPECT_FALSE(f.undo.canUndo()) << "the whole create was ONE undo step";
}

// A single click on empty grid is a plain click-through: it DESELECTS and never draws (the old
// pencil-by-default behaviour is gone — creating a note is the double-click above).
TEST(PianoRollEditingTest, SingleClickOnEmptyGridDeselectsAndCreatesNothing) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);
    const auto id = f.doc.addNote(clipId, makeNote(1.0, 60));
    ASSERT_TRUE(id.isValid());
    f.roll.getSelectionForTest().setSelection({id});

    const int pitch = f.roll.getFirstVisiblePitchForTest() - 4;
    const juce::Point<float> empty((float)f.roll.beatToX(5.0), (float)f.roll.yForPitch(pitch) + 5.0f);

    f.roll.mouseDown(leftClick(f.roll, empty));
    f.roll.mouseUp(leftClick(f.roll, empty));

    EXPECT_EQ(f.doc.getClip(clipId)->notes.size(), 1u) << "a single click never creates a note";
    EXPECT_TRUE(f.roll.getSelectionForTest().isEmpty()) << "it deselects instead";
    EXPECT_FALSE(f.undo.canUndo()) << "and writes no undo step";

    // A drag from empty grid CREATES nothing either — it marquees (section 7 owns that behaviour;
    // this arrangement used to assert the drag was inert, back when the marquee needed Shift).
    const juce::Point<float> dragged(empty.x + 120.0f, empty.y);
    f.roll.mouseDown(leftClick(f.roll, empty));
    f.roll.mouseDrag(leftDrag(f.roll, dragged, empty));
    EXPECT_TRUE(f.roll.isMarqueeActiveForTest()) << "a plain drag from empty grid multi-selects";
    f.roll.mouseUp(leftDrag(f.roll, dragged, empty));
    EXPECT_EQ(f.doc.getClip(clipId)->notes.size(), 1u);
    EXPECT_FALSE(f.roll.isMarqueeActiveForTest());
    EXPECT_FALSE(f.undo.canUndo()) << "selecting is never a document edit";
}

// The new note is exactly ONE snap division long: quantise 1 bar -> a 1-bar note, 1/4 -> a quarter.
TEST(PianoRollEditingTest, NewNoteLengthFollowsTheSnapDivision) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);
    const float y = (float)f.roll.yForPitch(f.roll.getFirstVisiblePitchForTest() - 2) + 5.0f;

    // Bar (4 beats with no transport wired — the same 4.0 fallback every timeline sub-component uses).
    f.state.snap = TimelineViewState::Snap::Bar;
    f.roll.mouseDoubleClick(leftClick(f.roll, {(float)f.roll.beatToX(5.0), y}));
    const auto* clip = f.doc.getClip(clipId);
    ASSERT_EQ(clip->notes.size(), 1u);
    EXPECT_DOUBLE_EQ(clip->notes[0].startBeat, 4.0);
    EXPECT_DOUBLE_EQ(clip->notes[0].lengthBeats, 4.0) << "1 bar quantise -> a 1-bar note";

    // A sixteenth note (0.25 beat), three rows lower (further from the header, still well inside
    // the grid).
    f.state.snap = TimelineViewState::Snap::Sixteenth;
    f.roll.mouseDoubleClick(leftClick(f.roll, {(float)f.roll.beatToX(10.1), y + 30.0f}));
    ASSERT_EQ(f.doc.getClip(clipId)->notes.size(), 2u);
    const auto& sixteenth = f.doc.getClip(clipId)->notes[1];
    EXPECT_DOUBLE_EQ(sixteenth.startBeat, 10.0);
    EXPECT_DOUBLE_EQ(sixteenth.lengthBeats, 0.25) << "1/16 quantise -> a sixteenth-long note";
}

// A double-click ON a note deletes it (standard DAW idiom), in one undo step — and creating and
// deleting are the same gesture on opposite targets.
TEST(PianoRollEditingTest, DoubleClickOnNoteDeletesOneStep) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);

    const int pitch = f.roll.getFirstVisiblePitchForTest() - 2;
    const juce::Point<float> pos((float)f.roll.beatToX(2.0) + 4.0f, (float)f.roll.yForPitch(pitch) + 5.0f);
    f.roll.mouseDoubleClick(leftClick(f.roll, pos)); // create
    ASSERT_EQ(f.doc.getClip(clipId)->notes.size(), 1u);
    const auto id = f.doc.getClip(clipId)->notes[0].id;

    f.roll.mouseDoubleClick(leftClick(f.roll, centreOf(f.roll.getNoteRect(id)))); // delete
    EXPECT_EQ(f.doc.getClip(clipId)->notes.size(), 0u);
    EXPECT_FALSE(f.roll.getSelectionForTest().contains(id));

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.doc.getClip(clipId)->notes.size(), 1u) << "the delete was ONE undo step";
    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.doc.getClip(clipId)->notes.size(), 0u) << "and the create before it was another";
}

// Click-to-select, then drag the body: the existing snapped-move machinery still commits once.
TEST(PianoRollEditingTest, ClickSelectsThenDragMovesOneStep) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);
    const auto id = f.doc.addNote(clipId, makeNote(2.0, 60, 1.0));
    ASSERT_TRUE(id.isValid());
    ASSERT_TRUE(f.roll.getSelectionForTest().isEmpty());

    const auto rect = f.roll.getNoteRect(id);
    const juce::Point<float> anchor = centreOf(rect);
    const juce::Point<float> dragged(anchor.x + 40.0f, anchor.y); // +1 beat at 40 px/beat

    f.roll.mouseDown(leftClick(f.roll, anchor));
    EXPECT_TRUE(f.roll.getSelectionForTest().contains(id)) << "a plain click on a note selects it";
    f.roll.mouseDrag(leftDrag(f.roll, dragged, anchor));
    f.roll.mouseUp(leftDrag(f.roll, dragged, anchor));

    EXPECT_DOUBLE_EQ(f.doc.getNote(id)->startBeat, 3.0);
    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_DOUBLE_EQ(f.doc.getNote(id)->startBeat, 2.0);
    EXPECT_FALSE(f.undo.canUndo()) << "the move was ONE undo step";
}

// ---- Move (multi-selection, together) and right-edge resize ----

TEST(PianoRollEditingTest, MoveAndResize) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);

    const auto id1 = f.doc.addNote(clipId, makeNote(2.0, 60, 1.0));
    const auto id2 = f.doc.addNote(clipId, makeNote(6.0, 64, 1.0));
    ASSERT_TRUE(id1.isValid());
    ASSERT_TRUE(id2.isValid());
    f.roll.getSelectionForTest().setSelection({id1, id2});

    // Grab id1's body, drag +1 beat and up 2 semitones — both notes move together.
    const auto rect1 = f.roll.getNoteRect(id1);
    const juce::Point<float> anchor((float)rect1.getCentreX(), (float)rect1.getCentreY());
    const juce::Point<float> dragged(anchor.x + 40.0f, anchor.y - 20.0f);

    f.roll.mouseDown(leftClick(f.roll, anchor));
    f.roll.mouseDrag(leftDrag(f.roll, dragged, anchor));
    f.roll.mouseUp(leftDrag(f.roll, dragged, anchor));

    const auto* note1 = f.doc.getNote(id1);
    const auto* note2 = f.doc.getNote(id2);
    ASSERT_NE(note1, nullptr);
    ASSERT_NE(note2, nullptr);
    EXPECT_DOUBLE_EQ(note1->startBeat, 3.0);
    EXPECT_EQ(note1->pitch, 62);
    EXPECT_DOUBLE_EQ(note2->startBeat, 7.0) << "same shared delta, moved together";
    EXPECT_EQ(note2->pitch, 66);

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_DOUBLE_EQ(f.doc.getNote(id1)->startBeat, 2.0);
    EXPECT_EQ(f.doc.getNote(id1)->pitch, 60);
    EXPECT_FALSE(f.undo.canUndo()) << "the multi-note move was ONE undo step";

    // Right-edge resize: both notes are still SELECTED (the undo above restored their geometry, not
    // the selection), so the grabbed edge trims the whole group by one shared delta — see the
    // multi-note resize section further down for the full contract.
    const auto rect1b = f.roll.getNoteRect(id1);
    const juce::Point<float> edge((float)rect1b.getRight() - 2.0f, (float)rect1b.getCentreY());
    const juce::Point<float> draggedEdge(edge.x + 40.0f, edge.y); // +1 beat

    f.roll.mouseDown(leftClick(f.roll, edge));
    f.roll.mouseDrag(leftDrag(f.roll, draggedEdge, edge));
    f.roll.mouseUp(leftDrag(f.roll, draggedEdge, edge));

    const auto* resized1 = f.doc.getNote(id1);
    const auto* resized2 = f.doc.getNote(id2);
    ASSERT_NE(resized1, nullptr);
    EXPECT_DOUBLE_EQ(resized1->startBeat, 2.0) << "resize never moves the start";
    EXPECT_DOUBLE_EQ(resized1->lengthBeats, 2.0);
    EXPECT_DOUBLE_EQ(resized2->lengthBeats, 2.0) << "the whole selection took the same +1 beat delta";
    ASSERT_TRUE(f.undo.canUndo());
}

// ---- Velocity scrub: Cmd+drag, ~1/px, clamped, one step ----

TEST(PianoRollEditingTest, VelocityScrub) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);

    auto n = makeNote(1.0, 60, 1.0);
    n.velocity = 80;
    const auto id = f.doc.addNote(clipId, n);
    ASSERT_TRUE(id.isValid());
    f.roll.getSelectionForTest().setSelection({id});

    const auto rect = f.roll.getNoteRect(id);
    const juce::Point<float> anchor((float)rect.getCentreX(), (float)rect.getCentreY());
    const juce::Point<float> dragged(anchor.x, anchor.y - 15.0f); // up 15 px -> +15 velocity

    f.roll.mouseDown(leftClick(f.roll, anchor, juce::ModifierKeys::altModifier));
    f.roll.mouseDrag(leftDrag(f.roll, dragged, anchor, juce::ModifierKeys::altModifier));
    f.roll.mouseUp(leftDrag(f.roll, dragged, anchor, juce::ModifierKeys::altModifier));

    const auto* updated = f.doc.getNote(id);
    ASSERT_NE(updated, nullptr);
    EXPECT_EQ(updated->velocity, 95);

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.doc.getNote(id)->velocity, 80);
    EXPECT_FALSE(f.undo.canUndo()) << "the scrub was ONE undo step";

    // Clamped at 127.
    f.roll.getSelectionForTest().setSelection({id});
    const juce::Point<float> draggedFar(anchor.x, anchor.y - 400.0f);
    f.roll.mouseDown(leftClick(f.roll, anchor, juce::ModifierKeys::altModifier));
    f.roll.mouseDrag(leftDrag(f.roll, draggedFar, anchor, juce::ModifierKeys::altModifier));
    f.roll.mouseUp(leftDrag(f.roll, draggedFar, anchor, juce::ModifierKeys::altModifier));
    EXPECT_EQ(f.doc.getNote(id)->velocity, 127);
}

// ---- Delete: double-click and the Delete/Backspace key ----

TEST(PianoRollEditingTest, DeleteViaDoubleClickAndKey) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);

    const auto idA = f.doc.addNote(clipId, makeNote(1.0, 60));
    const auto idB = f.doc.addNote(clipId, makeNote(3.0, 64));
    ASSERT_TRUE(idA.isValid());
    ASSERT_TRUE(idB.isValid());

    const auto rectA = f.roll.getNoteRect(idA);
    f.roll.mouseDoubleClick(leftClick(f.roll, centreOf(rectA)));
    EXPECT_EQ(f.doc.getClip(clipId)->notes.size(), 1u);
    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.doc.getClip(clipId)->notes.size(), 2u);
    EXPECT_FALSE(f.undo.canUndo());

    f.roll.getSelectionForTest().setSelection({idA, idB});
    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)));
    EXPECT_EQ(f.doc.getClip(clipId)->notes.size(), 0u);
    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.doc.getClip(clipId)->notes.size(), 2u) << "both notes came back in ONE undo";
    EXPECT_FALSE(f.undo.canUndo());

    f.roll.getSelectionForTest().clear();
    EXPECT_FALSE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)))
        << "empty selection: the key falls through";
}

// ---- Quantise: selected subset (per-note moveNote) vs none-selected (doc.quantiseNotes) ----

TEST(PianoRollEditingTest, QuantiseSelectedAndAll) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);

    const auto idA = f.doc.addNote(clipId, makeNote(1.1, 60));
    const auto idB = f.doc.addNote(clipId, makeNote(2.6, 64));
    ASSERT_TRUE(idA.isValid());
    ASSERT_TRUE(idB.isValid());

    // Selected subset: only idA quantises (per-note moveNote path — quantiseNotes has no subset
    // overload). A PLAIN click on "Q" is the quantise verb (Shift+click is the snap toggle).
    f.roll.getSelectionForTest().setSelection({idA});
    f.roll.mouseDown(leftClick(f.roll, centreOf(f.roll.getQuantiseButtonBounds())));

    EXPECT_DOUBLE_EQ(f.doc.getNote(idA)->startBeat, 1.0);
    EXPECT_DOUBLE_EQ(f.doc.getNote(idB)->startBeat, 2.6) << "unselected note is untouched";
    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_DOUBLE_EQ(f.doc.getNote(idA)->startBeat, 1.1);
    EXPECT_FALSE(f.undo.canUndo());

    // Nothing selected: quantises EVERY note in the clip via doc.quantiseNotes.
    f.roll.getSelectionForTest().clear();
    f.roll.mouseDown(leftClick(f.roll, centreOf(f.roll.getQuantiseButtonBounds())));

    EXPECT_DOUBLE_EQ(f.doc.getNote(idA)->startBeat, 1.0);
    EXPECT_DOUBLE_EQ(f.doc.getNote(idB)->startBeat, 3.0) << "an empty selection means ALL notes";
    ASSERT_TRUE(f.undo.canUndo());
}

// Clicking Q always gives feedback (a momentary highlight), but an ALREADY-quantised clip mutates
// nothing and must not push an undo step — the house no-op rule.
TEST(PianoRollEditingTest, QuantiseNoOpWritesNoUndoStep) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);
    const auto idA = f.doc.addNote(clipId, makeNote(1.1, 60));
    const auto idB = f.doc.addNote(clipId, makeNote(2.6, 64));
    ASSERT_TRUE(idA.isValid());
    ASSERT_TRUE(idB.isValid());

    const auto qCentre = centreOf(f.roll.getQuantiseButtonBounds());
    f.roll.mouseDown(leftClick(f.roll, qCentre)); // real change -> one undo step
    ASSERT_TRUE(f.undo.canUndo());
    EXPECT_TRUE(f.roll.isQuantiseFlashingForTest()) << "the click flashes the button";

    f.roll.mouseDown(leftClick(f.roll, qCentre)); // already quantised -> nothing to record
    EXPECT_TRUE(f.roll.isQuantiseFlashingForTest()) << "a no-op click still gives visual feedback";

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_DOUBLE_EQ(f.doc.getNote(idA)->startBeat, 1.1) << "ONE undo returns to the pre-quantise state, "
                                                            "so the second click wrote no step";
    EXPECT_DOUBLE_EQ(f.doc.getNote(idB)->startBeat, 2.6);
    EXPECT_FALSE(f.undo.canUndo());
}

// ---- Alt+Q: quantise LENGTH, selected subset (per-note resizeNote) vs none-selected
// (doc.quantiseNoteLengths) -- the length twin of the QuantiseSelectedAndAll test above. Driven
// entirely through keyPressed here; the QuantiseLength header chip that calls the same
// performQuantiseLength() is covered by QuantiseLengthButtonEnabledStateAndTooltip below.

TEST(PianoRollEditingTest, AltQQuantiseLengthSelectedAndAll) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);

    const auto idA = f.doc.addNote(clipId, makeNote(0.0, 60, 1.4));
    const auto idB = f.doc.addNote(clipId, makeNote(2.0, 64, 1.6));
    ASSERT_TRUE(idA.isValid());
    ASSERT_TRUE(idB.isValid());

    const juce::KeyPress altQ('q', juce::ModifierKeys::altModifier, 0);

    // Selected subset: only idA's length changes (per-note resizeNote path — quantiseNoteLengths has
    // no subset overload).
    f.roll.getSelectionForTest().setSelection({idA});
    EXPECT_TRUE(f.roll.keyPressed(altQ));

    EXPECT_DOUBLE_EQ(f.doc.getNote(idA)->lengthBeats, 1.0);
    EXPECT_DOUBLE_EQ(f.doc.getNote(idB)->lengthBeats, 1.6) << "unselected note is untouched";
    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_DOUBLE_EQ(f.doc.getNote(idA)->lengthBeats, 1.4);
    EXPECT_FALSE(f.undo.canUndo());

    // Nothing selected: quantises EVERY note's length in the clip via doc.quantiseNoteLengths.
    f.roll.getSelectionForTest().clear();
    EXPECT_TRUE(f.roll.keyPressed(altQ));

    EXPECT_DOUBLE_EQ(f.doc.getNote(idA)->lengthBeats, 1.0);
    EXPECT_DOUBLE_EQ(f.doc.getNote(idB)->lengthBeats, 2.0) << "an empty selection means ALL notes";
    ASSERT_TRUE(f.undo.canUndo());
}

// A note shorter than half a grid unit must floor at one grid unit rather than vanish/go negative.
TEST(PianoRollEditingTest, AltQQuantiseLengthFlooredAtOneGridUnit) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);
    const auto tiny = f.doc.addNote(clipId, makeNote(0.0, 60, 0.1));
    ASSERT_TRUE(tiny.isValid());

    // Selected-subset path (per-note resizeNote), exercised separately from the all-notes path
    // already covered at the TimelineDoc level (TimelineClipEditingQuantiseTests.cpp).
    f.roll.getSelectionForTest().setSelection({tiny});
    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress('q', juce::ModifierKeys::altModifier, 0)));
    EXPECT_DOUBLE_EQ(f.doc.getNote(tiny)->lengthBeats, 1.0) << "floored at one grid unit, never zero";
}

// One undo step regardless of how many selected notes' lengths actually change.
TEST(PianoRollEditingTest, AltQQuantiseLengthIsOneUndoStep) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);
    const auto idA = f.doc.addNote(clipId, makeNote(0.0, 60, 1.4));
    const auto idB = f.doc.addNote(clipId, makeNote(2.0, 64, 1.6));
    ASSERT_TRUE(idA.isValid());
    ASSERT_TRUE(idB.isValid());

    f.roll.getSelectionForTest().clear(); // quantise-all path
    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress('q', juce::ModifierKeys::altModifier, 0)));
    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_DOUBLE_EQ(f.doc.getNote(idA)->lengthBeats, 1.4) << "ONE undo returns both notes to their "
                                                              "pre-quantise lengths";
    EXPECT_DOUBLE_EQ(f.doc.getNote(idB)->lengthBeats, 1.6);
    EXPECT_FALSE(f.undo.canUndo());
}

// Snap no longer has its own header chip (FRO108: it duplicated the timeline toolbar's own Snap
// button, which reads/writes the SAME shared TimelineViewState::snapEnabled by reference) — the J
// key is now the only piano-roll-local way to flip it. It toggles grid magnetism and moves no note.
TEST(PianoRollEditingTest, JKeyTogglesSnapWithoutMovingNotes) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);
    const auto idA = f.doc.addNote(clipId, makeNote(1.1, 60));
    ASSERT_TRUE(idA.isValid());
    ASSERT_TRUE(f.state.snapEnabled);

    int toggles = 0;
    f.roll.onSnapToggled = [&] { ++toggles; };

    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress('j')));
    EXPECT_FALSE(f.state.snapEnabled) << "J flips the switch off";
    EXPECT_EQ(toggles, 1);
    EXPECT_DOUBLE_EQ(f.doc.getNote(idA)->startBeat, 1.1) << "toggling never moves notes";
    EXPECT_FALSE(f.undo.canUndo()) << "a view-state toggle is not a document edit";
    EXPECT_FALSE(f.roll.isQuantiseFlashingForTest())
        << "the flash belongs to the QUANTISE chip; a snap toggle has its own lit state to show";

    // With the switch off the effective (MAGNETIC) grid is gone — edits go free-hand…
    EXPECT_DOUBLE_EQ(f.roll.getGridDivisionForTest(), 0.0);
    // …but the DRAWN grid is untouched, which is the whole point of the split (see drawnGridBeats).
    EXPECT_DOUBLE_EQ(f.roll.getDrawnGridDivisionForTest(), 1.0) << "snap governs magnetism, never visibility";

    // …and a second J press toggles it right back.
    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress('j')));
    EXPECT_TRUE(f.state.snapEnabled);
    EXPECT_EQ(toggles, 2);
    EXPECT_DOUBLE_EQ(f.roll.getGridDivisionForTest(), 1.0);
}

// Bare Q (the key) is the one-shot start-quantise, and it works from the CHOSEN division even while
// the magnetism switch is off — that is the whole point of a one-shot clean-up.
TEST(PianoRollEditingTest, QKeyQuantisesEvenWhileSnapToggledOff) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);
    const auto idA = f.doc.addNote(clipId, makeNote(1.1, 60));
    ASSERT_TRUE(idA.isValid());

    f.state.snapEnabled = false;
    EXPECT_TRUE(f.roll.isQuantiseEnabled()) << "the one-shot reads the RAW division, not the switch";

    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress('q')));
    EXPECT_DOUBLE_EQ(f.doc.getNote(idA)->startBeat, 1.0);
    EXPECT_FALSE(f.state.snapEnabled) << "Q quantises and never flips the switch";
    ASSERT_TRUE(f.undo.canUndo());
}

// The button paints dimmed when it would do nothing, and carries the tooltip that explains its
// selection-vs-all behaviour.
TEST(PianoRollEditingTest, QuantiseButtonEnabledStateAndTooltip) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);
    EXPECT_FALSE(f.roll.isQuantiseEnabled()) << "an empty clip has nothing to quantise";

    ASSERT_TRUE(f.doc.addNote(clipId, makeNote(1.1, 60)).isValid());
    EXPECT_TRUE(f.roll.isQuantiseEnabled());

    f.state.snap = TimelineViewState::Snap::Off;
    EXPECT_FALSE(f.roll.isQuantiseEnabled()) << "Snap::Off leaves no grid to quantise to";

    // Dynamic (see synth::shortcutHintFor) — no ShortcutManager installed, so it falls back to the
    // hardcoded default: bare "q", lower-cased.
    const auto tooltip = f.roll.getTooltipFor(f.roll.getQuantiseButtonBounds().getCentre());
    EXPECT_TRUE(tooltip.startsWith("Quantize note starts to the grid (q)")) << tooltip;
    EXPECT_TRUE(f.roll.getTooltipFor(f.roll.getBackButtonBounds().getCentre()).isEmpty());
}

// The QuantiseLength chip's twin of the test above: same isQuantiseEnabled() gate as the Quantise
// (position) chip (performQuantiseLength() uses it too), and its own dynamic tooltip reflecting the
// live "pianoRollQuantiseLength" binding.
TEST(PianoRollEditingTest, QuantiseLengthButtonEnabledStateAndTooltip) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);
    EXPECT_FALSE(f.roll.isQuantiseEnabled()) << "an empty clip has nothing to quantise";

    ASSERT_TRUE(f.doc.addNote(clipId, makeNote(1.1, 60)).isValid());
    EXPECT_TRUE(f.roll.isQuantiseEnabled());

    f.state.snap = TimelineViewState::Snap::Off;
    EXPECT_FALSE(f.roll.isQuantiseEnabled()) << "Snap::Off leaves no grid to quantise to — same gate "
                                                "as the Quantise (position) chip";

    // Dynamic (see synth::shortcutHintFor) — no ShortcutManager installed, so it falls back to the
    // hardcoded default: Alt+Q.
    const auto tooltip = f.roll.getTooltipFor(f.roll.getQuantiseLengthButtonBounds().getCentre());
    EXPECT_TRUE(tooltip.startsWith("Quantize note lengths to the grid (Alt + Q)")) << tooltip;
}

// ---- Edits clamped to the clip window ----

TEST(PianoRollEditingTest, EditsClampedToClipWindow) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 4.0, "Clip"); // [0, 4)
    f.open(clipId);

    // Create past the clip's END: the start snaps back inside the window rather than producing a
    // note that escapes it (or no note at all).
    const int pitch = f.roll.getFirstVisiblePitchForTest();
    const juce::Point<float> pastEnd((float)f.roll.beatToX(3.8), (float)f.roll.yForPitch(pitch) + 5.0f);
    f.roll.mouseDoubleClick(leftClick(f.roll, pastEnd));

    const auto* clip = f.doc.getClip(clipId);
    ASSERT_EQ(clip->notes.size(), 1u);
    const auto& created = clip->notes[0];
    EXPECT_DOUBLE_EQ(created.startBeat, 3.0) << "snapping up to beat 4 would leave no room — stepped back";
    EXPECT_LE(created.startBeat + created.lengthBeats, 4.0 + 1e-9) << "clamped to the clip's own end";
    EXPECT_GT(created.lengthBeats, 0.0) << "still a valid, positive-length note";

    // Move a note wildly past the clip's end: the shared delta clamps so the group's end never
    // crosses the clip boundary (TimelineClipLaneArea's clamp-the-group-together reasoning,
    // extended with an upper bound because notes — unlike clips — live inside one).
    const auto id = f.doc.addNote(clipId, makeNote(1.0, 60, 1.0)); // [1, 2)
    ASSERT_TRUE(id.isValid());
    f.roll.getSelectionForTest().setSelection({id});
    const auto rect = f.roll.getNoteRect(id);
    const juce::Point<float> moveAnchor((float)rect.getCentreX(), (float)rect.getCentreY());
    const juce::Point<float> moveDragged(moveAnchor.x + 4000.0f, moveAnchor.y);

    f.roll.mouseDown(leftClick(f.roll, moveAnchor));
    f.roll.mouseDrag(leftDrag(f.roll, moveDragged, moveAnchor));
    f.roll.mouseUp(leftDrag(f.roll, moveDragged, moveAnchor));

    const auto* moved = f.doc.getNote(id);
    ASSERT_NE(moved, nullptr);
    EXPECT_GE(moved->startBeat, 0.0);
    EXPECT_LE(moved->startBeat + moved->lengthBeats, 4.0 + 1e-9);
}

// ---- Pitch-scroll clamps at the extremes ----

TEST(PianoRollInteractionTest, PitchScrollClamps) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);

    juce::MouseWheelDetails wheelUp{};
    wheelUp.deltaY = 50.0f; // far beyond a real wheel's range — exercises the clamp, not the tuning
    for (int i = 0; i < 50; ++i)
        f.roll.mouseWheelMove(leftClick(f.roll, {100.0f, 100.0f}), wheelUp);
    EXPECT_GE(f.roll.getFirstVisiblePitchForTest(), 0);
    EXPECT_LE(f.roll.getFirstVisiblePitchForTest(), 127);

    juce::MouseWheelDetails wheelDown{};
    wheelDown.deltaY = -50.0f;
    for (int i = 0; i < 50; ++i)
        f.roll.mouseWheelMove(leftClick(f.roll, {100.0f, 100.0f}), wheelDown);
    EXPECT_GE(f.roll.getFirstVisiblePitchForTest(), 0);
    EXPECT_LE(f.roll.getFirstVisiblePitchForTest(), 127);
}

// ---- The roll's OWN mapping: the keys column is a gutter, so the first bar is reachable ----

TEST(PianoRollInteractionTest, FirstBarIsReachableRightOfTheKeysColumn) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 4.0, 8.0, "Clip"); // starts at absolute beat 4
    f.roll.openClip(clipId);                                      // framing comes from openClip itself

    // openClip parks the clip's start at the keys column's right edge and fits the whole clip.
    EXPECT_DOUBLE_EQ(f.roll.getFirstVisibleBeat(), 4.0);
    EXPECT_DOUBLE_EQ(f.roll.beatToX(4.0), (double)PianoRollComponent::kKeysColumnWidth);
    EXPECT_GE(f.roll.beatToX(12.0), (double)f.roll.getWidth() - 1.0) << "the whole clip fits the grid";

    // A note can be CREATED at the clip's very first beat — the bug was that those pixels were
    // hidden under an opaque keys strip that ignored clicks.
    const int pitch = f.roll.getFirstVisiblePitchForTest() - 2;
    const juce::Point<float> firstBeat((float)PianoRollComponent::kKeysColumnWidth + 2.0f,
                                       (float)f.roll.yForPitch(pitch) + 5.0f);
    f.roll.mouseDoubleClick(leftClick(f.roll, firstBeat));

    const auto* clip = f.doc.getClip(clipId);
    ASSERT_EQ(clip->notes.size(), 1u);
    EXPECT_DOUBLE_EQ(clip->notes[0].startBeat, 0.0) << "clip-relative beat 0 — the clip's very first beat";

    const auto rect = f.roll.getNoteRect(clip->notes[0].id);
    EXPECT_GE(rect.getX(), PianoRollComponent::kKeysColumnWidth) << "and it draws RIGHT OF the keys gutter";
    EXPECT_EQ(rect.getX(), (int)std::llround(f.roll.beatToX(4.0)));
}

// ---- Fine-snap sanity: a new note is exactly ONE division long (see computeNewNoteAnchor), and
// nothing floors or rejects a division as fine as 1/128 (0.03125 beats). TimelineDoc::addNote only
// rejects a NON-POSITIVE/non-finite length (isFinitePositive: `> 0.0`, no minimum), and
// computeNewNoteAnchor only falls back to kMinNoteLengthBeats when the grid is OFF (`grid > 0.0 ?
// grid : kMinNoteLengthBeats`) — a real division smaller than kMinNoteLengthBeats is used AS IS.
TEST(PianoRollInteractionTest, NoteCreatedAtOneHundredTwentyEighthGridHasExactlyThatLength) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);

    // Set directly on the shared view state, per the class comment: it is consulted for the snap
    // division ONLY.
    f.state.snap = TimelineViewState::Snap::HundredTwentyEighth;
    f.state.snapEnabled = true;

    const int pitch = f.roll.getFirstVisiblePitchForTest() - 2;
    const juce::Point<float> pos((float)f.roll.beatToX(2.0), (float)f.roll.yForPitch(pitch) + 5.0f);
    f.roll.mouseDoubleClick(leftClick(f.roll, pos));

    const auto* clip = f.doc.getClip(clipId);
    ASSERT_EQ(clip->notes.size(), 1u);
    EXPECT_DOUBLE_EQ(clip->notes[0].lengthBeats, 0.03125);
}

// Cmd+wheel zooms the roll's own mapping around the cursor: the beat under the pointer does not
// move, and the SHARED view state (the lanes behind the roll) is untouched.
TEST(PianoRollInteractionTest, CmdWheelZoomsAroundTheCursorBeat) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 4.0, 8.0, "Clip");
    f.open(clipId);

    const float anchorX = 300.0f;
    const double beatUnderCursor = f.roll.xToBeat((double)anchorX);

    juce::MouseWheelDetails wheel{}; // value-initialised: the struct has no default member initialisers
    wheel.deltaY = 0.5f;
    f.roll.mouseWheelMove(leftClick(f.roll, {anchorX, 90.0f}, juce::ModifierKeys::commandModifier), wheel);

    EXPECT_GT(f.roll.getPixelsPerBeat(), 40.0) << "positive deltaY zooms in";
    EXPECT_NEAR(f.roll.xToBeat((double)anchorX), beatUnderCursor, 1.0e-9)
        << "the beat under the cursor is the zoom's fixed point";
    EXPECT_DOUBLE_EQ(f.state.pixelsPerBeat, 40.0) << "the panel-wide view state never moves with the roll's zoom";
    EXPECT_DOUBLE_EQ(f.state.firstVisibleBeat, 0.0);

    // And back out again: equal-and-opposite gestures cancel (exponential factor).
    wheel.deltaY = -0.5f;
    f.roll.mouseWheelMove(leftClick(f.roll, {anchorX, 90.0f}, juce::ModifierKeys::commandModifier), wheel);
    EXPECT_NEAR(f.roll.getPixelsPerBeat(), 40.0, 1.0e-9);
}

// Cmd+Shift+wheel is the VERTICAL zoom, clamped to the documented bounds.
TEST(PianoRollInteractionTest, CmdShiftWheelZoomsPitchRowsWithinClamps) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);
    ASSERT_DOUBLE_EQ(f.roll.getPixelsPerSemitone(), PianoRollComponent::kPixelsPerSemitone);

    const int mods = juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier;
    juce::MouseWheelDetails wheel{}; // value-initialised: the struct has no default member initialisers
    wheel.deltaY = 0.4f;
    f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}, mods), wheel);
    EXPECT_GT(f.roll.getPixelsPerSemitone(), PianoRollComponent::kPixelsPerSemitone);

    wheel.deltaY = 5.0f; // far past the clamp
    for (int i = 0; i < 20; ++i)
        f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}, mods), wheel);
    EXPECT_DOUBLE_EQ(f.roll.getPixelsPerSemitone(), PianoRollComponent::kMaxPixelsPerSemitone);

    wheel.deltaY = -5.0f;
    for (int i = 0; i < 40; ++i)
        f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}, mods), wheel);
    EXPECT_DOUBLE_EQ(f.roll.getPixelsPerSemitone(), PianoRollComponent::kMinPixelsPerSemitone);
}

// Shift+wheel scrolls time through the roll's own scroll origin (never the shared one).
TEST(PianoRollInteractionTest, ShiftWheelScrollsTimeLocally) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 4.0, 8.0, "Clip");
    f.open(clipId);

    juce::MouseWheelDetails wheel{}; // value-initialised: the struct has no default member initialisers
    wheel.deltaY = -0.5f;            // scroll right
    f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}, juce::ModifierKeys::shiftModifier), wheel);

    EXPECT_GT(f.roll.getFirstVisibleBeat(), 4.0);
    EXPECT_DOUBLE_EQ(f.state.firstVisibleBeat, 0.0) << "the lanes behind the roll keep their own scroll";
}

// ---- Gridlines follow the snap division ----

TEST(PianoRollInteractionTest, GridLinesFollowTheSnapDivision) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 32.0, "Clip");
    f.open(clipId); // 40 px/beat, beat 0 at the gutter

    f.state.snap = TimelineViewState::Snap::Quarter;
    EXPECT_DOUBLE_EQ(f.roll.getGridDivisionForTest(), 1.0);
    const int beatLines = f.roll.getGridLineCountForTest(1.0);
    EXPECT_GT(beatLines, 0);

    f.state.snap = TimelineViewState::Snap::Sixteenth;
    EXPECT_DOUBLE_EQ(f.roll.getGridDivisionForTest(), 0.25);
    const int sixteenthLines = f.roll.getGridLineCountForTest(f.roll.getGridDivisionForTest());
    EXPECT_GT(sixteenthLines, beatLines * 3) << "a finer division draws proportionally more lines";

    f.state.snap = TimelineViewState::Snap::Off;
    EXPECT_DOUBLE_EQ(f.roll.getGridDivisionForTest(), 0.0) << "Snap::Off has no sub-beat level at all";
    EXPECT_EQ(f.roll.getGridLineCountForTest(0.0), 0);

    // Zoomed far out, a sub-beat level is dropped rather than drawn as a wall of lines.
    f.roll.setHorizontalView(TimelineViewState::kMinPixelsPerBeat, 0.0);
    EXPECT_EQ(f.roll.getGridLineCountForTest(0.25), 0);
}

// ---- The local playhead: the roll draws it at ITS OWN x, under the same strip discipline ----

TEST(PianoRollInteractionTest, LocalPlayheadUsesTheRollsOwnMapping) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 4.0, 8.0, "Clip");
    EXPECT_FALSE(f.roll.isLocalPlayheadActive()) << "a closed roll never claims the overlay's rows";
    f.open(clipId); // 40 px/beat with beat 4 at the gutter
    EXPECT_TRUE(f.roll.isLocalPlayheadActive());

    // The overlay hands over the BEAT; the roll maps it itself.
    f.roll.setPlayheadBeat(6.0);
    const int expectedX = (int)std::llround(f.roll.beatToX(6.0));
    EXPECT_EQ(f.roll.getPlayheadLineX(), expectedX);
    EXPECT_EQ(expectedX, PianoRollComponent::kKeysColumnWidth + 80);
    EXPECT_NE(expectedX, (int)std::llround(f.state.beatToX(6.0)))
        << "which is exactly why the overlay must not draw here: the shared mapping puts beat 6 elsewhere";
    EXPECT_EQ(f.roll.requests, 1) << "the first position after an open costs exactly one strip";

    // Stopped: the same beat again and again costs nothing.
    for (int i = 0; i < 5; ++i)
        f.roll.setPlayheadBeat(6.0);
    EXPECT_EQ(f.roll.requests, 1);

    // Playing: each moved position repaints a STRIP, never the component.
    f.roll.setPlayheadBeat(6.5);
    EXPECT_EQ(f.roll.requests, 2);
    EXPECT_EQ(f.roll.lastStrip.getY(), f.roll.canvasTop()) << "confined below the header";
    EXPECT_GE(f.roll.lastStrip.getX(), PianoRollComponent::kKeysColumnWidth) << "and right of the keys gutter";
    EXPECT_LE(f.roll.lastStrip.getWidth(), 20 + 2 * PianoRollComponent::kPlayheadStripHalfWidth + 1);
    EXPECT_LT(f.roll.lastStrip.getWidth(), f.roll.getWidth());

    // A zoom moves the line without the transport moving at all.
    f.roll.setHorizontalView(80.0, 4.0);
    EXPECT_EQ(f.roll.getPlayheadLineX(), (int)std::llround(f.roll.beatToX(6.5)));
}

// ---- Snapshot smoke ----

TEST(PianoRollInteractionTest, SnapshotSmoke) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);

    const auto id = f.doc.addNote(clipId, makeNote(1.0, 60, 1.0));
    ASSERT_TRUE(id.isValid());
    f.roll.getSelectionForTest().setSelection({id});

    f.roll.setSize(900, 160);
    const juce::Image img = f.roll.createComponentSnapshot(f.roll.getLocalBounds());
    EXPECT_FALSE(img.isNull());
    EXPECT_EQ(img.getWidth(), 900);
    EXPECT_EQ(img.getHeight(), 160);
}
