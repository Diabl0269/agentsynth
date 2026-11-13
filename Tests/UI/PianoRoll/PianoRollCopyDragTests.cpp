// PianoRoll Option+drag COPY of notes (live Option toggle, Esc cancel, the copy ghost's paint) and
// the velocity-scrub chord that moved off Option (Ctrl on macOS, Ctrl+Alt elsewhere). Every gesture
// runs through the component's real mouseDown/mouseDrag/mouseUp/keyPressed handlers. Shared
// PianoRollFixture and mouse helpers live in PianoRollTestHelpers.h.

#include "PianoRollTestHelpers.h"

namespace {

constexpr int kAlt = juce::ModifierKeys::altModifier;
constexpr int kCtrlAlt = juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::altModifier;

struct CopyBed {
    ClipId clipId;
    NoteId a, b;
};

// Two notes at beats 2 and 4 on different rows, one clip long enough to drop copies to the right.
CopyBed makeCopyBed(PianoRollFixture& f) {
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    CopyBed bed;
    bed.clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    bed.a = f.doc.addNote(bed.clipId, makeNote(2.0, 60, 1.0));
    bed.b = f.doc.addNote(bed.clipId, makeNote(4.0, 64, 1.0));
    f.open(bed.clipId);
    return bed;
}

size_t noteCount(PianoRollFixture& f, ClipId clipId) { return f.doc.getClip(clipId)->notes.size(); }

const synth::MidiNote* noteAt(PianoRollFixture& f, ClipId clipId, double startBeat, int pitch) {
    for (const auto& note : f.doc.getClip(clipId)->notes)
        if (std::abs(note.startBeat - startBeat) < 1e-9 && note.pitch == pitch)
            return &note;
    return nullptr;
}

} // namespace

TEST(PianoRollCopyDragTest, OptionDragCopiesTheNoteOneBeatRightAndSelectsTheCopy) {
    PianoRollFixture f;
    const auto bed = makeCopyBed(f);
    f.roll.getSelectionForTest().setSelection({bed.a});

    const auto anchor = centreOf(f.roll.getNoteRect(bed.a));
    const juce::Point<float> dragged(anchor.x + 40.0f, anchor.y); // +1 beat at 40 px/beat

    f.roll.mouseDown(leftClick(f.roll, anchor, kAlt));
    f.roll.mouseDrag(leftDrag(f.roll, dragged, anchor, kAlt));
    EXPECT_DOUBLE_EQ(f.doc.getNote(bed.a)->startBeat, 2.0) << "the original does not move mid-drag";
    EXPECT_EQ(noteCount(f, bed.clipId), 2u) << "and nothing is committed before the release";
    f.roll.mouseUp(leftDrag(f.roll, dragged, anchor, kAlt));

    ASSERT_EQ(noteCount(f, bed.clipId), 3u);
    EXPECT_DOUBLE_EQ(f.doc.getNote(bed.a)->startBeat, 2.0);
    const auto* copy = noteAt(f, bed.clipId, 3.0, 60);
    ASSERT_NE(copy, nullptr);
    const auto selected = f.roll.getSelectionForTest().getSelected();
    ASSERT_EQ(selected.size(), 1u);
    EXPECT_EQ(selected[0], copy->id) << "the COPY ends up selected";

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(noteCount(f, bed.clipId), 2u) << "one undo removes only the copy";
    EXPECT_DOUBLE_EQ(f.doc.getNote(bed.a)->startBeat, 2.0);
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(PianoRollCopyDragTest, OptionDragOfAnUnselectedNoteSelectsItThenCopiesIt) {
    PianoRollFixture f;
    const auto bed = makeCopyBed(f);
    f.roll.getSelectionForTest().setSelection({bed.b});

    const auto anchor = centreOf(f.roll.getNoteRect(bed.a));
    const juce::Point<float> dragged(anchor.x + 40.0f, anchor.y);
    f.roll.mouseDown(leftClick(f.roll, anchor, kAlt));
    f.roll.mouseDrag(leftDrag(f.roll, dragged, anchor, kAlt));
    f.roll.mouseUp(leftDrag(f.roll, dragged, anchor, kAlt));

    ASSERT_EQ(noteCount(f, bed.clipId), 3u) << "only the grabbed note was copied, not the old selection";
    EXPECT_NE(noteAt(f, bed.clipId, 3.0, 60), nullptr);
    EXPECT_DOUBLE_EQ(f.doc.getNote(bed.b)->startBeat, 4.0);
}

TEST(PianoRollCopyDragTest, OptionDragOfTwoSelectedNotesCopiesBothKeepingTheirSpacing) {
    PianoRollFixture f;
    const auto bed = makeCopyBed(f);
    f.roll.getSelectionForTest().setSelection({bed.a, bed.b});

    const auto anchor = centreOf(f.roll.getNoteRect(bed.a));
    const juce::Point<float> dragged(anchor.x + 120.0f, anchor.y); // +3 beats
    f.roll.mouseDown(leftClick(f.roll, anchor, kAlt));
    f.roll.mouseDrag(leftDrag(f.roll, dragged, anchor, kAlt));
    f.roll.mouseUp(leftDrag(f.roll, dragged, anchor, kAlt));

    ASSERT_EQ(noteCount(f, bed.clipId), 4u);
    const auto* copyA = noteAt(f, bed.clipId, 5.0, 60);
    const auto* copyB = noteAt(f, bed.clipId, 7.0, 64);
    ASSERT_NE(copyA, nullptr);
    ASSERT_NE(copyB, nullptr) << "the 2-beat spacing between the notes is kept";
    EXPECT_EQ(f.roll.getSelectionForTest().getSelected().size(), 2u);
    EXPECT_TRUE(f.roll.getSelectionForTest().contains(copyA->id));
    EXPECT_TRUE(f.roll.getSelectionForTest().contains(copyB->id));

    f.undo.undo();
    EXPECT_EQ(noteCount(f, bed.clipId), 2u);
    EXPECT_FALSE(f.undo.canUndo()) << "both copies were ONE undo step";
}

TEST(PianoRollCopyDragTest, ReleasingOptionMidDragTurnsTheCopyIntoAMove) {
    PianoRollFixture f;
    const auto bed = makeCopyBed(f);
    f.roll.getSelectionForTest().setSelection({bed.a});

    const auto anchor = centreOf(f.roll.getNoteRect(bed.a));
    const juce::Point<float> dragged(anchor.x + 40.0f, anchor.y);
    f.roll.mouseDown(leftClick(f.roll, anchor, kAlt));
    f.roll.mouseDrag(leftDrag(f.roll, dragged, anchor, kAlt));
    EXPECT_TRUE(f.roll.isCopyDragForTest());
    f.roll.mouseDrag(leftDrag(f.roll, dragged, anchor)); // Option released
    EXPECT_FALSE(f.roll.isCopyDragForTest());
    f.roll.mouseUp(leftDrag(f.roll, dragged, anchor));

    EXPECT_EQ(noteCount(f, bed.clipId), 2u) << "no copies";
    EXPECT_DOUBLE_EQ(f.doc.getNote(bed.a)->startBeat, 3.0) << "the original moved";
}

TEST(PianoRollCopyDragTest, PressingOptionMidPlainDragTurnsTheMoveIntoACopy) {
    PianoRollFixture f;
    const auto bed = makeCopyBed(f);
    f.roll.getSelectionForTest().setSelection({bed.a});

    const auto anchor = centreOf(f.roll.getNoteRect(bed.a));
    const juce::Point<float> dragged(anchor.x + 40.0f, anchor.y);
    f.roll.mouseDown(leftClick(f.roll, anchor));
    f.roll.mouseDrag(leftDrag(f.roll, dragged, anchor));
    EXPECT_FALSE(f.roll.isCopyDragForTest());
    f.roll.mouseDrag(leftDrag(f.roll, dragged, anchor, kAlt)); // Option pressed
    EXPECT_TRUE(f.roll.isCopyDragForTest());
    f.roll.mouseUp(leftDrag(f.roll, dragged, anchor, kAlt));

    ASSERT_EQ(noteCount(f, bed.clipId), 3u);
    EXPECT_DOUBLE_EQ(f.doc.getNote(bed.a)->startBeat, 2.0);
    EXPECT_NE(noteAt(f, bed.clipId, 3.0, 60), nullptr);
}

TEST(PianoRollCopyDragTest, OptionClickWithNoNetMovementChangesNothing) {
    PianoRollFixture f;
    const auto bed = makeCopyBed(f);
    const auto anchor = centreOf(f.roll.getNoteRect(bed.a));

    f.roll.mouseDown(leftClick(f.roll, anchor, kAlt));
    f.roll.mouseUp(leftClick(f.roll, anchor, kAlt));
    EXPECT_EQ(noteCount(f, bed.clipId), 2u);
    EXPECT_FALSE(f.undo.canUndo());

    // A drag that ends back where it began is a zero net delta too.
    const juce::Point<float> away(anchor.x + 40.0f, anchor.y);
    f.roll.mouseDown(leftClick(f.roll, anchor, kAlt));
    f.roll.mouseDrag(leftDrag(f.roll, away, anchor, kAlt));
    f.roll.mouseDrag(leftDrag(f.roll, anchor, anchor, kAlt));
    f.roll.mouseUp(leftDrag(f.roll, anchor, anchor, kAlt));
    EXPECT_EQ(noteCount(f, bed.clipId), 2u);
    EXPECT_FALSE(f.undo.canUndo()) << "no undo step for a zero-delta copy";
}

TEST(PianoRollCopyDragTest, OptionOnTheRightEdgeStillResizes) {
    PianoRollFixture f;
    const auto bed = makeCopyBed(f);
    const auto rect = f.roll.getNoteRect(bed.a);
    const juce::Point<float> edge((float)rect.getRight() - 2.0f, (float)rect.getCentreY());
    const juce::Point<float> dragged(edge.x + 40.0f, edge.y);

    f.roll.mouseDown(leftClick(f.roll, edge, kAlt));
    f.roll.mouseDrag(leftDrag(f.roll, dragged, edge, kAlt));
    f.roll.mouseUp(leftDrag(f.roll, dragged, edge, kAlt));

    EXPECT_EQ(noteCount(f, bed.clipId), 2u);
    EXPECT_DOUBLE_EQ(f.doc.getNote(bed.a)->lengthBeats, 2.0);
}

TEST(PianoRollCopyDragTest, EscMidCopyDragCommitsNothingAndLeavesNoUndoStep) {
    PianoRollFixture f;
    const auto bed = makeCopyBed(f);
    f.roll.getSelectionForTest().setSelection({bed.a});

    const auto anchor = centreOf(f.roll.getNoteRect(bed.a));
    const juce::Point<float> dragged(anchor.x + 40.0f, anchor.y);
    f.roll.mouseDown(leftClick(f.roll, anchor, kAlt));
    f.roll.mouseDrag(leftDrag(f.roll, dragged, anchor, kAlt));
    ASSERT_TRUE(f.roll.isCopyDragForTest());

    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_FALSE(f.roll.isCopyDragForTest());
    f.roll.mouseUp(leftDrag(f.roll, dragged, anchor, kAlt)); // the release that follows is a no-op

    EXPECT_EQ(noteCount(f, bed.clipId), 2u);
    EXPECT_DOUBLE_EQ(f.doc.getNote(bed.a)->startBeat, 2.0);
    EXPECT_FALSE(f.undo.canUndo());
    EXPECT_TRUE(f.roll.getSelectionForTest().contains(bed.a)) << "Esc cancelled the drag, not the selection";
}

TEST(PianoRollCopyDragTest, EscMidPlainMoveDragCancelsTheMove) {
    PianoRollFixture f;
    const auto bed = makeCopyBed(f);
    f.roll.getSelectionForTest().setSelection({bed.a});

    const auto anchor = centreOf(f.roll.getNoteRect(bed.a));
    const juce::Point<float> dragged(anchor.x + 40.0f, anchor.y);
    f.roll.mouseDown(leftClick(f.roll, anchor));
    f.roll.mouseDrag(leftDrag(f.roll, dragged, anchor));
    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    f.roll.mouseUp(leftDrag(f.roll, dragged, anchor));

    EXPECT_DOUBLE_EQ(f.doc.getNote(bed.a)->startBeat, 2.0);
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(PianoRollCopyDragTest, CopyGhostPaintsAtThePreviewWhileTheOriginalStaysPut) {
    PianoRollFixture f;
    const auto bed = makeCopyBed(f);
    f.roll.getSelectionForTest().setSelection({bed.a});

    auto render = [&f] {
        juce::Image img(juce::Image::ARGB, f.roll.getWidth(), f.roll.getHeight(), true, juce::SoftwareImageType());
        juce::Graphics g(img);
        f.roll.paintEntireComponent(g, false);
        return img;
    };

    const auto origRect = f.roll.getNoteRect(bed.a);
    const auto anchor = centreOf(origRect);
    const juce::Point<float> dragged(anchor.x + 80.0f, anchor.y); // +2 beats, onto empty grid
    const auto before = render();

    f.roll.mouseDown(leftClick(f.roll, anchor, kAlt));
    f.roll.mouseDrag(leftDrag(f.roll, dragged, anchor, kAlt));
    const auto during = render();

    const int gx = (int)dragged.x;
    const int gy = (int)dragged.y;
    EXPECT_NE(before.getPixelAt(gx, gy), during.getPixelAt(gx, gy)) << "the ghost is drawn at the preview";
    EXPECT_EQ(before.getPixelAt((int)anchor.x, (int)anchor.y), during.getPixelAt((int)anchor.x, (int)anchor.y))
        << "the original stays at full opacity where it was";
    f.roll.mouseUp(leftDrag(f.roll, dragged, anchor, kAlt));
}

TEST(PianoRollCopyDragTest, CtrlAltDragScrubsVelocityOnEveryPlatform) {
    PianoRollFixture f;
    const auto bed = makeCopyBed(f);
    f.roll.getSelectionForTest().setSelection({bed.a});
    const int before = f.doc.getNote(bed.a)->velocity;

    const auto anchor = centreOf(f.roll.getNoteRect(bed.a));
    const juce::Point<float> up(anchor.x, anchor.y - 10.0f);
    f.roll.mouseDown(leftClick(f.roll, anchor, kCtrlAlt));
    f.roll.mouseDrag(leftDrag(f.roll, up, anchor, kCtrlAlt));
    f.roll.mouseUp(leftDrag(f.roll, up, anchor, kCtrlAlt));

    EXPECT_EQ(f.doc.getNote(bed.a)->velocity, before + 10);
    EXPECT_EQ(noteCount(f, bed.clipId), 2u);
}

TEST(PianoRollCopyDragTest, OptionDragNoLongerChangesVelocity) {
    PianoRollFixture f;
    const auto bed = makeCopyBed(f);
    f.roll.getSelectionForTest().setSelection({bed.a});
    const int before = f.doc.getNote(bed.a)->velocity;

    const auto anchor = centreOf(f.roll.getNoteRect(bed.a));
    const juce::Point<float> up(anchor.x, anchor.y - 10.0f); // one row up
    f.roll.mouseDown(leftClick(f.roll, anchor, kAlt));
    f.roll.mouseDrag(leftDrag(f.roll, up, anchor, kAlt));
    f.roll.mouseUp(leftDrag(f.roll, up, anchor, kAlt));

    EXPECT_EQ(f.doc.getNote(bed.a)->velocity, before);
    EXPECT_EQ(noteCount(f, bed.clipId), 3u) << "it copied one row up instead";
    EXPECT_NE(noteAt(f, bed.clipId, 2.0, 61), nullptr);
}

// The tool cursor stays in charge until the notes have actually moved; then a plain move shows the
// grab hand and an Option copy the copy cursor, and toggling Option off mid-drag goes back to grab
// (the move continues), never to the tool cursor. Release restores the tool cursor.
TEST(PianoRollCopyDragTest, MoveShowsGrabCopyShowsCopyAndReleaseRestoresTheToolCursor) {
    PianoRollFixture f;
    const auto bed = makeCopyBed(f);
    f.roll.getSelectionForTest().setSelection({bed.a});

    const auto anchor = centreOf(f.roll.getNoteRect(bed.a));
    const juce::Point<float> dragged(anchor.x + 40.0f, anchor.y);

    f.roll.mouseDown(leftClick(f.roll, anchor));
    EXPECT_TRUE(f.roll.getMouseCursor() == juce::MouseCursor::NormalCursor) << "a press alone changes nothing";
    f.roll.mouseDrag(leftDrag(f.roll, dragged, anchor));
    EXPECT_TRUE(f.roll.getMouseCursor() == juce::MouseCursor::DraggingHandCursor);
    f.roll.mouseDrag(leftDrag(f.roll, dragged, anchor, kAlt));
    EXPECT_TRUE(f.roll.getMouseCursor() == juce::MouseCursor::CopyingCursor);
    f.roll.mouseDrag(leftDrag(f.roll, dragged, anchor));
    EXPECT_TRUE(f.roll.getMouseCursor() == juce::MouseCursor::DraggingHandCursor) << "Option off: still moving";
    f.roll.mouseUp(leftDrag(f.roll, dragged, anchor));
    EXPECT_TRUE(f.roll.getMouseCursor() == juce::MouseCursor::NormalCursor);
}

#if JUCE_MAC
// macOS only: Cmd is a separate flag there, so plain Ctrl scrubs. On Windows/Linux Ctrl IS the Cmd
// modifier (the unsnapped-move chord), which is why Ctrl+Alt exists.
TEST(PianoRollCopyDragTest, PlainCtrlDragScrubsVelocityOnMac) {
    PianoRollFixture f;
    const auto bed = makeCopyBed(f);
    f.roll.getSelectionForTest().setSelection({bed.a});
    const int before = f.doc.getNote(bed.a)->velocity;

    const auto anchor = centreOf(f.roll.getNoteRect(bed.a));
    const juce::Point<float> up(anchor.x, anchor.y - 10.0f);
    constexpr int kCtrl = juce::ModifierKeys::ctrlModifier;
    f.roll.mouseDown(leftClick(f.roll, anchor, kCtrl));
    f.roll.mouseDrag(leftDrag(f.roll, up, anchor, kCtrl));
    f.roll.mouseUp(leftDrag(f.roll, up, anchor, kCtrl));

    EXPECT_EQ(f.doc.getNote(bed.a)->velocity, before + 10);
    EXPECT_DOUBLE_EQ(f.doc.getNote(bed.a)->startBeat, 2.0);
    EXPECT_EQ(f.doc.getNote(bed.a)->pitch, 60);
}
#endif
