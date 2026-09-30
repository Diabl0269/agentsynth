// PianoRoll velocity-strip tests: the strip's layout band (bottom of the canvas, grid above it,
// hidden -> grid to the bottom), sticks lining up under note starts (also with Scale Assist open),
// and every gesture driven through the lane's real mouse handlers — stick drag (absolute on an
// unselected note, shared delta on a selection), the pen (fast-stroke interpolation, selection
// restriction, chords), the Shift ramp, the Draw tool, Escape-cancel on both key routes, the live
// recolour of the notes in the grid, and one undo step per gesture.
// Toolbar controls (value box, Humanize, chip, shortcut, persistence) are in
// PianoRollVelocityToolbarTests.cpp.

#include "PianoRollVelocityTestHelpers.h"

// ============================================================================
// Layout band
// ============================================================================

TEST(PianoRollVelocityLaneLayoutTest, TheStripIsCarvedFromTheBottomAndTheGridStopsAboveIt) {
    VelocityLaneFixture f;
    const auto& lane = f.lane();
    const int bottom = f.roll.getHeight();
    EXPECT_TRUE(lane.isVisible());
    EXPECT_EQ(lane.getBounds(),
              juce::Rectangle<int>(0, bottom - synth::ui::PianoRollVelocityLane::kDefaultHeight, f.roll.getWidth(),
                                   synth::ui::PianoRollVelocityLane::kDefaultHeight));
    EXPECT_EQ(f.roll.canvasBottom(), lane.getY());
    EXPECT_EQ(f.roll.getNoteGridBounds().getBottom(), lane.getY());
    EXPECT_EQ(f.roll.getKeysColumnBounds().getBottom(), lane.getY());
}

// The real dispatch path (what JUCE's mouse source asks), not a handler call: a press in the strip's
// band reaches the strip, and one in the header's right end reaches the value box.
TEST(PianoRollVelocityLaneLayoutTest, ClicksInTheStripAndOnTheValueBoxReachThoseChildren) {
    VelocityLaneFixture f;
    f.makeBed({100});
    f.roll.setVisible(true); // getComponentAt only answers for a visible component
    EXPECT_EQ(f.roll.getComponentAt(f.lane().getBounds().getCentre()), &f.lane());
    EXPECT_EQ(f.roll.getComponentAt(f.roll.getVelocityValueBox().getBounds().getCentre()),
              &f.roll.getVelocityValueBox());
    EXPECT_EQ(f.roll.getComponentAt(f.roll.getNoteGridBounds().getCentre()), &f.roll);
}

TEST(PianoRollVelocityLaneLayoutTest, HidingTheStripGivesTheGridTheWholeCanvasAgain) {
    VelocityLaneFixture f;
    f.roll.setVelocityLaneVisible(false);
    EXPECT_FALSE(f.lane().isVisible());
    EXPECT_EQ(f.roll.canvasBottom(), f.roll.getHeight());
    EXPECT_EQ(f.roll.getNoteGridBounds().getBottom(), f.roll.getHeight());
    EXPECT_EQ(f.roll.getKeysColumnBounds().getBottom(), f.roll.getHeight());
}

TEST(PianoRollVelocityLaneLayoutTest, TheScalePanelStopsAboveTheStripToo) {
    VelocityLaneFixture f;
    f.roll.toggleScalePanel(); // headless: lands open at once
    ASSERT_TRUE(f.roll.getScaleAssistPanel().isVisible());
    EXPECT_EQ(f.roll.getScaleAssistPanel().getBounds().getBottom(), f.lane().getY());
    EXPECT_EQ(f.lane().getWidth(), f.roll.getWidth()) << "the strip spans the full width under the panel";
}

// ============================================================================
// Sticks line up under the notes
// ============================================================================

TEST(PianoRollVelocityLaneLayoutTest, EachStickSitsExactlyUnderItsNotesStart) {
    VelocityLaneFixture f;
    const auto bed = f.makeBed({100, 100, 100});
    auto& lane = f.lane();
    for (const auto id : bed.notes) {
        const int noteX = f.roll.getNoteRect(id).getX();
        lane.mouseMove(hover(lane, {(float)(noteX - lane.getX()), lane.yForVelocity(100)}));
        EXPECT_EQ(lane.getReadoutNote(), id) << "hovering the note's start x finds its stick";
    }
    lane.mouseMove(hover(lane, {(float)(f.roll.getNoteRect(bed.notes[0]).getX() + 20), lane.yForVelocity(100)}));
    EXPECT_FALSE(lane.getReadoutNote().isValid()) << "20 px from any stick is empty strip";
}

TEST(PianoRollVelocityLaneLayoutTest, SticksFollowTheGridWhenTheScalePanelShiftsIt) {
    VelocityLaneFixture f;
    const auto bed = f.makeBed({100});
    f.roll.toggleScalePanel();
    auto& lane = f.lane();
    const int noteX = f.roll.getNoteRect(bed.notes[0]).getX();
    ASSERT_GT(noteX, PianoRollComponent::kScalePanelWidth) << "the panel pushed the grid right";
    lane.mouseMove(hover(lane, {(float)noteX, lane.yForVelocity(100)}));
    EXPECT_EQ(lane.getReadoutNote(), bed.notes[0]);
}

TEST(PianoRollVelocityLaneLayoutTest, APaintedStickIsDrawnInItsColumnAndNotBesideIt) {
    VelocityLaneFixture f;
    const auto bed = f.makeBed({127});
    auto& lane = f.lane();
    juce::Image image(juce::Image::ARGB, lane.getWidth(), lane.getHeight(), true, juce::SoftwareImageType());
    {
        juce::Graphics g(image);
        lane.paintEntireComponent(g, false);
    }
    const int x = f.roll.getNoteRect(bed.notes[0]).getX() - lane.getX();
    const int y = lane.getHeight() / 2;
    EXPECT_NE(image.getPixelAt(x, y), image.getPixelAt(x + 12, y)) << "the stick's column differs from empty strip";
    EXPECT_EQ(image.getPixelAt(x + 12, y), image.getPixelAt(x + 24, y));
}

// ============================================================================
// Stick drag
// ============================================================================

TEST(PianoRollVelocityLaneGestureTest, DraggingAnUnselectedStickSetsThatNoteAloneFromThePointer) {
    VelocityLaneFixture f;
    const auto bed = f.makeBed({100, 100});
    auto& lane = f.lane();
    const auto press = f.at(1.0, 100);
    lane.mouseDown(leftClick(lane, press));
    EXPECT_EQ(lane.getGesture(), synth::ui::PianoRollVelocityLane::Gesture::Stick);
    lane.mouseDrag(leftDrag(lane, f.at(1.0, 40), press));
    lane.mouseUp(leftDrag(lane, f.at(1.0, 40), press));

    EXPECT_EQ(f.velocityOf(bed.notes[0]), 40);
    EXPECT_EQ(f.velocityOf(bed.notes[1]), 100) << "only the grabbed note";
    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.velocityOf(bed.notes[0]), 100);
    EXPECT_FALSE(f.undo.canUndo()) << "one gesture, one undo step";
}

TEST(PianoRollVelocityLaneGestureTest, DraggingASelectedStickMovesTheWholeSelectionByOneDeltaClamped) {
    VelocityLaneFixture f;
    const auto bed = f.makeBed({100, 60, 120, 80});
    f.roll.getSelectionForTest().setSelection({bed.notes[0], bed.notes[1], bed.notes[2]});
    auto& lane = f.lane();
    const auto press = f.at(1.0, 100);
    lane.mouseDown(leftClick(lane, press));
    EXPECT_EQ(f.velocityOf(bed.notes[0]), 100) << "a press on a selected stick jumps nothing";
    lane.mouseDrag(leftDrag(lane, f.at(1.0, 110), press));
    lane.mouseUp(leftDrag(lane, f.at(1.0, 110), press));

    EXPECT_EQ(f.velocityOf(bed.notes[0]), 110);
    EXPECT_EQ(f.velocityOf(bed.notes[1]), 70);
    EXPECT_EQ(f.velocityOf(bed.notes[2]), 127) << "clamped on its own, the others still moved by +10";
    EXPECT_EQ(f.velocityOf(bed.notes[3]), 80) << "unselected notes are untouched";
    f.undo.undo();
    EXPECT_EQ(f.velocityOf(bed.notes[0]), 100);
    EXPECT_EQ(f.velocityOf(bed.notes[1]), 60);
    EXPECT_EQ(f.velocityOf(bed.notes[2]), 120);
    EXPECT_FALSE(f.undo.canUndo());
}

// ============================================================================
// Pen
// ============================================================================

TEST(PianoRollVelocityLaneGestureTest, AFastPenStrokeSetsEveryStickItCrossesInOneEvent) {
    VelocityLaneFixture f;
    const auto bed = f.makeBed({100, 100, 100, 100});
    auto& lane = f.lane();
    const auto press = f.at(0.5, 30);
    lane.mouseDown(leftClick(lane, press));
    EXPECT_EQ(lane.getGesture(), synth::ui::PianoRollVelocityLane::Gesture::Pen) << "a press on empty strip";
    // ONE drag event jumping four beats (160 px): no stick in between may be skipped.
    lane.mouseDrag(leftDrag(lane, f.at(4.5, 30), press));
    lane.mouseUp(leftDrag(lane, f.at(4.5, 30), press));
    for (const auto id : bed.notes)
        EXPECT_EQ(f.velocityOf(id), 30);
    f.undo.undo();
    for (const auto id : bed.notes)
        EXPECT_EQ(f.velocityOf(id), 100);
    EXPECT_FALSE(f.undo.canUndo()) << "the whole stroke is one undo step";
}

TEST(PianoRollVelocityLaneGestureTest, APenStrokeInterpolatesBetweenMouseEvents) {
    VelocityLaneFixture f;
    const auto bed = f.makeBed({100, 100, 100, 100});
    auto& lane = f.lane();
    const auto press = f.at(0.5, 10);
    lane.mouseDown(leftClick(lane, press));
    lane.mouseDrag(leftDrag(lane, f.at(4.5, 90), press));
    lane.mouseUp(leftDrag(lane, f.at(4.5, 90), press));
    // Beats 1..4 on a line from (0.5, 10) to (4.5, 90): 20 velocity units per beat.
    EXPECT_NEAR(f.velocityOf(bed.notes[0]), 20, 1);
    EXPECT_NEAR(f.velocityOf(bed.notes[1]), 40, 1);
    EXPECT_NEAR(f.velocityOf(bed.notes[2]), 60, 1);
    EXPECT_NEAR(f.velocityOf(bed.notes[3]), 80, 1);
}

TEST(PianoRollVelocityLaneGestureTest, WithASelectionThePenTouchesOnlySelectedNotes) {
    VelocityLaneFixture f;
    const auto bed = f.makeBed({100, 100, 100, 100});
    f.roll.getSelectionForTest().setSelection({bed.notes[1], bed.notes[2]});
    auto& lane = f.lane();
    const auto press = f.at(0.5, 30);
    lane.mouseDown(leftClick(lane, press));
    lane.mouseDrag(leftDrag(lane, f.at(4.5, 30), press));
    lane.mouseUp(leftDrag(lane, f.at(4.5, 30), press));
    EXPECT_EQ(f.velocityOf(bed.notes[0]), 100);
    EXPECT_EQ(f.velocityOf(bed.notes[1]), 30);
    EXPECT_EQ(f.velocityOf(bed.notes[2]), 30);
    EXPECT_EQ(f.velocityOf(bed.notes[3]), 100);
}

TEST(PianoRollVelocityLaneGestureTest, ChordNotesSharingAStartAllTakeThePensValue) {
    VelocityLaneFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    const auto a = f.doc.addNote(clipId, makeNote(2.0, 60, 1.0));
    const auto b = f.doc.addNote(clipId, makeNote(2.0, 64, 1.0));
    const auto c = f.doc.addNote(clipId, makeNote(2.0, 67, 1.0));
    f.open(clipId);
    auto& lane = f.lane();
    const auto press = f.at(1.5, 45);
    lane.mouseDown(leftClick(lane, press));
    lane.mouseDrag(leftDrag(lane, f.at(2.5, 45), press));
    lane.mouseUp(leftDrag(lane, f.at(2.5, 45), press));
    EXPECT_EQ(f.velocityOf(a), 45);
    EXPECT_EQ(f.velocityOf(b), 45);
    EXPECT_EQ(f.velocityOf(c), 45);
}

// ============================================================================
// Shift ramp and the Draw tool
// ============================================================================

TEST(PianoRollVelocityLaneGestureTest, ShiftDragDrawsAStraightRampAndShrinkingItRestoresWhatItLeft) {
    VelocityLaneFixture f;
    const auto bed = f.makeBed({100, 100, 100, 100});
    auto& lane = f.lane();
    const auto anchor = f.at(0.5, 10);
    lane.mouseDown(leftClick(lane, anchor, juce::ModifierKeys::shiftModifier));
    EXPECT_EQ(lane.getGesture(), synth::ui::PianoRollVelocityLane::Gesture::Ramp);
    lane.mouseDrag(leftDrag(lane, f.at(4.5, 90), anchor, juce::ModifierKeys::shiftModifier));
    // Pull back to beat 2.5 at velocity 50: the ramp now spans only the first two notes.
    const auto end = f.at(2.5, 50);
    lane.mouseDrag(leftDrag(lane, end, anchor, juce::ModifierKeys::shiftModifier));
    lane.mouseUp(leftDrag(lane, end, anchor, juce::ModifierKeys::shiftModifier));
    // Line from (0.5, 10) to (2.5, 50): 20 per beat.
    EXPECT_NEAR(f.velocityOf(bed.notes[0]), 20, 1);
    EXPECT_NEAR(f.velocityOf(bed.notes[1]), 40, 1);
    EXPECT_EQ(f.velocityOf(bed.notes[2]), 100) << "outside the final ramp: back to its own value";
    EXPECT_EQ(f.velocityOf(bed.notes[3]), 100);
    f.undo.undo();
    EXPECT_EQ(f.velocityOf(bed.notes[0]), 100);
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(PianoRollVelocityLaneGestureTest, WithTheDrawToolAPressOnAStickIsAPenStroke) {
    VelocityLaneFixture f;
    const auto bed = f.makeBed({100, 100});
    // Selected, so a Select-tool press on its stick would be a RELATIVE drag that changes nothing
    // until the pointer moves; the pen sets the pointer's value on the press itself.
    f.roll.getSelectionForTest().setSelection({bed.notes[0]});
    f.roll.setActiveTool(EditTool::Draw);
    auto& lane = f.lane();
    const auto press = f.at(1.0, 50);
    lane.mouseDown(leftClick(lane, press));
    EXPECT_EQ(lane.getGesture(), synth::ui::PianoRollVelocityLane::Gesture::Pen);
    lane.mouseUp(leftClick(lane, press));
    EXPECT_EQ(f.velocityOf(bed.notes[0]), 50);
}

// ============================================================================
// Escape, live preview
// ============================================================================

TEST(PianoRollVelocityLaneGestureTest, EscapeOnTheStripCancelsTheDragWithNothingCommitted) {
    VelocityLaneFixture f;
    const auto bed = f.makeBed({100, 100});
    auto& lane = f.lane();
    const auto press = f.at(0.5, 30);
    lane.mouseDown(leftClick(lane, press));
    lane.mouseDrag(leftDrag(lane, f.at(2.5, 30), press));
    EXPECT_TRUE(lane.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_FALSE(lane.isGestureActive());
    lane.mouseDrag(leftDrag(lane, f.at(2.5, 10), press)); // the rest of the drag does nothing
    lane.mouseUp(leftDrag(lane, f.at(2.5, 10), press));
    EXPECT_EQ(f.velocityOf(bed.notes[0]), 100);
    EXPECT_EQ(f.velocityOf(bed.notes[1]), 100);
    EXPECT_FALSE(f.undo.canUndo());
    EXPECT_FALSE(lane.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey))) << "idle: Escape falls through";
}

TEST(PianoRollVelocityLaneGestureTest, EscapeReachingTheRollCancelsTheStripDragBeforeClearingTheSelection) {
    VelocityLaneFixture f;
    const auto bed = f.makeBed({100, 100});
    f.roll.getSelectionForTest().setSelection({bed.notes[0], bed.notes[1]});
    auto& lane = f.lane();
    const auto press = f.at(0.5, 30);
    lane.mouseDown(leftClick(lane, press));
    lane.mouseDrag(leftDrag(lane, f.at(2.5, 30), press));
    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_EQ(f.roll.getSelectionForTest().size(), 2) << "the first Escape only cancelled the drag";
    lane.mouseUp(leftDrag(lane, f.at(2.5, 30), press));
    EXPECT_EQ(f.velocityOf(bed.notes[0]), 100);
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(PianoRollVelocityLaneGestureTest, NotesInTheGridRecolourLiveWhileTheirStickIsDragged) {
    VelocityLaneFixture f;
    const auto bed = f.makeBed({127});
    auto& lane = f.lane();
    const auto noteCentre = f.roll.getNoteRect(bed.notes[0]).getCentre();
    const auto pixelAtNote = [&] {
        juce::Image image(juce::Image::ARGB, f.roll.getWidth(), f.roll.getHeight(), true, juce::SoftwareImageType());
        juce::Graphics g(image);
        f.roll.paintEntireComponent(g, false);
        return image.getPixelAt(noteCentre.x, noteCentre.y);
    };
    const auto before = pixelAtNote();
    const auto press = f.at(1.0, 127);
    lane.mouseDown(leftClick(lane, press));
    lane.mouseDrag(leftDrag(lane, f.at(1.0, 5), press));
    EXPECT_EQ(f.velocityOf(bed.notes[0]), 127) << "nothing is written mid-drag";
    const auto during = pixelAtNote();
    EXPECT_NE(before, during) << "the note is painted at its previewed velocity";
    lane.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    EXPECT_EQ(pixelAtNote(), before) << "a cancel clears the preview";
}
