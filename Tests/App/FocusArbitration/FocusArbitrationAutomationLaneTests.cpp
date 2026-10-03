// Concern: the automation lane surface of the one focus-ownership rule (MainComponent::resolveEditSurface):
// Select All, Copy, Cut and Paste act on a lane editor's points, Duplicate acts on the lane itself, and Repeat stays
// inactive there.
#include "FocusArbitrationTestFixture.h"

namespace {
struct LaneSurface {
    MainComponent mc{std::make_unique<FocusMockProvider>()};
    synth::LaneId lane;
    synth::ui::AutomationLaneEditor* editor = nullptr;

    LaneSurface() {
        auto& doc = mc.getTimelineDoc();
        const auto track = doc.addTrack(synth::TrackKind::Midi, "Track");
        synth::AutomationLane::RangeSnapshot range;
        range.minValue = 0.0f;
        range.maxValue = 100.0f;
        range.defaultValue = 50.0f;
        lane = doc.addLane(track, "node-x", "cutoff", range);
        doc.addBreakpoint(lane, 1.0, 20.0);
        doc.addBreakpoint(lane, 2.0, 60.0);
        mc.getTimelinePanel().showAutomationLane(lane);
        mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::AutomationLane);
        editor = mc.getTimelinePanel().laneEditorForTest(lane);
    }
    const synth::AutomationLane& theLane() { return *mc.getTimelineDoc().getLane(lane); }
};
} // namespace

TEST_F(FocusArbitrationTest, SelectAllOnTheAutomationLaneSurfaceSelectsThePointsAndNotTheModules) {
    LaneSurface f;
    ASSERT_NE(f.editor, nullptr);
    auto& cm = f.mc.getCommandManager();
    EXPECT_FALSE(commandIsActive(f.mc, AppCommands::copySelection)) << "no point selected";
    EXPECT_FALSE(commandIsActive(f.mc, AppCommands::cutSelection));
    EXPECT_FALSE(commandIsActive(f.mc, AppCommands::pasteSelection)) << "nothing copied yet";

    ASSERT_TRUE(cm.invokeDirectly(AppCommands::selectAllModules, false));
    EXPECT_EQ(f.editor->getPointSelection().size(), 2);
    EXPECT_EQ(f.mc.getGraphEditor().getSelectionCount(), 0) << "the graph selection is untouched";
    EXPECT_TRUE(commandIsActive(f.mc, AppCommands::copySelection));
    EXPECT_TRUE(commandIsActive(f.mc, AppCommands::cutSelection));
    EXPECT_TRUE(commandIsActive(f.mc, AppCommands::duplicateSelection)) << "Duplicate copies the lane, not a point";
    EXPECT_FALSE(commandIsActive(f.mc, AppCommands::repeatSelection));
}

TEST_F(FocusArbitrationTest, CopyCutAndPasteOnTheAutomationLaneSurfaceMovePoints) {
    LaneSurface f;
    auto& cm = f.mc.getCommandManager();
    f.editor->selectAllPoints();

    ASSERT_TRUE(cm.invokeDirectly(AppCommands::copySelection, false));
    EXPECT_TRUE(commandIsActive(f.mc, AppCommands::pasteSelection));
    ASSERT_EQ(f.theLane().points.size(), 2u);

    ASSERT_TRUE(cm.invokeDirectly(AppCommands::cutSelection, false));
    EXPECT_TRUE(f.theLane().points.empty());
    ASSERT_TRUE(f.mc.getUndoManager().canUndo());

    ASSERT_TRUE(cm.invokeDirectly(AppCommands::pasteSelection, false));
    ASSERT_EQ(f.theLane().points.size(), 2u) << "pasted at the playhead";
    EXPECT_DOUBLE_EQ(f.theLane().points[0].beat, 0.0);
    EXPECT_DOUBLE_EQ(f.theLane().points[1].beat, 1.0);
    EXPECT_EQ(f.editor->getPointSelection().size(), 2) << "the pasted points are selected";
}
