// PointEditorSharedKeyTests.cpp -- one key script, run on every editor with ordered dots (the curve editor and an
// automation lane) through a small adapter per editor, so the shared key map (docs/control/point-editing.md) is
// checked once for both. A new point editor adds an adapter to PointEditorTypes.

#include "../../Timeline/AutomationLanes/AutomationLanesTestFixture.h"
#include "UI/ModuleViews/CurveEditor/CurveEditorComponent.h"
#include <algorithm>
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
// Control and Option on the Mac; Ctrl+Alt elsewhere.
constexpr int kScale = juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::altModifier;

juce::KeyPress key(int code, int mods = 0) { return juce::KeyPress(code, juce::ModifierKeys(mods), 0); }

// Three loose points between two fixed ends (the ends cannot be picked); one undo step per gesture.
struct CurveAdapter {
    static constexpr bool kHasUndo = false;
    CurveEditorComponent curve;
    int gestures = 0;

    CurveAdapter() {
        curve.setBounds(0, 0, 400, 160);
        CurveModel model(CurveMode::Free);
        model.setNodes({CurveNode{0.0, 0.5f, false, false}, CurveNode{0.25, 0.2f, true, true},
                        CurveNode{0.5, 0.6f, true, true}, CurveNode{0.75, 0.4f, true, true},
                        CurveNode{1.0, 0.5f, false, false}});
        curve.setModel(std::move(model));
        curve.onGestureStart = [this] { ++gestures; };
    }
    bool press(int code, int mods = 0) { return curve.keyPressed(key(code, mods)); }
    int points() const { return curve.getModel().getNumNodes() - 2; }
    int selected() const { return (int)curve.getSelectedIndices().size(); }
    double value(int point) const { return curve.getModel().getNode(point + 1).y; }
    double position(int point) const { return curve.getModel().getNode(point + 1).x; }
    static double rangeLow() { return 0.0; }
    static double rangeHigh() { return 1.0; }
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
    double position(int point) const { return doc.getLane(lane)->points[(size_t)point].beat; }
    double rangeLow() const { return doc.getLane(lane)->range.minValue; }
    double rangeHigh() const { return doc.getLane(lane)->range.maxValue; }
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

// ---- Ctrl+Alt+arrows: scale the selection about its middle (docs/control/point-editing.md) ----

namespace {
template <typename E>
void selectAllThree(E& e) {
    ASSERT_TRUE(e.press(juce::KeyPress::rightKey));
    ASSERT_TRUE(e.press(juce::KeyPress::rightKey, kShift));
    ASSERT_TRUE(e.press(juce::KeyPress::rightKey, kShift));
    ASSERT_EQ(e.selected(), 3);
}
template <typename E>
double spreadOfValues(const E& e) {
    return std::max({e.value(0), e.value(1), e.value(2)}) - std::min({e.value(0), e.value(1), e.value(2)});
}
} // namespace

TYPED_TEST(PointEditorSharedKeys, CtrlAltUpAndDownSpreadAndPullTogetherTheValuesAboutTheirMiddleOneUndoStepEach) {
    auto& e = this->editor;
    selectAllThree(e);
    const double before = spreadOfValues(e);
    const double middleBefore =
        (std::max({e.value(0), e.value(1), e.value(2)}) + std::min({e.value(0), e.value(1), e.value(2)})) / 2;
    int steps = e.editSteps();
    EXPECT_TRUE(e.press(juce::KeyPress::upKey, kScale));
    EXPECT_NEAR(spreadOfValues(e), before * 1.1, 1e-4) << "a tenth wider";
    EXPECT_EQ(e.editSteps(), steps + 1) << "one press, one undo step";
    EXPECT_EQ(e.selected(), 3) << "the selection stays";

    steps = e.editSteps();
    EXPECT_TRUE(e.press(juce::KeyPress::downKey, kScale));
    EXPECT_NEAR(spreadOfValues(e), before, 1e-4) << "Down undoes an Up";
    EXPECT_EQ(e.editSteps(), steps + 1);
    EXPECT_TRUE(e.press(juce::KeyPress::downKey, kScale));
    EXPECT_LT(spreadOfValues(e), before);
    const double middleAfter =
        (std::max({e.value(0), e.value(1), e.value(2)}) + std::min({e.value(0), e.value(1), e.value(2)})) / 2;
    EXPECT_NEAR(middleAfter, middleBefore, 1e-4) << "the middle stays";
    if constexpr (TypeParam::kHasUndo) {
        e.undoLast();
        EXPECT_NEAR(spreadOfValues(e), before, 1e-4) << "one Cmd+Z takes back one press";
    }
}

TYPED_TEST(PointEditorSharedKeys, CtrlAltRightAndLeftSpreadAndPullTogetherTheTimesWithoutChangingTheOrder) {
    auto& e = this->editor;
    selectAllThree(e);
    const double width = e.position(2) - e.position(0);
    int steps = e.editSteps();
    EXPECT_TRUE(e.press(juce::KeyPress::rightKey, kScale));
    EXPECT_NEAR(e.position(2) - e.position(0), width * 1.1, 1e-3);
    EXPECT_EQ(e.editSteps(), steps + 1);
    steps = e.editSteps();
    EXPECT_TRUE(e.press(juce::KeyPress::leftKey, kScale));
    EXPECT_NEAR(e.position(2) - e.position(0), width, 1e-3);
    EXPECT_EQ(e.editSteps(), steps + 1);
    for (int i = 0; i < 80; ++i)
        e.press(juce::KeyPress::leftKey, kScale);
    EXPECT_LT(e.position(0), e.position(1));
    EXPECT_LT(e.position(1), e.position(2));
    for (int i = 0; i < 80; ++i)
        e.press(juce::KeyPress::rightKey, kScale);
    EXPECT_LT(e.position(0), e.position(1));
    EXPECT_LT(e.position(1), e.position(2));
    EXPECT_GE(e.position(0), 0.0);
}

TYPED_TEST(PointEditorSharedKeys, CtrlAltUpStopsAtTheRangeEdgeAndThenOpensNoUndoStep) {
    auto& e = this->editor;
    selectAllThree(e);
    for (int i = 0; i < 80; ++i)
        e.press(juce::KeyPress::upKey, kScale);
    const double low = std::min({e.value(0), e.value(1), e.value(2)});
    const double high = std::max({e.value(0), e.value(1), e.value(2)});
    EXPECT_LE(high, e.rangeHigh());
    EXPECT_GE(low, e.rangeLow());
    const int steps = e.editSteps();
    EXPECT_TRUE(e.press(juce::KeyPress::upKey, kScale));
    EXPECT_EQ(e.editSteps(), steps) << "everything that can spread is at its edge: no empty undo step";
}

TYPED_TEST(PointEditorSharedKeys, CtrlAltArrowsWithASingleDotSelectedDoNothing) {
    auto& e = this->editor;
    ASSERT_TRUE(e.press(juce::KeyPress::rightKey));
    ASSERT_EQ(e.selected(), 1);
    const double value = e.value(0), position = e.position(0);
    const int steps = e.editSteps();
    for (const int code :
         {juce::KeyPress::upKey, juce::KeyPress::downKey, juce::KeyPress::leftKey, juce::KeyPress::rightKey})
        e.press(code, kScale);
    EXPECT_EQ(e.value(0), value);
    EXPECT_EQ(e.position(0), position);
    EXPECT_EQ(e.editSteps(), steps);
}

TYPED_TEST(PointEditorSharedKeys, ShiftMakesTheScaleStepFiner) {
    auto& e = this->editor;
    selectAllThree(e);
    const double before = spreadOfValues(e);
    EXPECT_TRUE(e.press(juce::KeyPress::upKey, kScale | kShift));
    EXPECT_NEAR(spreadOfValues(e), before * 1.02, 1e-4);
}

TEST(LaneScaleKeys, SpreadingInTimeNeverPassesAnUnselectedPointAndStopsAtBeatZero) {
    LaneAdapter e;
    e.doc.addBreakpoint(e.lane, 3.1, 10.0); // an unselected point just after the third
    selectAllThree(e);
    for (int i = 0; i < 40; ++i)
        e.press(juce::KeyPress::rightKey, kScale);
    const auto& pts = e.doc.getLane(e.lane)->points;
    ASSERT_EQ(pts.size(), 4u);
    EXPECT_LT(pts[2].beat, pts[3].beat) << "the neighbour outside the selection is never crossed";
    EXPECT_EQ(pts[3].beat, 3.1) << "and is not pushed";
    for (int i = 0; i < 40; ++i)
        e.press(juce::KeyPress::leftKey, kScale);
    EXPECT_GE(e.doc.getLane(e.lane)->points[0].beat, 0.0);
}

TEST(LaneScaleKeys, CtrlAltUpIsOneUndoStepPerPressInTheRealLane) {
    LaneAdapter e;
    selectAllThree(e);
    const double before = e.value(1) - e.value(0);
    e.press(juce::KeyPress::upKey, kScale);
    e.press(juce::KeyPress::upKey, kScale);
    e.undoLast();
    EXPECT_NEAR(e.value(1) - e.value(0), before * 1.1, 1e-6) << "undo takes back only the second press";
    e.undoLast();
    EXPECT_NEAR(e.value(1) - e.value(0), before, 1e-6);
}
