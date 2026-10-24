// PointEditorSharedKeyTests.cpp -- one key script, run on every editor with ordered dots (the curve editor and an
// automation lane) through a small adapter per editor, so the shared key map (docs/control/point-editing.md) is
// checked once for both. A new point editor adds an adapter to PointEditorTypes.

#include "../../Timeline/AutomationLanes/AutomationLanesTestFixture.h"
#include "UI/ModuleViews/CurveEditor/CurveEditorComponent.h"
#include <gtest/gtest.h>

using synth::ui::AutomationLaneEditor;
using synth::ui::CurveEditorComponent;
using synth::ui::CurveMode;
using synth::ui::CurveModel;
using synth::ui::CurveNode;

namespace {

constexpr int kShift = juce::ModifierKeys::shiftModifier;
constexpr int kAlt = juce::ModifierKeys::altModifier;
constexpr int kCmd = juce::ModifierKeys::commandModifier;

juce::KeyPress key(int code, int mods = 0) { return juce::KeyPress(code, juce::ModifierKeys(mods), 0); }

// Three loose points between two fixed ends (the ends cannot be picked); one undo step per gesture.
struct CurveAdapter {
    static constexpr bool kHasUndo = false;
    CurveEditorComponent curve;
    int gestures = 0;

    CurveAdapter() {
        curve.setBounds(0, 0, 400, 160);
        CurveModel model(CurveMode::Free);
        model.setNodes({CurveNode{0.0, 0.5f, false, false}, CurveNode{0.25, 0.5f, true, true},
                        CurveNode{0.5, 0.5f, true, true}, CurveNode{0.75, 0.5f, true, true},
                        CurveNode{1.0, 0.5f, false, false}});
        curve.setModel(std::move(model));
        curve.onGestureStart = [this] { ++gestures; };
    }
    bool press(int code, int mods = 0) { return curve.keyPressed(key(code, mods)); }
    int points() const { return curve.getModel().getNumNodes() - 2; }
    int selected() const { return (int)curve.getSelectedIndices().size(); }
    double value(int point) const { return curve.getModel().getNode(point + 1).y; }
    bool duplicate() { return press('D', kCmd); }
    int editSteps() const { return gestures; }
    void undoLast() {}
};

// Three points at beats 1, 2 and 3 on an open lane; Cmd+D reaches a lane as the app's edit command, not as a key.
struct LaneAdapter : automation_lanes_test::LanesPanel {
    static constexpr bool kHasUndo = true;
    synth::LaneId lane;
    AutomationLaneEditor* editor = nullptr;

    LaneAdapter() {
        const auto track = doc.addTrack(synth::TrackKind::Midi, "Bass");
        lane = addLane(track, "cutoff");
        panel.setTrackAutomationExpanded(track, true);
        editor = panel.laneEditorForTest(lane);
        editor->setTool(AutomationLaneEditor::Tool::Pointer);
        doc.addBreakpoint(lane, 1.0, 20.0);
        doc.addBreakpoint(lane, 2.0, 60.0);
        doc.addBreakpoint(lane, 3.0, 40.0);
    }
    bool press(int code, int mods = 0) { return editor->keyPressed(key(code, mods)); }
    int points() const { return (int)doc.getLane(lane)->points.size(); }
    int selected() const { return editor->getPointSelection().size(); }
    double value(int point) const { return doc.getLane(lane)->points[(size_t)point].value; }
    bool duplicate() { return editor->duplicateSelectedPoints(); }
    int editSteps() const { return undo.getEditSerial(); }
    void undoLast() { undo.undo(); }
};

template <typename Editor>
class PointEditorSharedKeys : public ::testing::Test {
protected:
    Editor editor;
};

using PointEditorTypes = ::testing::Types<CurveAdapter, LaneAdapter>;
TYPED_TEST_SUITE(PointEditorSharedKeys, PointEditorTypes);

} // namespace

// One key script, the same result on every editor: the selection counts and every edit as one undo step.
TYPED_TEST(PointEditorSharedKeys, TheSameKeyScriptGivesTheSameSelectionCountsAndEditsOnACurveAndOnALane) {
    auto& e = this->editor;
    EXPECT_TRUE(e.press(juce::KeyPress::rightKey));
    EXPECT_TRUE(e.press(juce::KeyPress::rightKey));
    EXPECT_TRUE(e.press(juce::KeyPress::rightKey, kShift));
    EXPECT_EQ(e.selected(), 2);

    const double before = e.value(1);
    int steps = e.editSteps();
    EXPECT_TRUE(e.press(juce::KeyPress::upKey, kAlt));
    EXPECT_GT(e.value(1), before);
    EXPECT_EQ(e.editSteps(), steps + 1) << "one press, one undo step";

    steps = e.editSteps();
    ASSERT_TRUE(e.duplicate());
    EXPECT_EQ(e.points(), 5);
    EXPECT_EQ(e.selected(), 2) << "the copies are the selection";
    EXPECT_EQ(e.editSteps(), steps + 1);

    steps = e.editSteps();
    EXPECT_TRUE(e.press(juce::KeyPress::backspaceKey));
    EXPECT_EQ(e.points(), 3);
    EXPECT_EQ(e.editSteps(), steps + 1);
    if constexpr (TypeParam::kHasUndo) {
        e.undoLast();
        EXPECT_EQ(e.points(), 5) << "one Cmd+Z brings the copies back";
    }
}

TYPED_TEST(PointEditorSharedKeys, PlainArrowsPickOneDotAndStopAtTheLastWithoutEditing) {
    auto& e = this->editor;
    const int steps = e.editSteps();
    for (int i = 0; i < 5; ++i)
        EXPECT_TRUE(e.press(juce::KeyPress::rightKey));
    EXPECT_EQ(e.selected(), 1);
    EXPECT_TRUE(e.press(juce::KeyPress::rightKey, kShift)) << "consumed at the last dot";
    EXPECT_EQ(e.selected(), 1) << "nothing past the last dot";
    EXPECT_TRUE(e.press(juce::KeyPress::leftKey, kShift));
    EXPECT_EQ(e.selected(), 2);
    EXPECT_TRUE(e.press(juce::KeyPress::leftKey));
    EXPECT_EQ(e.selected(), 1) << "a plain arrow collapses the selection";
    EXPECT_EQ(e.editSteps(), steps) << "selecting edits nothing";
}

TYPED_TEST(PointEditorSharedKeys, CtrlTInsertsAfterTheCursorAndSelectsTheNewDot) {
    auto& e = this->editor;
    EXPECT_TRUE(e.press(juce::KeyPress::rightKey));
    const int steps = e.editSteps();
    EXPECT_TRUE(e.press('T', juce::ModifierKeys::ctrlModifier));
    EXPECT_EQ(e.points(), 4);
    EXPECT_EQ(e.selected(), 1);
    EXPECT_EQ(e.editSteps(), steps + 1);
}

TYPED_TEST(PointEditorSharedKeys, AltLeftMovesTheSelectionAndAPlainUpMovesItsValue) {
    auto& e = this->editor;
    EXPECT_TRUE(e.press(juce::KeyPress::rightKey));
    EXPECT_TRUE(e.press(juce::KeyPress::rightKey));
    const double before = e.value(1);
    EXPECT_TRUE(e.press(juce::KeyPress::upKey));
    EXPECT_GT(e.value(1), before) << "plain Up is Alt+Up";
    const int steps = e.editSteps();
    EXPECT_TRUE(e.press(juce::KeyPress::leftKey, kAlt));
    EXPECT_EQ(e.editSteps(), steps + 1) << "one press, one undo step";
}
