// AutomationLanesDuplicateTests.cpp -- "Duplicate": the lane menu item opens the picker first; the copy exists only
// once a parameter is picked, directly below its source with the same points and record mode, in ONE undo step.
// Dismissing the picker leaves the doc untouched.

#include "AutomationLanesMenuFixture.h"
#include "UI/Timeline/AutomationLanes/AutomationLaneActions.h"

using namespace lane_menu_test;
using namespace automation_lanes_test;

namespace {
void pickRow(synth::ui::ModMatrixPicker& picker, int index) { picker.chooseVisibleItemForTest(index); }

std::vector<synth::LaneId> laneIds(const synth::TimelineDoc& doc, synth::TrackId track) {
    std::vector<synth::LaneId> ids;
    for (const auto& lane : doc.getTrack(track)->lanes)
        ids.push_back(lane.id);
    return ids;
}
} // namespace

TEST(AutomationLanesDuplicateTest, DismissingThePickerLeavesTheDocUnchanged) {
    MenuPanel f;
    PickerCapture capture;
    const auto revision = f.doc.getRevision();
    const auto before = laneIds(f.doc, f.track);

    f.header(f.lane)->applyMenuChoice(synth::ui::AutomationLaneHeaderComponent::kDuplicateMenuId);
    ASSERT_NE(capture.picker, nullptr) << "the picker opens first";
    EXPECT_EQ(f.doc.getRevision(), revision) << "no transient lane while it is open";
    capture.picker.reset(); // dismissed without a pick

    EXPECT_EQ(f.doc.getRevision(), revision);
    EXPECT_EQ(laneIds(f.doc, f.track), before);
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(AutomationLanesDuplicateTest, PickingCreatesTheCopyDirectlyBelowWithTheSamePointsAndOneUndoRemovesIt) {
    MenuPanel f;
    const auto last = f.addLane(f.track, "last"); // a lane below the source
    f.doc.setLaneRecordMode(f.lane, static_cast<int>(synth::LaneRecordMode::Latch));
    PickerCapture capture;
    f.header(f.lane)->applyMenuChoice(synth::ui::AutomationLaneHeaderComponent::kDuplicateMenuId);
    ASSERT_NE(capture.picker, nullptr);
    const auto texts = capture.picker->getVisibleItemTextsForTest();
    ASSERT_EQ(texts, (std::vector<juce::String>{"Resonance", "Detune"}));
    const auto before = laneIds(f.doc, f.track);

    pickRow(*capture.picker, 0); // Resonance, range 0..1

    const auto after = laneIds(f.doc, f.track);
    ASSERT_EQ(after.size(), before.size() + 1);
    EXPECT_EQ(after[0], f.lane);
    EXPECT_EQ(after[2], last) << "the lane that was below stays below the copy";
    const auto& copy = f.laneOf(after[1]);
    EXPECT_EQ(copy.paramId, "res");
    EXPECT_EQ(copy.recordMode, static_cast<int>(synth::LaneRecordMode::Latch));
    const auto& source = f.laneOf(f.lane);
    ASSERT_EQ(copy.points.size(), source.points.size());
    for (size_t i = 0; i < copy.points.size(); ++i) {
        EXPECT_DOUBLE_EQ(copy.points[i].beat, source.points[i].beat);
        EXPECT_NEAR(copy.points[i].value, source.points[i].value / 100.0, 1.0e-9) << "same position in its own range";
    }
    EXPECT_EQ(source.paramId, "cutoff");

    ASSERT_TRUE(f.undo.undo());
    EXPECT_EQ(laneIds(f.doc, f.track), before) << "ONE Cmd+Z removes the copy";
    EXPECT_EQ(f.doc.getLaneForParam("node-res", "res"), nullptr);
}

TEST(AutomationLanesDuplicateTest, ADuplicateOntoABoundParameterCreatesNothingAndRecordsNothing) {
    MenuPanel f;
    const synth::ui::LaneTarget target{"node-cutoff", "cutoff", -1, {0.0f, 100.0f, 50.0f}};
    const auto before = laneIds(f.doc, f.track);

    EXPECT_FALSE(synth::ui::duplicateLaneUndoable(f.doc, &f.undo, f.lane, target).isValid());

    EXPECT_EQ(laneIds(f.doc, f.track), before);
    EXPECT_FALSE(f.undo.canUndo());
}
