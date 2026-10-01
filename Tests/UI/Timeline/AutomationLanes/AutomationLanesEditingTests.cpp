// AutomationLanesEditingTests.cpp -- editing an automation lane in its row under the track: the
// timeline's edit tool drives the curve tools, the lane header's menu moves and deletes the lane,
// and its value readout follows the playhead. Real events on the real panel's children.

#include "AutomationLanesTestFixture.h"
#include "Timeline/AutomationKernel.h"
#include "Transport/TransportService.h"
#include "UI/Timeline/AutomationLanes/AutomationLaneActions.h"
#include "UI/Timeline/AutomationLanes/AutomationToolMapping.h"

using namespace automation_lanes_test;
using synth::TrackKind;
using synth::ui::AutomationLaneEditor;
using synth::ui::EditTool;

namespace {

// An open Bass track with one lane, its editor laid out at its row.
struct OpenLane : LanesPanel {
    synth::TrackId bass;
    synth::LaneId lane;
    AutomationLaneEditor* editor = nullptr;

    OpenLane() {
        bass = doc.addTrack(TrackKind::Midi, "Bass");
        lane = addLane(bass, "cutoff");
        panel.setTrackAutomationExpanded(bass, true);
        editor = panel.laneEditorForTest(lane);
    }

    const synth::AutomationLane& theLane() const { return *doc.getLane(lane); }
    float xAt(double beat) { return (float)panel.getViewState().beatToX(beat); }
};

} // namespace

TEST(AutomationToolMappingTest, EveryEditToolHasACurveTool) {
    using Tool = AutomationLaneEditor::Tool;
    EXPECT_EQ(synth::ui::automationToolFor(EditTool::Select, false), Tool::Pointer);
    EXPECT_EQ(synth::ui::automationToolFor(EditTool::Draw, false), Tool::Pencil);
    EXPECT_EQ(synth::ui::automationToolFor(EditTool::Draw, true), Tool::Line);
    EXPECT_EQ(synth::ui::automationToolFor(EditTool::Erase, false), Tool::Eraser);
    EXPECT_EQ(synth::ui::automationToolFor(EditTool::Erase, true), Tool::Eraser);
    for (auto tool : {EditTool::Range, EditTool::Split, EditTool::Glue, EditTool::Mute})
        EXPECT_EQ(synth::ui::automationToolFor(tool, false), Tool::Pointer);
}

TEST(AutomationLanesEditingTest, DrawToolDragOnTheLaneRowAddsPointsAsOneUndoStep) {
    OpenLane f;
    ASSERT_NE(f.editor, nullptr);
    f.panel.setActiveTool(EditTool::Draw);
    EXPECT_EQ(f.editor->getTool(), AutomationLaneEditor::Tool::Pencil);

    // Located the way a click is: the component under the lane row's centre IS the editor.
    const auto row = f.panel.laneRowBoundsForTest(f.lane);
    ASSERT_EQ(f.componentAt({row.getX() + 80, row.getCentreY()}), f.editor);
    dragAcross(*f.editor, {f.xAt(1.0), 30.0f}, {f.xAt(6.0), 8.0f}, 20);

    EXPECT_GE(f.theLane().points.size(), 2u) << "a freehand stroke lands as points";
    EXPECT_NEAR(f.theLane().points.front().beat, 1.0, 0.05);
    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_TRUE(f.theLane().points.empty()) << "the whole stroke is ONE undo step";
}

TEST(AutomationLanesEditingTest, ShiftDrawDrawsAStraightLine) {
    OpenLane f;
    f.panel.setActiveTool(EditTool::Draw);
    dragAcross(*f.editor, {f.xAt(2.0), 30.0f}, {f.xAt(4.0), 10.0f}, 6, leftButton(juce::ModifierKeys::shiftModifier));
    ASSERT_EQ(f.theLane().points.size(), 2u) << "a line commits its two snapped endpoints";
    EXPECT_DOUBLE_EQ(f.theLane().points[0].beat, 2.0);
    EXPECT_DOUBLE_EQ(f.theLane().points[1].beat, 4.0);
    EXPECT_EQ(f.editor->getTool(), AutomationLaneEditor::Tool::Line);
}

TEST(AutomationLanesEditingTest, EraseToolRemovesThePointsItSweeps) {
    OpenLane f;
    ASSERT_TRUE(f.doc.addBreakpoint(f.lane, 2.0, 50.0));
    ASSERT_TRUE(f.doc.addBreakpoint(f.lane, 8.0, 50.0));
    f.panel.setActiveTool(EditTool::Erase);

    const auto handle = f.editor->getHandleRectForTest(2.0).getCentre().toFloat();
    dragAcross(*f.editor, handle.translated(-6.0f, 0.0f), handle.translated(6.0f, 0.0f), 4);

    ASSERT_EQ(f.theLane().points.size(), 1u);
    EXPECT_DOUBLE_EQ(f.theLane().points[0].beat, 8.0) << "only the swept point goes";
    f.undo.undo();
    EXPECT_EQ(f.theLane().points.size(), 2u);
}

TEST(AutomationLanesEditingTest, SelectToolMovesAPoint) {
    OpenLane f;
    ASSERT_TRUE(f.doc.addBreakpoint(f.lane, 2.0, 50.0));
    f.panel.setActiveTool(EditTool::Select);

    const auto handle = f.editor->getHandleRectForTest(2.0).getCentre().toFloat();
    dragAcross(*f.editor, handle, {f.xAt(3.0), handle.y}, 5);

    ASSERT_EQ(f.theLane().points.size(), 1u);
    EXPECT_DOUBLE_EQ(f.theLane().points[0].beat, 3.0);
    EXPECT_NEAR(f.theLane().points[0].value, 50.0, 3.0) << "a horizontal drag keeps the value";
}

TEST(AutomationLanesEditingTest, LaneMenuMovesTheLaneToAnotherTrackAndUndoBringsItBack) {
    OpenLane f;
    const auto lead = f.doc.addTrack(TrackKind::Midi, "Lead");
    f.doc.addTrack(TrackKind::Audio, "Vox");
    auto* header = f.panel.laneHeaderForTest(f.lane);
    ASSERT_NE(header, nullptr);
    EXPECT_EQ(header->getMenuButton().getTitle(), "Lane menu for cutoff");

    auto menu = header->buildMenu();
    EXPECT_NE(findMenuItem(menu, "Vox"), nullptr) << "Audio tracks are move targets too";
    EXPECT_EQ(findMenuItem(menu, "Bass"), nullptr) << "never its own track";
    const auto* toLead = findMenuItem(menu, "Lead");
    ASSERT_NE(toLead, nullptr);
    header->applyMenuChoice(toLead->itemID); // destroys `header`: the lane leaves the open Bass track

    EXPECT_EQ(f.doc.getTrackForLane(f.lane)->id, lead);
    EXPECT_EQ(f.panel.laneHeaderForTest(f.lane), nullptr) << "Lead is folded, so the row is gone from view";
    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.doc.getTrackForLane(f.lane)->id, f.bass);
    EXPECT_NE(f.panel.laneHeaderForTest(f.lane), nullptr) << "back under the open Bass track";
}

TEST(AutomationLanesEditingTest, LaneMenuDeletesTheLaneAndUndoRestoresIt) {
    OpenLane f;
    ASSERT_TRUE(f.doc.addBreakpoint(f.lane, 2.0, 70.0));
    auto* header = f.panel.laneHeaderForTest(f.lane);
    auto menu = header->buildMenu();
    const auto* item = findMenuItem(menu, "Delete lane");
    ASSERT_NE(item, nullptr);
    header->applyMenuChoice(item->itemID);

    EXPECT_EQ(f.doc.getLane(f.lane), nullptr);
    EXPECT_EQ(f.panel.laneEditorForTest(f.lane), nullptr);
    EXPECT_TRUE(f.panel.getTrackHeaderAt(0)->getFoldArrow().isVisible()) << "no lanes left: the arrow stays";
    EXPECT_NE(f.panel.addAutomationRowForTest(f.bass), nullptr) << "the open track keeps its add row";
    f.undo.undo();
    ASSERT_NE(f.doc.getLane(f.lane), nullptr);
    EXPECT_EQ(f.theLane().points.size(), 1u) << "the points come back with it";
}

TEST(AutomationLanesEditingTest, ValueReadoutIsTheCurveAtThePlayheadBeat) {
    OpenLane f;
    ASSERT_TRUE(f.doc.addBreakpoint(f.lane, 0.0, 10.0));
    ASSERT_TRUE(f.doc.addBreakpoint(f.lane, 4.0, 90.0));
    auto* header = f.panel.laneHeaderForTest(f.lane);
    ASSERT_NE(header, nullptr);

    synth::TransportService::PositionSnapshot snapshot;
    snapshot.ppq = 3.0;
    f.panel.updateFromTransport(snapshot, 0.0);

    std::vector<synth::TimelineSnapshot::Point> points;
    for (const auto& bp : f.theLane().points)
        points.push_back({bp.beat, bp.value, bp.tension, bp.curve});
    synth::AutomationCursor cursor{};
    const double expected = synth::AutomationKernel::evaluate(points.data(), (int)points.size(), 3.0, 50.0, cursor);
    EXPECT_NEAR(expected, 70.0, 1e-9) << "linear between (0,10) and (4,90)";
    EXPECT_EQ(header->getValueText(), synth::ui::laneValueText(f.theLane(), expected, nullptr));
    EXPECT_EQ(header->getValueText(), "70.0");

    snapshot.ppq = 1.0;
    f.panel.updateFromTransport(snapshot, 0.0);
    EXPECT_EQ(header->getValueText(), "30.0");
}

namespace {
// Names every node "Filter 1" and every parameter "Cutoff", so a lane header that shows a raw
// paramId or a uuid prefix is visibly not going through the host.
struct NamingHost : synth::ui::TrackHeaderHost {
    std::vector<BindingOption> getAvailableTrackInNodes(synth::TrackId) override { return {}; }
    juce::String getNodeDisplayName(const juce::String&) override { return "Filter 1"; }
    juce::String getParameterDisplayName(const juce::String&, const juce::String&) override { return "Cutoff"; }
    void bindTrackTo(synth::TrackId, const juce::String&) override {}
    void createAndBindTrackInNode(synth::TrackId) override {}
    void selectNodeInGraph(const juce::String&) override {}
    void deleteTrack(synth::TrackId) override {}
    void performTrackEdit(const std::function<void()>& mutation) override {
        if (mutation)
            mutation();
    }
    void addMidiTrack() override {}
    void addAudioTrack() override {}
    void addInstrumentTrack(const juce::String&, bool) override {}
    std::vector<PluginLaneOption> getAvailablePluginLaneOptions() const override { return {}; }
    synth::LaneId addPluginAutomationLane(const PluginLaneOption&) override { return {}; }
};
} // namespace

// The panel's host must reach the lane headers: without it every lane read "cutoff" over a uuid
// prefix in a real project instead of "Cutoff" over "Filter 1".
TEST(AutomationLanesEditingTest, LaneHeaderNamesItsParameterThroughThePanelsHost) {
    NamingHost host;
    LanesPanel f;
    f.panel.setTrackHeaderHost(&host);
    const auto bass = f.doc.addTrack(TrackKind::Midi, "Bass");
    const auto lane = f.addLane(bass, "cutoff");
    f.panel.setTrackAutomationExpanded(bass, true);
    auto* header = f.panel.laneHeaderForTest(lane);
    ASSERT_NE(header, nullptr);
    EXPECT_EQ(header->getRecordModeCombo().getTitle(), "Cutoff record mode");
    EXPECT_EQ(header->getMenuButton().getTitle(), "Lane menu for Cutoff");
    f.panel.setTrackHeaderHost(nullptr); // the host dies first
}
