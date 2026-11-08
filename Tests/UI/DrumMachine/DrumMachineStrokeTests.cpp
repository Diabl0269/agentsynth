// The drum machine's paint stroke: pressing an empty cell and dragging adds a hit to every cell passed, the
// same stroke started on a hit removes the hits it passes, the first cell decides, and the whole stroke is one undo
// step.

#include "../PianoRoll/PianoRollTestHelpers.h"
#include "DrumDocFollower.h"
#include "Timeline/TimelineSnapshot.h"
#include "UI/DrumMachine/DrumMachineComponent.h"
#include "UI/Layout/FadeVisibility.h"

using synth::ui::DrumMachineComponent;

namespace {

struct StrokeFixture {
    TimelineDoc doc;
    AppUndoManager undo;
    DrumMachineComponent grid;
    ClipId clip;
    std::vector<std::pair<int, bool>> auditions;

    StrokeFixture() {
        synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
        const auto track = doc.addTrack(TrackKind::Midi, "Drums");
        clip = doc.addClip(track, 0.0, 8.0, "Beat 1");
        grid.setTimelineDoc(&doc);
        grid.setUndoManager(&undo);
        grid.setSize(1000, 320);
        grid.onAuditionNote = [this](int pitch, float, bool on) { auditions.emplace_back(pitch, on); };
        grid.openClip(clip);
    }
    juce::Point<float> centre(int pitch, int step) {
        const int row = grid.rowOfPitch(pitch);
        EXPECT_GE(row, 0);
        return centreOf(grid.cellBounds(row, step));
    }
    // Presses on (pitch, step) and drags through the listed cells, sample by sample.
    void press(int pitch, int step, int mods = 0) { grid.mouseDown(leftClick(grid, centre(pitch, step), mods)); }
    void dragTo(int pitch, int step) { grid.mouseDrag(leftDrag(grid, centre(pitch, step), centre(pitch, 0))); }
    void release(int pitch, int step) { grid.mouseUp(leftDrag(grid, centre(pitch, step), centre(pitch, 0))); }
    void addHit(int pitch, int step) { doc.addNote(clip, makeNote(step * 0.25, pitch, 0.25)); }
    void settle() {
        grid.refreshFromDoc();
        undo.clearUndoHistory();
    }
    const synth::Clip& c() { return *doc.getClip(clip); }
    int countAt(int pitch) {
        int n = 0;
        for (const auto& note : c().notes)
            n += note.pitch == pitch ? 1 : 0;
        return n;
    }
};

} // namespace

TEST(DrumMachineStrokeTest, DraggingFromAnEmptyCellAddsEveryCellPassedAsOneUndoStep) {
    StrokeFixture f;
    f.press(42, 0);
    for (int step = 1; step < 8; ++step)
        f.dragTo(42, step);
    EXPECT_TRUE(f.c().notes.empty()) << "nothing is written while the stroke is live";
    EXPECT_GT(f.grid.getCellVelocity(f.grid.rowOfPitch(42), 5), 0) << "but the passed cells preview their hits";
    f.release(42, 7);
    ASSERT_EQ(f.c().notes.size(), 8u);
    for (int step = 0; step < 8; ++step)
        EXPECT_EQ(f.grid.getCellVelocity(f.grid.rowOfPitch(42), step), 100);
    f.undo.undo();
    EXPECT_TRUE(f.c().notes.empty()) << "Cmd+Z removes the whole stroke";
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(DrumMachineStrokeTest, AFastDragThatJumpsManyCellsInOneMoveSkipsNone) {
    StrokeFixture f;
    f.press(42, 0);
    f.dragTo(42, 15); // one pointer sample, fifteen cells away
    f.release(42, 15);
    EXPECT_EQ(f.countAt(42), 16);
}

TEST(DrumMachineStrokeTest, DraggingFromAHitRemovesOnlyTheHitsPassedAndNeverAddsToEmptyCells) {
    StrokeFixture f;
    for (int step : {0, 1, 2, 4, 6})
        f.addHit(42, step);
    f.settle();
    f.press(42, 1);
    f.dragTo(42, 4);
    f.release(42, 4);
    EXPECT_EQ(f.countAt(42), 2) << "steps 1, 2 and 4 went; 0 and 6 stay; the empty cell 3 was not filled";
    std::vector<int> steps;
    for (const auto& n : f.c().notes)
        steps.push_back(static_cast<int>(std::lround(n.startBeat / 0.25)));
    std::sort(steps.begin(), steps.end());
    EXPECT_EQ(steps, (std::vector<int>{0, 6}));
    f.undo.undo();
    EXPECT_EQ(f.countAt(42), 5) << "one undo step for the whole stroke";
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(DrumMachineStrokeTest, AnAddStrokeLeavesTheHitsItPassesOverUntouched) {
    StrokeFixture f;
    f.addHit(42, 2);
    f.doc.setNoteVelocity(f.c().notes[0].id, 60);
    f.settle();
    f.press(42, 0);
    f.dragTo(42, 4);
    f.release(42, 4);
    EXPECT_EQ(f.countAt(42), 5) << "the first cell was empty, so the stroke only adds";
    for (const auto& n : f.c().notes)
        if (std::abs(n.startBeat - 0.5) < 1.0e-9)
            EXPECT_EQ(n.velocity, 60) << "the existing hit keeps its loudness";
}

TEST(DrumMachineStrokeTest, TheStrokeFollowsThePointerToAnyRow) {
    StrokeFixture f;
    f.press(42, 0);
    f.grid.mouseDrag(leftDrag(f.grid, f.centre(38, 2), f.centre(42, 0)));
    f.grid.mouseDrag(leftDrag(f.grid, f.centre(36, 4), f.centre(42, 0)));
    f.grid.mouseUp(leftDrag(f.grid, f.centre(36, 4), f.centre(42, 0)));
    EXPECT_GE(f.countAt(42), 1);
    EXPECT_GE(f.countAt(38), 1);
    EXPECT_GE(f.countAt(36), 1);
    f.undo.undo();
    EXPECT_TRUE(f.c().notes.empty()) << "rows crossed on the way are part of the same one undo step";
}

TEST(DrumMachineStrokeTest, DraggingUpOrDownOnAHitStillSetsItsLoudnessAndSidewaysDoesNot) {
    StrokeFixture f;
    f.addHit(36, 0);
    f.addHit(36, 1);
    f.settle();
    const auto start = f.centre(36, 0);
    f.grid.mouseDown(leftClick(f.grid, start));
    f.grid.mouseDrag(leftDrag(f.grid, start + juce::Point<float>(1.0f, -20.0f), start));
    f.grid.mouseUp(leftDrag(f.grid, start + juce::Point<float>(1.0f, -20.0f), start));
    ASSERT_EQ(f.c().notes.size(), 2u) << "a vertical drag never removes";
    EXPECT_GT(f.grid.getCellVelocity(f.grid.rowOfPitch(36), 0), 100);
    EXPECT_EQ(f.grid.getCellVelocity(f.grid.rowOfPitch(36), 1), 100) << "the neighbour is untouched";
}

TEST(DrumMachineStrokeTest, ASmallWobbleOnAHitIsStillAClickThatRemovesIt) {
    StrokeFixture f;
    f.addHit(38, 3);
    f.settle();
    const auto start = f.centre(38, 3);
    f.grid.mouseDown(leftClick(f.grid, start));
    f.grid.mouseDrag(leftDrag(f.grid, start + juce::Point<float>(1.0f, 1.0f), start));
    f.grid.mouseUp(leftDrag(f.grid, start + juce::Point<float>(1.0f, 1.0f), start));
    EXPECT_TRUE(f.c().notes.empty());
}

TEST(DrumMachineStrokeTest, EscapeDuringAStrokeWritesNothing) {
    StrokeFixture f;
    f.press(42, 0);
    f.dragTo(42, 5);
    EXPECT_TRUE(f.grid.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    f.release(42, 5);
    EXPECT_TRUE(f.c().notes.empty());
    EXPECT_FALSE(f.undo.canUndo());
    EXPECT_EQ(f.grid.getCellVelocity(f.grid.rowOfPitch(42), 3), 0) << "the preview is gone";
}

TEST(DrumMachineStrokeTest, AStrokeAuditionsAtMostOneHitEveryFortyMilliseconds) {
    StrokeFixture f;
    f.press(42, 0);
    for (int step = 1; step < 16; ++step)
        f.dragTo(42, step);
    f.release(42, 15);
    ASSERT_EQ(f.countAt(42), 16);
    EXPECT_GE(f.auditions.size(), 2u) << "the first hit sounds";
    EXPECT_LT(f.auditions.size(), 2u * 16u) << "a burst of sixteen cells in a few milliseconds is throttled";
}

TEST(DrumMachineStrokeTest, ShiftRightCarriesAHitAndShiftLeftOverAnEmptyCellErases) {
    StrokeFixture f;
    f.addHit(38, 2);
    f.settle();
    f.grid.setFocusCell({f.grid.rowOfPitch(38), 2});
    const auto shiftRight = juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys::shiftModifier, 0);
    EXPECT_TRUE(f.grid.keyPressed(shiftRight));
    EXPECT_TRUE(f.grid.keyPressed(shiftRight));
    EXPECT_EQ(f.countAt(38), 3) << "the hit is carried onto steps 3 and 4";
    EXPECT_EQ(f.grid.getFocusCell().step, 4);
    f.grid.setFocusCell({f.grid.rowOfPitch(38), 6});
    const auto shiftLeft = juce::KeyPress(juce::KeyPress::leftKey, juce::ModifierKeys::shiftModifier, 0);
    EXPECT_TRUE(f.grid.keyPressed(shiftLeft)); // from empty step 6 onto empty step 5: nothing to erase
    EXPECT_TRUE(f.grid.keyPressed(shiftLeft)); // onto step 4: erases the hit
    EXPECT_EQ(f.countAt(38), 2);
}

// what the drag shows is what lands. The document change reaches the grid from inside the write, so a hit the
// stroke previewed must not start its grow-in again there.
TEST(DrumMachineStrokeTest, DroppingAPaintStrokeKeepsEveryHitAtFullSizeWithNoSecondGrowIn) {
    StrokeFixture f;
    drum_test::DocFollower follow(f.doc, f.grid);
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(true);
    f.press(42, 0);
    for (int step = 1; step <= 5; ++step)
        f.dragTo(42, step);
    f.grid.ageHitMotionsForTest(500.0); // the previewed grow-ins have finished by the time the button is let go
    f.release(42, 5);
    ASSERT_EQ(f.countAt(42), 6);
    const int row = f.grid.rowOfPitch(42);
    for (int step = 0; step <= 5; ++step)
        EXPECT_FLOAT_EQ(f.grid.hitScaleForTest(row, step), 1.0f) << "step " << step << " pops in again on release";
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
}

TEST(DrumMachineStrokeTest, DroppingAnEraseStrokeKeepsTheErasedHitsGoneWithNoSecondShrink) {
    StrokeFixture f;
    for (int step = 0; step < 6; ++step)
        f.addHit(42, step);
    f.settle();
    drum_test::DocFollower follow(f.doc, f.grid);
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(true);
    f.press(42, 0);
    for (int step = 1; step <= 5; ++step)
        f.dragTo(42, step);
    f.grid.ageHitMotionsForTest(500.0);
    f.release(42, 5);
    ASSERT_EQ(f.countAt(42), 0);
    const int row = f.grid.rowOfPitch(42);
    for (int step = 0; step <= 5; ++step)
        EXPECT_FLOAT_EQ(f.grid.hitScaleForTest(row, step), 0.0f) << "step " << step << " flashes back on release";
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
}
