// AutomationLanesModulatorMoveMotionTests.cpp -- a modulator row dragged onto another lane grows in at its new slot
// while the gap it left in its old lane closes, so neither lane block (nor anything below) jumps, and the row is drawn
// once, not also fading where it was. Its undo moves it back the same way. Against a real MainComponent; the animated
// path is forced (FadeAnimateGuard) and the tweens are stepped by hand, never by frames.

#include "../../Layout/FadeVisibilityTestGuard.h"
#include "AutomationLanesAmountFixture.h"

using synth::ui::ModulatorRow;

namespace {

// A real press on the row's title, a drag to a screen y and a release, through the row's own mouse handlers.
void dragRowToScreenY(ModulatorRow& row, int screenY) {
    const juce::Point<float> pressLocal((float)ModulatorRow::kIndent + 30.0f, 5.0f);
    const auto pressScreen = row.localPointToGlobal(pressLocal.toInt());
    const auto at = [&](int y) {
        const auto local = row.getLocalPoint(nullptr, juce::Point<int>(pressScreen.x, y)).toFloat();
        return makeDragEvent(row, local, pressLocal, automation_lanes_test::leftButton());
    };
    row.mouseDown(makeClickEvent(row, pressLocal, automation_lanes_test::leftButton()));
    for (int i = 1; i <= 6; ++i)
        row.mouseDrag(at(pressScreen.y + (screenY - pressScreen.y) * i / 6));
    row.mouseUp(at(screenY));
}

// A Cutoff lane carrying an LFO and an empty Resonance lane below it on the same track, everything landed.
struct MoveMotionScene : amount_test::AmountScene {
    synth::LaneId resLane;

    MoveMotionScene() {
        resLane = doc().addLane(track, targetUuid, "resonance", {0.0f, 1.0f, 0.5f});
        undo().clearUndoHistory();
        mc.getGraphEditor().updateComponents();
        panel().refreshModulators();
        lanes().stepBlockMotionForTest(1.0f);
        synth::ui::FadeVisibility::stepAllForTest(1.0f);
    }
    synth::ui::TimelineAutomationLanes& lanes() { return panel().getAutomationLanes(); }
    int resY() { return panel().laneHeaderForTest(resLane)->getY(); }
    int rowsTopOnScreen(synth::LaneId l) {
        auto& header = *panel().laneHeaderForTest(l);
        return header.localPointToGlobal(juce::Point<int>(0, header.getHeight())).y;
    }
    // Modulator rows drawn in the header column: the lanes' own and any still fading where they stood.
    int visibleRows() {
        int count = 0;
        for (auto* child : panel().getTrackHeaderListForTest().getChildren())
            if (dynamic_cast<ModulatorRow*>(child) != nullptr && child->isVisible() && child->getHeight() > 0)
                ++count;
        return count;
    }
};

void expectBetween(int value, int a, int b, const char* what) {
    EXPECT_GT(value, std::min(a, b)) << what;
    EXPECT_LT(value, std::max(a, b)) << what;
}

} // namespace

// Regression test for FRO888: both lane blocks changed height at once on the drop, so the lanes and tracks below
// jumped while only the row's opacity faded.
TEST_F(TimelinePanelIntegrationTest, ARowDroppedOnAnotherLaneGrowsInThereWhileItsOldGapClosesAndIsDrawnOnce) {
    MoveMotionScene s;
    FadeAnimateGuard animate;
    auto& lanes = s.lanes();
    ASSERT_EQ(lanes.modulatorCount(s.lane), 1);
    const int full = lanes.modulatorRowHeight(s.lane);
    const int resY = s.resY();

    dragRowToScreenY(*s.row(), s.rowsTopOnScreen(s.resLane) - 8); // onto the Resonance lane's own row
    ASSERT_EQ(lanes.modulatorCount(s.lane), 0);
    ASSERT_EQ(lanes.modulatorCount(s.resLane), 1);
    EXPECT_TRUE(lanes.isBlockMotionAnimating());
    EXPECT_EQ(lanes.modulatorRowHeightFor(s.resLane, 0), 0) << "the moved row starts with no height in its new lane";
    EXPECT_EQ(s.resY(), resY) << "the gap it left still holds the Resonance lane where it was";
    EXPECT_EQ(s.visibleRows(), 0) << "not drawn in its old place: it is the row growing in the new one";

    lanes.stepBlockMotionForTest(0.5f);
    expectBetween(s.resY(), resY - full, resY, "the old gap is closing");
    expectBetween(lanes.modulatorRowHeightFor(s.resLane, 0), 0, full, "the row is growing in its new lane");
    EXPECT_EQ(s.visibleRows(), 1) << "drawn once";

    lanes.stepBlockMotionForTest(1.0f);
    EXPECT_EQ(s.resY(), resY - full);
    EXPECT_EQ(lanes.modulatorRowHeightFor(s.resLane, 0), full);
    EXPECT_EQ(lanes.leavingModulatorCountForTest(), 0);

    synth::ui::FadeVisibility::stepAllForTest(1.0f);
    ASSERT_TRUE(s.undo().undo());
    ASSERT_EQ(lanes.modulatorCount(s.lane), 1);
    EXPECT_TRUE(lanes.isBlockMotionAnimating()) << "the undo moves it back the same way";
    lanes.stepBlockMotionForTest(0.5f);
    expectBetween(s.resY(), resY - full, resY, "the Cutoff block grows back");
    lanes.stepBlockMotionForTest(1.0f);
    EXPECT_EQ(s.resY(), resY);
}
