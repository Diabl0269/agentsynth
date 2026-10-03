// AutomationLanesRetargetTests.cpp -- "Change parameter...": the lane menu item and a click on the parameter name
// open the add-automation picker, a pick points the lane at that parameter keeping its curve (one undo step), and a
// parameter that already has a lane is never offered.

#include "AutomationLanesMenuFixture.h"
#include "UI/Timeline/AutomationLanes/AutomationLaneActions.h"

using namespace lane_menu_test;
using namespace automation_lanes_test;

namespace {
void pick(synth::ui::ModMatrixPicker& picker, const juce::String& itemText) {
    const auto texts = picker.getVisibleItemTextsForTest();
    for (int i = 0; i < (int)texts.size(); ++i)
        if (texts[(size_t)i] == itemText) {
            picker.chooseVisibleItemForTest(i);
            return;
        }
    FAIL() << "no picker row named " << itemText;
}
} // namespace

TEST(AutomationLanesRetargetTest, TheMenuItemOpensThePickerWithoutTheParametersThatAlreadyHaveALane) {
    MenuPanel f;
    f.addLane(f.track, "res"); // Resonance already has a lane
    f.host.ranges["res"] = {0.0f, 1.0f, 0.5f};
    PickerCapture capture;

    f.header(f.lane)->applyMenuChoice(synth::ui::AutomationLaneHeaderComponent::kChangeParameterMenuId);

    ASSERT_NE(capture.picker, nullptr);
    EXPECT_EQ(capture.picker->getVisibleItemTextsForTest(), (std::vector<juce::String>{"Detune"}))
        << "neither Cutoff (this lane) nor Resonance (another lane) can be picked";
}

TEST(AutomationLanesRetargetTest, PickingRetargetsTheLaneKeepingItsCurveAndItIsOneUndoStep) {
    MenuPanel f;
    PickerCapture capture;
    const auto beforeIds = f.doc.getTrack(f.track)->lanes.size();
    f.header(f.lane)->applyMenuChoice(synth::ui::AutomationLaneHeaderComponent::kChangeParameterMenuId);
    ASSERT_NE(capture.picker, nullptr);

    pick(*capture.picker, "Detune");

    const auto& lane = f.laneOf(f.lane);
    EXPECT_EQ(lane.paramId, "detune");
    EXPECT_EQ(lane.nodeUuid, "node-detune");
    EXPECT_FLOAT_EQ(lane.range.minValue, -100.0f);
    EXPECT_FLOAT_EQ(lane.range.maxValue, 100.0f);
    ASSERT_EQ(lane.points.size(), 3u);
    EXPECT_DOUBLE_EQ(lane.points[0].value, -100.0);
    EXPECT_DOUBLE_EQ(lane.points[1].value, -50.0) << "a quarter of the way up, as before";
    EXPECT_DOUBLE_EQ(lane.points[2].value, 100.0);
    EXPECT_EQ(f.doc.getTrack(f.track)->lanes.size(), beforeIds);
    EXPECT_EQ(f.doc.getLaneForParam("node-cutoff", "cutoff"), nullptr);

    ASSERT_TRUE(f.undo.undo());
    EXPECT_EQ(f.laneOf(f.lane).paramId, "cutoff") << "one Cmd+Z puts the parameter and the points back";
    EXPECT_DOUBLE_EQ(f.laneOf(f.lane).points[1].value, 25.0);
    EXPECT_FALSE(f.undo.canUndo()) << "nothing else was recorded";
}

TEST(AutomationLanesRetargetTest, ARetargetOntoAParameterWithAnotherLaneIsRefusedAndRecordsNothing) {
    MenuPanel f;
    const auto other = f.addLane(f.track, "res");
    ASSERT_TRUE(other.isValid());
    const synth::ui::LaneTarget target{"node-res", "res", -1, {0.0f, 1.0f, 0.5f}};

    EXPECT_FALSE(synth::ui::retargetLaneUndoable(f.doc, &f.undo, f.lane, target));

    EXPECT_EQ(f.laneOf(f.lane).paramId, "cutoff");
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(AutomationLanesRetargetTest, ClickingTheParameterNameOpensTheSamePicker) {
    MenuPanel f;
    PickerCapture capture;
    auto* header = f.header(f.lane);
    ASSERT_NE(header, nullptr);
    const auto name = header->getNameAreaForTest();
    ASSERT_FALSE(name.isEmpty());

    const auto click = makeClickEvent(*header, name.getCentre().toFloat(), leftButton());
    header->mouseDown(click);
    EXPECT_EQ(capture.picker, nullptr) << "the pick waits for the release: a press may still become a drag";
    header->mouseUp(click);

    ASSERT_NE(capture.picker, nullptr);
    const auto texts = capture.picker->getVisibleItemTextsForTest();
    EXPECT_EQ(texts, (std::vector<juce::String>{"Resonance", "Detune"}));
    EXPECT_FALSE(f.undo.canUndo()) << "opening the picker edits nothing";
}

TEST(AutomationLanesRetargetTest, ALeftClickAwayFromTheNameOpensNoPicker) {
    MenuPanel f;
    PickerCapture capture;
    auto* header = f.header(f.lane);
    const auto click =
        makeClickEvent(*header, {(float)synth::ui::AutomationLaneHeaderComponent::kIndent + 1.0f, 3.0f}, leftButton());
    header->mouseDown(click);
    header->mouseUp(click);
    EXPECT_EQ(capture.picker, nullptr);
}

TEST(AutomationLanesRetargetTest, TheHeaderRereadsItsNameAfterTheRetarget) {
    MenuPanel f;
    auto* header = f.header(f.lane);
    ASSERT_NE(header, nullptr);
    EXPECT_EQ(header->getRecordModeCombo().getTitle(), "cutoff record mode");

    const synth::ui::LaneTarget target{"node-detune", "detune", -1, {-100.0f, 100.0f, 0.0f}};
    ASSERT_TRUE(synth::ui::retargetLaneUndoable(f.doc, &f.undo, f.lane, target));
    f.panel.getAutomationLanes().sync();

    EXPECT_EQ(header->getRecordModeCombo().getTitle(), "detune record mode");
}
