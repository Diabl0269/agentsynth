// A drag in the drum machine follows its view: a paint stroke, a drum's end edge and a ramp across a lane under the
// grid, held at the edge, scroll the grid sideways (a stroke at the top or bottom also scrolls the rows), and each
// tick reaches what scrolled in under the still pointer; release and Esc stop it, and each drag stays one undo step.

#include "../Layout/ViewportDragFollow.h"
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

    // A stroke pressed with the pointer already inside the right edge zone stays put until the pointer moves toward it.
    EdgeFixture still;
    auto inZone = still.rightEdgeOfRow(42);
    inZone.x -= 10.0f;
    const auto from = inZone; // a stroke can start on any cell, so the press itself lands in the zone
    viewport_drag_follow::expectPressInZoneHoldsStill(
        {[&] {
             still.grid.mouseDown(leftClick(still.grid, from));
             still.grid.mouseDrag(leftDrag(still.grid, inZone, from));
         },
         [&] { still.ticks(1); },
         [&] { still.grid.mouseDrag(leftDrag(still.grid, {inZone.x + 6.0f, inZone.y}, from)); },
         [&] { return still.grid.getViewState().firstVisibleBeat; },
         [&] { still.grid.mouseUp(leftDrag(still.grid, {inZone.x + 6.0f, inZone.y}, from)); }});

    // Regression test for FRO2083: a real pointer dragged past the window keeps the grid scrolling. A stroke paints
    // cells rather than moving anything, so nothing is drawn at the pointer to keep inside.
    std::shared_ptr<EdgeFixture> past;
    juce::Point<float> pastPress;
    auto pastAt = [&](float px) {
        return juce::Point<float>(static_cast<float>(past->grid.gridRegion().getRight()) + px, pastPress.y);
    };
    viewport_drag_follow::expectPastWindowFollows(
        {[&] {
             past = std::make_shared<EdgeFixture>();
             pastPress = centreOf(past->grid.cellBounds(past->grid.rowOfPitch(42), 0));
             past->grid.mouseDown(leftClick(past->grid, pastPress));
         },
         [&](float px) { past->grid.mouseDrag(leftDrag(past->grid, pastAt(px), pastPress)); }, [&] { past->ticks(1); },
         [&] { return past->grid.getViewState().firstVisibleBeat; }, nullptr,
         [&] { past->grid.mouseUp(leftDrag(past->grid, pastAt(40.0f), pastPress)); }});
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
        f->ticks(8); // past the ease-in, so the pace is each pointer's own
    }
    EXPECT_GT(deep.grid.getViewState().firstVisibleBeat, shallow.grid.getViewState().firstVisibleBeat);
    const auto press = centreOf(shallow.grid.cellBounds(shallow.grid.rowOfPitch(42), 0)); // as before any scrolling
    const juce::Point<float> middle{static_cast<float>(deep.grid.gridRegion().getCentreX()), press.y};
    deep.grid.mouseDrag(leftDrag(deep.grid, middle, press));
    deep.ticks(4); // the speed eases out over a few frames...
    EXPECT_FALSE(deep.grid.isEdgeScrollRunningForTest()) << "...then the follow stops itself";
    const double rested = deep.grid.getViewState().firstVisibleBeat;
    deep.ticks(5);
    EXPECT_EQ(deep.grid.getViewState().firstVisibleBeat, rested) << "and nothing scrolls once it is back in";
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

    // A ramp pressed with the pointer already inside the right edge zone stays put until the pointer moves toward it.
    EdgeFixture still;
    still.addHits(42, {0, 4, 8, 12, 16, 20, 24, 28}, 50);
    selectRow(still, 42);
    still.showLane(DrumLaneKind::Loudness);
    still.grid.setHorizontalView(kZoomedIn, 0.0);
    auto& stillLane = still.grid.getLane(DrumLaneKind::Loudness);
    const juce::Point<float> inZone{static_cast<float>(stillLane.getWidth() - 12), inLane(stillLane, 0, 0.9f).y};
    const auto from = inZone; // a ramp can start anywhere in the lane, so the press itself lands in the zone
    const juce::Point<float> towardEdge{inZone.x + 6.0f, from.y};
    viewport_drag_follow::expectPressInZoneHoldsStill(
        {[&] {
             stillLane.mouseDown(leftClick(stillLane, from));
             stillLane.mouseDrag(leftDrag(stillLane, inZone, from));
         },
         [&] { still.ticks(1); }, [&] { stillLane.mouseDrag(leftDrag(stillLane, towardEdge, from)); },
         [&] { return still.grid.getViewState().firstVisibleBeat; },
         [&] { stillLane.mouseUp(leftDrag(stillLane, towardEdge, from)); }});

    // Regression test for FRO2083: a real pointer dragged past the window keeps the grid scrolling. A ramp grows
    // across the lane rather than moves, so nothing is drawn at the pointer to keep inside.
    std::shared_ptr<EdgeFixture> past;
    juce::Point<float> pastPress;
    auto pastAt = [&](float px) {
        return juce::Point<float>(static_cast<float>(past->grid.getLane(DrumLaneKind::Loudness).getWidth()) + px,
                                  pastPress.y);
    };
    viewport_drag_follow::expectPastWindowFollows({[&] {
                                                       past = std::make_shared<EdgeFixture>();
                                                       past->addHits(42, {0, 4, 8, 12, 16, 20, 24, 28}, 50);
                                                       selectRow(*past, 42);
                                                       past->showLane(DrumLaneKind::Loudness);
                                                       past->grid.setHorizontalView(kZoomedIn, 0.0);
                                                       auto& pastLane = past->grid.getLane(DrumLaneKind::Loudness);
                                                       pastPress = inLane(pastLane, 0, 0.9f);
                                                       pastLane.mouseDown(leftClick(pastLane, pastPress));
                                                   },
                                                   [&](float px) {
                                                       auto& pastLane = past->grid.getLane(DrumLaneKind::Loudness);
                                                       pastLane.mouseDrag(leftDrag(pastLane, pastAt(px), pastPress));
                                                   },
                                                   [&] { past->ticks(1); },
                                                   [&] { return past->grid.getViewState().firstVisibleBeat; }, nullptr,
                                                   [&] {
                                                       auto& pastLane = past->grid.getLane(DrumLaneKind::Loudness);
                                                       pastLane.mouseUp(leftDrag(pastLane, pastAt(40.0f), pastPress));
                                                   }});
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
