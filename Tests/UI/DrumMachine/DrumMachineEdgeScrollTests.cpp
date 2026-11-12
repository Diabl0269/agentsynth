// A drag in the drum machine follows its view: a paint stroke, a drum's end edge and a ramp across a lane under the
// grid, held at the edge, scroll the grid sideways (a stroke at the top or bottom also scrolls the rows), and each
// tick reaches what scrolled in under the still pointer; release and Esc stop it, and each drag stays one undo step.

#include "../PianoRoll/PianoRollTestHelpers.h"
#include "DrumMachineGrooveFixture.h"
#include "UI/DrumMachine/DrumMachineComponent.h"

using drumtest::GrooveFixture;
using drumtest::inLane;
using synth::ui::DrumLaneKind;

namespace {

constexpr double kZoomedIn = 200.0; // px per beat: about four of the clip's eight beats show

struct EdgeFixture : GrooveFixture {
    EdgeFixture() { grid.setHorizontalView(kZoomedIn, 0.0); }

    juce::Point<float> rightEdgeOfRow(int pitch) {
        const auto cell = grid.cellBounds(grid.rowOfPitch(pitch), 0);
        return {static_cast<float>(grid.gridRegion().getRight() - 2), static_cast<float>(cell.getCentreY())};
    }
    void ticks(int n) {
        for (int i = 0; i < n; ++i)
            grid.tickEdgeScrollForTest();
    }
    int countAt(int pitch) {
        int n = 0;
        for (const auto& note : c().notes)
            n += note.pitch == pitch ? 1 : 0;
        return n;
    }
    int lastVisibleStep() const {
        const double beats = static_cast<double>(grid.gridRegion().getWidth()) / kZoomedIn;
        return static_cast<int>(beats / 0.25);
    }
};

void selectRow(GrooveFixture& f, int pitch) { f.grid.getRowLabels().getNameButton(pitch)->onClick(); }

} // namespace

TEST(DrumMachineEdgeScroll, AStrokeHeldAtTheRightEdgeScrollsTheGridAndPaintsTheCellsThatScrollIn) {
    EdgeFixture f;
    const int shown = f.lastVisibleStep();
    ASSERT_LT(shown, 31) << "the clip must run past the view for the test to mean anything";
    const auto press = centreOf(f.grid.cellBounds(f.grid.rowOfPitch(42), 0));
    const auto edge = f.rightEdgeOfRow(42);
    f.grid.mouseDown(leftClick(f.grid, press));
    f.grid.mouseDrag(leftDrag(f.grid, edge, press));
    ASSERT_TRUE(f.grid.isEdgeScrollRunningForTest());
    const int before = f.countAt(42);

    f.ticks(60);
    EXPECT_GT(f.grid.getViewState().firstVisibleBeat, 0.0) << "the grid scrolled right";
    EXPECT_GT(f.grid.getCellVelocity(f.grid.rowOfPitch(42), 31), 0) << "the last cell scrolled in and took a hit";
    EXPECT_EQ(f.countAt(42), before) << "nothing is written while the stroke is live";

    f.grid.mouseUp(leftDrag(f.grid, edge, press));
    EXPECT_FALSE(f.grid.isEdgeScrollRunningForTest()) << "release stops it";
    EXPECT_EQ(f.countAt(42), 32) << "every cell the stroke passed, the scrolled-in ones too";
    f.undo.undo();
    EXPECT_EQ(f.countAt(42), 0);
    EXPECT_FALSE(f.undo.canUndo()) << "the whole stroke is one undo step";
}

TEST(DrumMachineEdgeScroll, EscapeStopsAHeldStrokeAndWritesNothing) {
    EdgeFixture f;
    const auto press = centreOf(f.grid.cellBounds(f.grid.rowOfPitch(42), 0));
    const auto edge = f.rightEdgeOfRow(42);
    f.grid.mouseDown(leftClick(f.grid, press));
    f.grid.mouseDrag(leftDrag(f.grid, edge, press));
    f.ticks(10);
    EXPECT_TRUE(f.grid.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_FALSE(f.grid.isEdgeScrollRunningForTest());
    f.grid.mouseUp(leftDrag(f.grid, edge, press));
    EXPECT_EQ(f.countAt(42), 0);
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(DrumMachineEdgeScroll, MovingBackInStopsTheScrollAndADeeperPointerScrollsFaster) {
    EdgeFixture shallow, deep;
    for (auto* f : {&shallow, &deep}) {
        const auto press = centreOf(f->grid.cellBounds(f->grid.rowOfPitch(42), 0));
        auto at = f->rightEdgeOfRow(42);
        if (f == &shallow)
            at.x -= 18.0f;
        f->grid.mouseDown(leftClick(f->grid, press));
        f->grid.mouseDrag(leftDrag(f->grid, at, press));
        f->ticks(5);
    }
    EXPECT_GT(deep.grid.getViewState().firstVisibleBeat, shallow.grid.getViewState().firstVisibleBeat);
    const auto press = centreOf(deep.grid.cellBounds(deep.grid.rowOfPitch(42), 0));
    const auto middle = centreOf(deep.grid.cellBounds(deep.grid.rowOfPitch(42), 2));
    deep.grid.mouseDrag(leftDrag(deep.grid, middle, press));
    EXPECT_FALSE(deep.grid.isEdgeScrollRunningForTest());
    deep.grid.mouseUp(leftDrag(deep.grid, middle, press));
    shallow.grid.mouseUp(leftDrag(shallow.grid, press, press));
}

TEST(DrumMachineEdgeScroll, AStrokeHeldAtTheBottomScrollsTheRows) {
    GrooveFixture f(220);
    f.grid.setHorizontalView(kZoomedIn, 0.0);
    const auto grid = f.grid.gridRegion();
    const int row = 0;
    const auto press = centreOf(f.grid.cellBounds(row, 1));
    const juce::Point<float> bottom{press.x, static_cast<float>(grid.getBottom() - 2)};
    f.grid.mouseDown(leftClick(f.grid, press));
    f.grid.mouseDrag(leftDrag(f.grid, bottom, press));
    for (int i = 0; i < 40; ++i)
        f.grid.tickEdgeScrollForTest();
    EXPECT_GT(f.grid.getScrollYForTest(), 0.0) << "the rows below scrolled up into view";
    f.grid.mouseUp(leftDrag(f.grid, bottom, press));
    EXPECT_FALSE(f.grid.isEdgeScrollRunningForTest());
}

TEST(DrumMachineEdgeScroll, ADrumsEndEdgeHeldAtTheRightEdgeScrollsAndTheLengthFollows) {
    EdgeFixture f;
    f.doc.setClipRowLength(f.clip, 42, 4);
    f.grid.refreshFromDoc();
    f.undo.clearUndoHistory();
    const int row = f.grid.rowOfPitch(42);
    const auto handle = centreOf(f.grid.edgeHandleBounds(row));
    const auto edge = f.rightEdgeOfRow(42);
    f.grid.mouseDown(leftClick(f.grid, handle));
    f.grid.mouseDrag(leftDrag(f.grid, edge, handle));
    ASSERT_TRUE(f.grid.isEdgeScrollRunningForTest());
    const int shown = f.lastVisibleStep();
    f.ticks(15);
    EXPECT_GT(f.grid.getViewState().firstVisibleBeat, 0.0);
    f.grid.mouseUp(leftDrag(f.grid, edge, handle));
    const int length = f.grid.getRowLength(42);
    EXPECT_GT(length, shown) << "the end followed the scrolled view past what showed at the press";
    const double edgeBeat = f.grid.getViewState().xToBeat(edge.x - f.grid.gridRegion().getX());
    EXPECT_NEAR(length, edgeBeat / 0.25, 1.0) << "and stayed under the pointer";
    f.undo.undo();
    EXPECT_EQ(f.grid.getRowLength(42), 4);
    EXPECT_FALSE(f.undo.canUndo()) << "the whole drag is one undo step";
}

TEST(DrumMachineEdgeScroll, ALaneRampHeldAtTheRightEdgeScrollsTheGridAndSetsTheHitsThatScrollIn) {
    EdgeFixture f;
    f.addHits(42, {0, 4, 8, 12, 16, 20, 24, 28}, 50);
    selectRow(f, 42);
    f.showLane(DrumLaneKind::Loudness);
    f.grid.setHorizontalView(kZoomedIn, 0.0);
    auto& lane = f.grid.getLane(DrumLaneKind::Loudness);
    ASSERT_EQ(lane.getHits().size(), 8u);
    const auto press = inLane(lane, 0, 0.9f);
    const juce::Point<float> edge{static_cast<float>(lane.getWidth() - 2), press.y};
    lane.mouseDown(leftClick(lane, press));
    lane.mouseDrag(leftDrag(lane, edge, press));
    ASSERT_TRUE(f.grid.isEdgeScrollRunningForTest());

    f.ticks(60);
    EXPECT_GT(f.grid.getViewState().firstVisibleBeat, 0.0);
    EXPECT_GT(lane.valueOf(7), 100) << "the last hit scrolled in under the pointer and took its height";
    lane.mouseUp(leftDrag(lane, edge, press));
    EXPECT_FALSE(f.grid.isEdgeScrollRunningForTest());
    for (const int v : f.velocities(42))
        EXPECT_GT(v, 100) << "every hit the ramp crossed, the scrolled-in ones too";
    f.undo.undo();
    for (const int v : f.velocities(42))
        EXPECT_EQ(v, 50);
    EXPECT_FALSE(f.undo.canUndo()) << "the whole ramp is one undo step";
}

TEST(DrumMachineEdgeScroll, EscapeInALaneStopsItsRampWithoutClosingTheDrumMachine) {
    EdgeFixture f;
    f.addHits(42, {0, 4, 8, 12, 16, 20, 24, 28}, 50);
    selectRow(f, 42);
    f.showLane(DrumLaneKind::Loudness);
    f.grid.setHorizontalView(kZoomedIn, 0.0);
    bool closed = false;
    f.grid.onCloseRequested = [&] { closed = true; };
    auto& lane = f.grid.getLane(DrumLaneKind::Loudness);
    const auto press = inLane(lane, 0, 0.9f);
    const juce::Point<float> edge{static_cast<float>(lane.getWidth() - 2), press.y};
    lane.mouseDown(leftClick(lane, press));
    lane.mouseDrag(leftDrag(lane, edge, press));
    f.ticks(10);
    EXPECT_TRUE(lane.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_FALSE(f.grid.isEdgeScrollRunningForTest());
    EXPECT_FALSE(closed) << "Esc ends the ramp; only a second one closes";
    lane.mouseUp(leftDrag(lane, edge, press));
    for (const int v : f.velocities(42))
        EXPECT_EQ(v, 50);
    EXPECT_FALSE(f.undo.canUndo());
}
