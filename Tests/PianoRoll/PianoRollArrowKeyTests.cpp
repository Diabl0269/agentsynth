// PianoRoll arrow-key tests: grid nudge, semitone/octave transpose, group clamping, the
// fall-through contract when nothing is selected, and Alt+Left/Right note NAVIGATION — walking the
// doc's canonical note order, collapsing a multi-selection, the ends of the run, and the minimal
// scroll that brings an off-screen note into view.
// Shared PianoRollFixture, altArrow and onlySelected helpers live in PianoRollTestHelpers.h.

#include "PianoRollTestHelpers.h"

// ============================================================================
// 6. Arrow-key editing
// ============================================================================

TEST(PianoRollArrowKeyTest, LeftAndRightNudgeByTheGridDivision) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);

    const auto id = f.doc.addNote(clipId, makeNote(2.0, 60, 1.0));
    ASSERT_TRUE(id.isValid());
    f.roll.getSelectionForTest().setSelection({id});

    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    EXPECT_DOUBLE_EQ(f.doc.getNote(id)->startBeat, 3.0);
    EXPECT_EQ(f.doc.getNote(id)->pitch, 60) << "a horizontal nudge never transposes";

    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::leftKey)));
    EXPECT_DOUBLE_EQ(f.doc.getNote(id)->startBeat, 2.0);

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_DOUBLE_EQ(f.doc.getNote(id)->startBeat, 3.0) << "each press is its own undo step";
    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_DOUBLE_EQ(f.doc.getNote(id)->startBeat, 2.0);
    EXPECT_FALSE(f.undo.canUndo());

    // A finer grid nudges by that grid.
    setSnap(f, TimelineViewState::Snap::Sixteenth);
    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    EXPECT_DOUBLE_EQ(f.doc.getNote(id)->startBeat, 2.25);
}

TEST(PianoRollArrowKeyTest, NudgeFallsBackToASixteenthWhenSnapIsOff) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    setSnap(f, TimelineViewState::Snap::Quarter, /*enabled*/ false);
    f.open(clipId);
    ASSERT_DOUBLE_EQ(f.roll.getGridDivisionForTest(), 0.0);

    const auto id = f.doc.addNote(clipId, makeNote(1.0, 60, 1.0));
    ASSERT_TRUE(id.isValid());
    f.roll.getSelectionForTest().setSelection({id});

    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    EXPECT_DOUBLE_EQ(f.doc.getNote(id)->startBeat, 1.0 + PianoRollComponent::kMinNoteLengthBeats);
}

TEST(PianoRollArrowKeyTest, NudgeClampsTheWholeGroupAtBothClipEdges) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 4.0, "Clip"); // [0, 4)
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);

    const auto idA = f.doc.addNote(clipId, makeNote(0.0, 60, 1.0)); // [0, 1)
    const auto idB = f.doc.addNote(clipId, makeNote(2.0, 64, 1.0)); // [2, 3)
    ASSERT_TRUE(idA.isValid());
    ASSERT_TRUE(idB.isValid());
    f.roll.getSelectionForTest().setSelection({idA, idB});

    // One step right fits exactly (the group's end reaches the clip's end)…
    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    EXPECT_DOUBLE_EQ(f.doc.getNote(idA)->startBeat, 1.0);
    EXPECT_DOUBLE_EQ(f.doc.getNote(idB)->startBeat, 3.0);

    // …and the next one is clamped to nothing: the key is still consumed, but nothing moves and no
    // undo step is written. Crucially the group keeps its shape — the leading note does NOT slide
    // on while the trailing one is stuck.
    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    EXPECT_DOUBLE_EQ(f.doc.getNote(idA)->startBeat, 1.0);
    EXPECT_DOUBLE_EQ(f.doc.getNote(idB)->startBeat, 3.0);

    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::leftKey)));
    EXPECT_DOUBLE_EQ(f.doc.getNote(idA)->startBeat, 0.0);
    EXPECT_DOUBLE_EQ(f.doc.getNote(idB)->startBeat, 2.0);

    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::leftKey)));
    EXPECT_DOUBLE_EQ(f.doc.getNote(idA)->startBeat, 0.0) << "clamped at the clip's start";
    EXPECT_DOUBLE_EQ(f.doc.getNote(idB)->startBeat, 2.0);

    // Two real moves, two undo steps — the two clamped presses wrote none.
    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_DOUBLE_EQ(f.doc.getNote(idA)->startBeat, 1.0);
    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_DOUBLE_EQ(f.doc.getNote(idA)->startBeat, 0.0);
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(PianoRollArrowKeyTest, UpAndDownTransposeBySemitoneAndByOctaveWithShift) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);

    const auto id = f.doc.addNote(clipId, makeNote(1.0, 60, 1.0));
    ASSERT_TRUE(id.isValid());
    f.roll.getSelectionForTest().setSelection({id});

    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
    EXPECT_EQ(f.doc.getNote(id)->pitch, 61);
    EXPECT_DOUBLE_EQ(f.doc.getNote(id)->startBeat, 1.0) << "a transpose never moves a note in time";

    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::downKey)));
    EXPECT_EQ(f.doc.getNote(id)->pitch, 60);

    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::upKey, juce::ModifierKeys::shiftModifier, 0)));
    EXPECT_EQ(f.doc.getNote(id)->pitch, 72) << "Shift is the octave jump";

    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::downKey, juce::ModifierKeys::shiftModifier, 0)));
    EXPECT_EQ(f.doc.getNote(id)->pitch, 60);

    // Four presses, four undo steps.
    for (int i = 0; i < 4; ++i) {
        ASSERT_TRUE(f.undo.canUndo());
        f.undo.undo();
    }
    EXPECT_EQ(f.doc.getNote(id)->pitch, 60);
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(PianoRollArrowKeyTest, TransposeClampsTheWholeGroupAtThePitchExtremes) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);

    const auto high = f.doc.addNote(clipId, makeNote(0.0, 120, 1.0));
    const auto higher = f.doc.addNote(clipId, makeNote(1.0, 126, 1.0));
    ASSERT_TRUE(high.isValid());
    ASSERT_TRUE(higher.isValid());
    f.roll.getSelectionForTest().setSelection({high, higher});

    // +12 would take 126 past the top, so the SHARED delta is clamped to +1 — the interval between
    // the two notes survives, which per-note clamping would have destroyed.
    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::upKey, juce::ModifierKeys::shiftModifier, 0)));
    EXPECT_EQ(f.doc.getNote(high)->pitch, 121);
    EXPECT_EQ(f.doc.getNote(higher)->pitch, 127);

    // Same rule at the bottom.
    const auto low = f.doc.addNote(clipId, makeNote(2.0, 1, 1.0));
    const auto lower = f.doc.addNote(clipId, makeNote(3.0, 5, 1.0));
    ASSERT_TRUE(low.isValid());
    ASSERT_TRUE(lower.isValid());
    f.roll.getSelectionForTest().setSelection({low, lower});

    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::downKey, juce::ModifierKeys::shiftModifier, 0)));
    EXPECT_EQ(f.doc.getNote(low)->pitch, 0);
    EXPECT_EQ(f.doc.getNote(lower)->pitch, 4);
}

TEST(PianoRollArrowKeyTest, ArrowKeysFallThroughWhenNothingIsSelected) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);
    ASSERT_TRUE(f.doc.addNote(clipId, makeNote(1.0, 60, 1.0)).isValid());
    ASSERT_TRUE(f.roll.getSelectionForTest().isEmpty());

    EXPECT_FALSE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::leftKey)));
    EXPECT_FALSE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    EXPECT_FALSE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
    EXPECT_FALSE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::downKey)));
    EXPECT_FALSE(f.undo.canUndo());
}

// ============================================================================
// 8. Alt+Left/Right note navigation
// ============================================================================
//
// Selection-only: no test in this section may ever see the doc change or the undo stack grow.

namespace {

// Four notes whose canonical (startBeat, pitch, id) order is A, B, C, D — deliberately ADDED in a
// different order, so a walk that followed insertion (or id) order instead of the doc's would fail.
struct NavBed {
    ClipId clipId;
    NoteId a, b, c, d;
};

NavBed makeNavBed(PianoRollFixture& f) {
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    NavBed bed;
    bed.clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    bed.b = f.doc.addNote(bed.clipId, makeNote(0.0, 64, 1.0)); // same start as A, higher pitch
    bed.d = f.doc.addNote(bed.clipId, makeNote(2.0, 60, 1.0));
    bed.a = f.doc.addNote(bed.clipId, makeNote(0.0, 60, 1.0));
    bed.c = f.doc.addNote(bed.clipId, makeNote(1.0, 62, 1.0));
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(bed.clipId);
    return bed;
}

} // namespace

TEST(PianoRollNavigationTest, AltRightWalksTheDocsCanonicalNoteOrder) {
    PianoRollFixture f;
    const auto bed = makeNavBed(f);
    ASSERT_TRUE(bed.a.isValid());

    // Sanity: the doc really does hold them in (startBeat, pitch, id) order, which is the order the
    // walk below is asserting — not the order they were added in.
    const auto& notes = f.doc.getClip(bed.clipId)->notes;
    ASSERT_EQ(notes.size(), 4u);
    EXPECT_EQ(notes[0].id, bed.a);
    EXPECT_EQ(notes[1].id, bed.b);
    EXPECT_EQ(notes[2].id, bed.c);
    EXPECT_EQ(notes[3].id, bed.d);

    f.roll.getSelectionForTest().setSelection({bed.a});

    // A -> B is the same-start, different-pitch step: the one an order keyed on startBeat alone
    // could not make.
    EXPECT_TRUE(f.roll.keyPressed(altArrow(juce::KeyPress::rightKey)));
    EXPECT_TRUE(onlySelected(f, bed.b));
    EXPECT_TRUE(f.roll.keyPressed(altArrow(juce::KeyPress::rightKey)));
    EXPECT_TRUE(onlySelected(f, bed.c));
    EXPECT_TRUE(f.roll.keyPressed(altArrow(juce::KeyPress::rightKey)));
    EXPECT_TRUE(onlySelected(f, bed.d));

    EXPECT_FALSE(f.undo.canUndo()) << "navigation is selection, not document state";
    EXPECT_DOUBLE_EQ(f.doc.getNote(bed.a)->startBeat, 0.0) << "and it never moves a note";
}

TEST(PianoRollNavigationTest, AltLeftWalksBackThroughTheSameOrder) {
    PianoRollFixture f;
    const auto bed = makeNavBed(f);
    f.roll.getSelectionForTest().setSelection({bed.d});

    EXPECT_TRUE(f.roll.keyPressed(altArrow(juce::KeyPress::leftKey)));
    EXPECT_TRUE(onlySelected(f, bed.c));
    EXPECT_TRUE(f.roll.keyPressed(altArrow(juce::KeyPress::leftKey)));
    EXPECT_TRUE(onlySelected(f, bed.b));
    EXPECT_TRUE(f.roll.keyPressed(altArrow(juce::KeyPress::leftKey)));
    EXPECT_TRUE(onlySelected(f, bed.a));
    EXPECT_FALSE(f.undo.canUndo());
}

// The anchor rule, which is the whole reason a multi-selection cannot be walked "from the
// selection": forward anchors on the LAST selected note, backward on the FIRST, so the step always
// lands OUTSIDE the block instead of back inside it.
TEST(PianoRollNavigationTest, NavigatingFromAMultiSelectionCollapsesOntoTheOuterNeighbour) {
    PianoRollFixture f;
    const auto bed = makeNavBed(f);

    f.roll.getSelectionForTest().setSelection({bed.b, bed.c});
    EXPECT_TRUE(f.roll.keyPressed(altArrow(juce::KeyPress::rightKey)));
    EXPECT_TRUE(onlySelected(f, bed.d)) << "forward anchors on the selection's LAST note";

    f.roll.getSelectionForTest().setSelection({bed.b, bed.c});
    EXPECT_TRUE(f.roll.keyPressed(altArrow(juce::KeyPress::leftKey)));
    EXPECT_TRUE(onlySelected(f, bed.a)) << "backward anchors on its FIRST";
}

// At either end the key is still CONSUMED and the selection kept — the same contract a fully
// clamped nudge honours (see NudgeClampsTheWholeGroupAtBothClipEdges).
TEST(PianoRollNavigationTest, AtEitherEndTheSelectionIsKeptAndTheKeyIsStillConsumed) {
    PianoRollFixture f;
    const auto bed = makeNavBed(f);

    f.roll.getSelectionForTest().setSelection({bed.d});
    EXPECT_TRUE(f.roll.keyPressed(altArrow(juce::KeyPress::rightKey)));
    EXPECT_TRUE(onlySelected(f, bed.d));

    f.roll.getSelectionForTest().setSelection({bed.a});
    EXPECT_TRUE(f.roll.keyPressed(altArrow(juce::KeyPress::leftKey)));
    EXPECT_TRUE(onlySelected(f, bed.a));

    EXPECT_FALSE(f.undo.canUndo());
}

TEST(PianoRollNavigationTest, AltArrowsFallThroughWithNothingSelected) {
    PianoRollFixture f;
    const auto bed = makeNavBed(f);
    ASSERT_TRUE(bed.clipId.isValid());
    ASSERT_TRUE(f.roll.getSelectionForTest().isEmpty());

    // There is plenty to navigate TO — what is missing is somewhere to navigate FROM, and that is
    // what makes the key fall through to the panel instead of picking a note arbitrarily.
    EXPECT_FALSE(f.roll.keyPressed(altArrow(juce::KeyPress::rightKey)));
    EXPECT_FALSE(f.roll.keyPressed(altArrow(juce::KeyPress::leftKey)));
    EXPECT_TRUE(f.roll.getSelectionForTest().isEmpty());
    EXPECT_FALSE(f.undo.canUndo());
}

// Regression: adding Alt must not have changed what the UNmodified arrows do, and Alt+Up/Down stays
// reserved rather than quietly becoming a second transpose.
TEST(PianoRollNavigationTest, PlainArrowsStillNudgeAndAltUpDownIsLeftUnhandled) {
    PianoRollFixture f;
    const auto bed = makeNavBed(f);
    f.roll.getSelectionForTest().setSelection({bed.c});

    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    EXPECT_DOUBLE_EQ(f.doc.getNote(bed.c)->startBeat, 2.0) << "a plain arrow still nudges by the grid";
    EXPECT_TRUE(onlySelected(f, bed.c)) << "and never changes WHICH notes are selected";

    EXPECT_FALSE(f.roll.keyPressed(altArrow(juce::KeyPress::upKey)));
    EXPECT_FALSE(f.roll.keyPressed(altArrow(juce::KeyPress::downKey)));
    EXPECT_EQ(f.doc.getNote(bed.c)->pitch, 62) << "Alt+Up/Down transposes nothing";
    EXPECT_TRUE(onlySelected(f, bed.c)) << "and navigates nowhere";
}

// Navigating off-screen scrolls the roll's OWN horizontal mapping by the minimum that makes the
// target visible — no zoom change, and nothing at all when the target is already on screen.
TEST(PianoRollNavigationTest, NavigatingToAnOffScreenNoteScrollsItIntoView) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 64.0, "Clip");
    const auto nearId = f.doc.addNote(clipId, makeNote(1.0, 60, 1.0));
    const auto farId = f.doc.addNote(clipId, makeNote(40.0, 60, 1.0));
    ASSERT_TRUE(nearId.isValid());
    ASSERT_TRUE(farId.isValid());
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId, /*pixelsPerBeat*/ 40.0);

    // 900 px wide, 44 of them the keys gutter -> 856 px of grid -> 21.4 beats visible at this zoom.
    const double visibleBeats = (double)(900 - PianoRollComponent::kKeysColumnWidth) / f.roll.getPixelsPerBeat();
    ASSERT_DOUBLE_EQ(f.roll.getFirstVisibleBeat(), 0.0);

    f.roll.getSelectionForTest().setSelection({nearId});
    EXPECT_TRUE(f.roll.keyPressed(altArrow(juce::KeyPress::rightKey)));
    EXPECT_TRUE(onlySelected(f, farId));
    // Minimal scroll RIGHT: the note's trailing edge (beat 41) sits exactly on the grid's right edge.
    EXPECT_NEAR(f.roll.getFirstVisibleBeat(), 41.0 - visibleBeats, 1e-9);
    EXPECT_DOUBLE_EQ(f.roll.getPixelsPerBeat(), 40.0) << "a navigation never reframes the zoom";

    // …and back LEFT: the note's leading edge (beat 1) sits exactly on the grid's left edge.
    EXPECT_TRUE(f.roll.keyPressed(altArrow(juce::KeyPress::leftKey)));
    EXPECT_TRUE(onlySelected(f, nearId));
    EXPECT_NEAR(f.roll.getFirstVisibleBeat(), 1.0, 1e-9);

    // An ON-screen step moves the view by nothing at all.
    const auto midId = f.doc.addNote(clipId, makeNote(5.0, 60, 1.0));
    ASSERT_TRUE(midId.isValid());
    const double before = f.roll.getFirstVisibleBeat();
    EXPECT_TRUE(f.roll.keyPressed(altArrow(juce::KeyPress::rightKey)));
    EXPECT_TRUE(onlySelected(f, midId));
    EXPECT_DOUBLE_EQ(f.roll.getFirstVisibleBeat(), before);

    EXPECT_FALSE(f.undo.canUndo());
}
