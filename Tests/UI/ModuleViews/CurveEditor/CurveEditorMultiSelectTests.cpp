// CurveEditorMultiSelectTests.cpp
// Selecting several curve points and acting on them together: Shift+arrows extend and shrink the
// selection, Alt+arrows and a mouse drag move the group rigidly as one gesture, Delete removes it, and
// Cmd+A / C / V / D select, copy, paste and duplicate it.

#include "../../Layout/KeyGlide/KeyGlideTestHost.h"
#include "CurveEditorTestHelpers.h"
#include "UI/ModuleViews/ModuleViewAccessibility.h"
#include <gtest/gtest.h>

using namespace synth::ui;
using namespace synth::ui::test;

namespace {

constexpr int kShift = juce::ModifierKeys::shiftModifier;
constexpr int kAlt = juce::ModifierKeys::altModifier;
constexpr int kCmd = juce::ModifierKeys::commandModifier;
constexpr int kCtrl = juce::ModifierKeys::ctrlModifier;

juce::KeyPress key(int code, int mods = 0) { return juce::KeyPress(code, juce::ModifierKeys(mods), 0); }

// Pinned ends (time fixed, level free) around `interior` evenly spaced points.
CurveModel freeModel(int interior) {
    CurveModel model(CurveMode::Free);
    std::vector<CurveNode> nodes;
    nodes.push_back(CurveNode{0.0, 0.5f, false, true});
    for (int i = 1; i <= interior; ++i)
        nodes.push_back(CurveNode{(double)i / (double)(interior + 1), 0.5f, true, true});
    nodes.push_back(CurveNode{1.0, 0.5f, false, true});
    model.setNodes(nodes);
    return model;
}

struct Rig {
    CurveEditorComponent editor;
    std::vector<juce::String> events;

    explicit Rig(CurveModel model) {
        editor.setBounds(0, 0, 400, 160);
        editor.setModel(std::move(model));
        editor.onGestureStart = [this] { events.push_back("start"); };
        editor.onGestureEnd = [this] { events.push_back("end"); };
        editor.onNodeChanged = [this](int) { events.push_back("node"); };
        editor.onPointsChanged = [this] { events.push_back("points"); };
    }
    bool press(int code, int mods = 0) { return editor.keyPressed(key(code, mods)); }
    const CurveNode& node(int i) const { return editor.getModel().getNode(i); }
    std::vector<int> selected() const { return editor.getSelectedIndices(); }
    juce::Point<float> posOf(int i) const {
        return CurveEditorGeometry(editor.getModel(), editor.getLocalBounds().toFloat()).nodePosition(i);
    }
    // Selects interior points 1 and 2 of a `freeModel(3)` with the keyboard.
    void selectFirstTwoInterior() {
        press(juce::KeyPress::rightKey);
        press(juce::KeyPress::rightKey);
        press(juce::KeyPress::rightKey, kShift);
    }
    void click(juce::Point<float> p, int mods = 0) {
        const auto m = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | mods);
        editor.mouseDown(makeCurveMouseEvent(editor, p, m, false, p));
        editor.mouseUp(makeCurveMouseEvent(editor, p, m, false, p));
    }
};

} // namespace

TEST(CurveEditorMultiSelect, ShiftRightExtendsAndShiftLeftShrinksAndPinnedPointsNeverJoin) {
    Rig rig(buildEnvelopeModel());
    rig.press(juce::KeyPress::rightKey);
    ASSERT_EQ(rig.selected(), (std::vector<int>{1}));
    EXPECT_TRUE(rig.press(juce::KeyPress::rightKey, kShift));
    EXPECT_EQ(rig.selected(), (std::vector<int>{1, 2}));
    rig.press(juce::KeyPress::rightKey, kShift);
    EXPECT_EQ(rig.selected(), (std::vector<int>{1, 2, 3}));
    EXPECT_EQ(rig.editor.getSelectedIndex(), 3) << "the cursor is the end that moved";
    rig.press(juce::KeyPress::leftKey, kShift);
    EXPECT_EQ(rig.selected(), (std::vector<int>{1, 2}));
    rig.press(juce::KeyPress::leftKey, kShift);
    rig.press(juce::KeyPress::leftKey, kShift);
    EXPECT_EQ(rig.selected(), (std::vector<int>{1})) << "the pinned origin is never selected";
    EXPECT_TRUE(rig.events.empty()) << "selecting edits nothing";
}

TEST(CurveEditorMultiSelect, APlainArrowCollapsesTheSelectionToOnePoint) {
    Rig rig(freeModel(3));
    rig.selectFirstTwoInterior();
    ASSERT_EQ(rig.selected().size(), 2u);
    rig.press(juce::KeyPress::rightKey);
    EXPECT_EQ(rig.selected(), (std::vector<int>{3}));
}

TEST(CurveEditorMultiSelect, AltRightMovesEverySelectedPointTogetherAsOneGestureAndStopsAtANeighbour) {
    Rig rig(freeModel(3)); // interior points at 0.25, 0.5, 0.75
    rig.selectFirstTwoInterior();
    ASSERT_EQ(rig.selected(), (std::vector<int>{1, 2}));

    EXPECT_TRUE(rig.press(juce::KeyPress::rightKey, kAlt));
    EXPECT_GT(rig.node(1).x, 0.25);
    EXPECT_GT(rig.node(2).x, 0.5);
    EXPECT_NEAR(rig.node(2).x - rig.node(1).x, 0.25, 1e-9) << "rigid: the spacing is kept";
    EXPECT_EQ(rig.events, (std::vector<juce::String>{"start", "node", "node", "end"}));

    for (int i = 0; i < 80; ++i)
        rig.press(juce::KeyPress::rightKey, kAlt);
    EXPECT_NEAR(rig.node(2).x, 0.75, 1e-9) << "the leading point stops at the unselected neighbour";
    EXPECT_NEAR(rig.node(1).x, 0.5, 1e-9);
    EXPECT_EQ(rig.editor.getModel().getNumNodes(), 5) << "nothing crossed a neighbour";
    rig.events.clear();
    rig.press(juce::KeyPress::rightKey, kAlt);
    EXPECT_TRUE(rig.events.empty()) << "a clamped press records no undo step";

    rig.press(juce::KeyPress::upKey, kAlt);
    EXPECT_NEAR(rig.node(1).y, 0.55f, 1e-5f);
    EXPECT_NEAR(rig.node(2).y, 0.55f, 1e-5f);
    EXPECT_FLOAT_EQ(rig.node(3).y, 0.5f) << "an unselected point stays";
}

TEST(CurveEditorMultiSelect, ShiftClickTogglesAPointWithoutMovingIt) {
    Rig rig(freeModel(3));
    const auto first = rig.posOf(1);
    const auto second = rig.posOf(2);
    rig.click(first);
    EXPECT_EQ(rig.selected(), (std::vector<int>{1}));
    rig.click(second, kShift);
    EXPECT_EQ(rig.selected(), (std::vector<int>{1, 2}));
    rig.click(first, kShift);
    EXPECT_EQ(rig.selected(), (std::vector<int>{2})) << "a second Shift-click removes it";
    rig.click(rig.posOf(3), kCmd);
    EXPECT_EQ(rig.selected(), (std::vector<int>{2, 3})) << "Cmd toggles too";
    EXPECT_TRUE(rig.events.empty());
    EXPECT_NEAR(rig.node(1).x, 0.25, 1e-12);
}

TEST(CurveEditorMultiSelect, DraggingOneOfSeveralSelectedPointsMovesThemAllAsOneGesture) {
    Rig rig(freeModel(3));
    rig.click(rig.posOf(1));
    rig.click(rig.posOf(2), kShift);
    ASSERT_EQ(rig.selected(), (std::vector<int>{1, 2}));
    const auto start = rig.posOf(2);
    const double x1 = rig.node(1).x;
    const double x2 = rig.node(2).x;
    const auto left = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier);

    rig.editor.mouseDown(makeCurveMouseEvent(rig.editor, start, left, false, start));
    const juce::Point<float> to{start.x + 15.0f, start.y - 30.0f};
    rig.editor.mouseDrag(curveLeftDrag(rig.editor, to, start));
    rig.editor.mouseUp(curveLeftDrag(rig.editor, to, start));

    EXPECT_GT(rig.node(1).x, x1);
    EXPECT_GT(rig.node(2).x, x2);
    EXPECT_NEAR(rig.node(1).x - x1, rig.node(2).x - x2, 1e-9) << "the same shift for both";
    EXPECT_GT(rig.node(1).y, 0.5f);
    EXPECT_NEAR(rig.node(1).y, rig.node(2).y, 1e-6f);
    EXPECT_EQ(rig.selected(), (std::vector<int>{1, 2})) << "the group stays selected";
    EXPECT_EQ(std::count(rig.events.begin(), rig.events.end(), juce::String("start")), 1);
    EXPECT_EQ(std::count(rig.events.begin(), rig.events.end(), juce::String("end")), 1);
}

TEST(CurveEditorMultiSelect, APlainClickOnOneOfSeveralCollapsesToItButADragDoesNot) {
    Rig rig(freeModel(3));
    rig.selectFirstTwoInterior();
    rig.click(rig.posOf(1));
    EXPECT_EQ(rig.selected(), (std::vector<int>{1}));
}

TEST(CurveEditorMultiSelect, DeleteRemovesEverySelectedPointInFreeModeOnly) {
    Rig rig(freeModel(3));
    rig.selectFirstTwoInterior();
    EXPECT_TRUE(rig.press(juce::KeyPress::deleteKey));
    EXPECT_EQ(rig.editor.getModel().getNumNodes(), 3);
    EXPECT_NEAR(rig.node(1).x, 0.75, 1e-12);
    EXPECT_EQ(rig.events, (std::vector<juce::String>{"start", "points", "end"}));
    EXPECT_EQ(rig.editor.getSelectedIndex(), 1) << "the cursor goes to the next remaining point";
    EXPECT_EQ(rig.selected(), (std::vector<int>{1}));

    Rig fixed(buildEnvelopeModel());
    fixed.press(juce::KeyPress::rightKey);
    fixed.press(juce::KeyPress::rightKey, kShift);
    EXPECT_FALSE(fixed.press(juce::KeyPress::deleteKey)) << "a Fixed curve leaves Delete to the app";
    EXPECT_EQ(fixed.editor.getModel().getNumNodes(), 5);
}

TEST(CurveEditorMultiSelect, CommandACommandCAndCommandVStayWithTheAppWhenTheyHaveNothingToDo) {
    Rig rig(freeModel(3));
    EXPECT_FALSE(rig.press('C', kCmd)) << "nothing selected: Copy goes to the app";
    EXPECT_FALSE(rig.press('D', kCmd));
    rig.selectFirstTwoInterior();
    EXPECT_TRUE(rig.press('C', kCmd));

    Rig fixed(buildEnvelopeModel());
    fixed.press(juce::KeyPress::rightKey);
    EXPECT_FALSE(fixed.press('V', kCmd)) << "a Fixed curve cannot take pasted points";
    EXPECT_FALSE(fixed.press('D', kCmd));
}

TEST(CurveEditorMultiSelect, CommandASelectsEveryMovablePoint) {
    Rig rig(buildEnvelopeModel());
    EXPECT_TRUE(rig.press('A', kCmd));
    EXPECT_EQ(rig.selected(), (std::vector<int>{1, 2, 3, 4})) << "the pinned origin stays out";
}

TEST(CurveEditorMultiSelect, CopyThenPasteLandsAfterTheCursorKeepingTheSpacingAsOneGesture) {
    Rig rig(freeModel(3)); // 0.25, 0.5, 0.75
    rig.selectFirstTwoInterior();
    ASSERT_TRUE(rig.press('C', kCmd));
    rig.press(juce::KeyPress::rightKey); // the cursor on the last interior point, x 0.75
    rig.events.clear();
    ASSERT_TRUE(rig.press('V', kCmd));

    EXPECT_EQ(rig.editor.getModel().getNumNodes(), 7);
    EXPECT_EQ(rig.events, (std::vector<juce::String>{"start", "points", "end"}));
    const auto picked = rig.selected();
    ASSERT_EQ(picked.size(), 2u);
    EXPECT_GE(rig.node(picked[0]).x, 0.75);
    EXPECT_LE(rig.node(picked[1]).x, 1.0);
    EXPECT_NEAR(rig.node(picked[1]).x - rig.node(picked[0]).x, 0.25, 1e-9) << "the spacing is kept";
    EXPECT_EQ(rig.editor.getSelectedIndex(), picked.back());
}

TEST(CurveEditorMultiSelect, DuplicateCopiesTheSelectionRightAfterItsLastPointAsOneGesture) {
    Rig rig(freeModel(3));
    rig.selectFirstTwoInterior();
    rig.events.clear();
    ASSERT_TRUE(rig.press('D', kCmd));

    EXPECT_EQ(rig.editor.getModel().getNumNodes(), 7);
    EXPECT_EQ(rig.events, (std::vector<juce::String>{"start", "points", "end"}));
    const auto picked = rig.selected();
    ASSERT_EQ(picked.size(), 2u);
    EXPECT_GT(rig.node(picked[0]).x, 0.5) << "just after the original group";
    EXPECT_NEAR(rig.node(picked[1]).x - rig.node(picked[0]).x, 0.25, 1e-9);
    EXPECT_NEAR(rig.node(1).x, 0.25, 1e-12) << "the originals stay";
}

TEST(CurveEditorMultiSelect, ThePasteIsClampedToTheRangeAndToTheRoomLeft) {
    Rig rig(freeModel(3));
    rig.selectFirstTwoInterior();
    rig.press('C', kCmd);
    rig.press(juce::KeyPress::rightKey);
    rig.press(juce::KeyPress::rightKey); // the pinned end, x 1.0
    ASSERT_TRUE(rig.press('V', kCmd));
    for (const int i : rig.selected()) {
        EXPECT_GE(rig.node(i).x, 0.0);
        EXPECT_LE(rig.node(i).x, 1.0);
    }

    CurveModel full(CurveMode::Free);
    std::vector<CurveNode> nodes;
    for (int i = 0; i < CurveModel::kMaxNodes; ++i)
        nodes.push_back(
            CurveNode{(double)i / (CurveModel::kMaxNodes - 1), 0.5f, i != 0 && i != CurveModel::kMaxNodes - 1, true});
    full.setNodes(nodes);
    Rig packed(std::move(full));
    packed.press(juce::KeyPress::rightKey);
    packed.press(juce::KeyPress::rightKey);
    packed.press('C', kCmd);
    EXPECT_FALSE(packed.press('V', kCmd)) << "a full curve takes nothing more";
}

TEST(CurveEditorMultiSelect, TheSpokenValueSaysHowManyPointsAreSelected) {
    Rig rig(freeModel(3));
    rig.selectFirstTwoInterior();
    EXPECT_EQ(rig.editor.getAccessibilityValueText(), "2 points selected, Point 3 of 5, time 0.5, level 0.50");
}

// Regression test for FRO957: plain Up/Down did nothing in the curve editor while the lanes moved the value.
TEST(CurveEditorMultiSelect, PlainUpAndDownMoveTheSelectionsLevelLikeAltUpAndDownAsOneGesture) {
    Rig rig(freeModel(3));
    EXPECT_FALSE(rig.press(juce::KeyPress::upKey)) << "nothing selected: the key stays with the app";
    rig.press(juce::KeyPress::rightKey);
    rig.press(juce::KeyPress::rightKey);
    ASSERT_EQ(rig.selected(), (std::vector<int>{1}));
    rig.events.clear();

    EXPECT_TRUE(rig.press(juce::KeyPress::upKey));
    EXPECT_NEAR(rig.node(1).y, 0.55f, 1e-5f);
    EXPECT_NEAR(rig.node(1).x, 0.25, 1e-12) << "nothing moves in time";
    EXPECT_EQ(rig.events, (std::vector<juce::String>{"start", "node", "end"}));
    EXPECT_TRUE(rig.press(juce::KeyPress::downKey));
    EXPECT_NEAR(rig.node(1).y, 0.5f, 1e-5f);

    Rig alt(freeModel(3));
    alt.press(juce::KeyPress::rightKey);
    alt.press(juce::KeyPress::rightKey);
    alt.press(juce::KeyPress::upKey, kAlt);
    EXPECT_NEAR(alt.node(1).y, 0.55f, 1e-5f) << "the same step as Alt+Up";
}

// Regression test for FRO957: a point could only be added with the mouse.
TEST(CurveEditorMultiSelect, CtrlTInsertsAPointHalfwayAfterTheCursorSelectsItAndIsOneUndoStep) {
    Rig rig(freeModel(3)); // 0, 0.25, 0.5, 0.75, 1
    EXPECT_FALSE(rig.press('T', kCtrl)) << "no cursor point: nothing to insert after";
    rig.press(juce::KeyPress::rightKey);
    rig.press(juce::KeyPress::rightKey); // the cursor on x 0.25
    rig.events.clear();

    EXPECT_TRUE(rig.press('T', kCtrl));
    ASSERT_EQ(rig.editor.getModel().getNumNodes(), 6);
    EXPECT_NEAR(rig.node(2).x, 0.375, 1e-12);
    EXPECT_NEAR(rig.node(2).y, 0.5f, 1e-5f);
    EXPECT_EQ(rig.selected(), (std::vector<int>{2})) << "the new point is the selection and the cursor";
    EXPECT_EQ(rig.editor.getSelectedIndex(), 2);
    EXPECT_EQ(rig.events, (std::vector<juce::String>{"start", "points", "end"})) << "one undo step";
}

TEST(CurveEditorMultiSelect, CtrlTHalvesTheLevelBetweenTwoPointsAndRefusesWhereThereIsNoRoom) {
    CurveModel model(CurveMode::Free);
    model.setNodes({CurveNode{0.0, 0.2f, false, true}, CurveNode{1.0, 0.8f, false, true}});
    Rig rig(std::move(model));
    rig.press(juce::KeyPress::rightKey); // the first point
    ASSERT_TRUE(rig.press('T', kCtrl));
    EXPECT_NEAR(rig.node(1).x, 0.5, 1e-12);
    EXPECT_NEAR(rig.node(1).y, 0.5f, 1e-5f) << "the linear midpoint of 0.2 and 0.8";

    rig.press(juce::KeyPress::rightKey);
    rig.press(juce::KeyPress::rightKey); // the last point: the range ends there
    ASSERT_EQ(rig.editor.getSelectedIndex(), 2);
    rig.events.clear();
    EXPECT_FALSE(rig.press('T', kCtrl));
    EXPECT_TRUE(rig.events.empty()) << "a refused press opens no undo step";
}

TEST(CurveEditorMultiSelect, CtrlTDoesNothingOnAFixedCurveAndCommandTStaysWithTheApp) {
    Rig fixed(buildEnvelopeModel());
    fixed.press(juce::KeyPress::rightKey);
    fixed.events.clear();
    EXPECT_FALSE(fixed.press('T', kCtrl));
    EXPECT_EQ(fixed.editor.getModel().getNumNodes(), 5);
    EXPECT_TRUE(fixed.events.empty());

#if JUCE_MAC
    Rig rig(freeModel(3));
    rig.press(juce::KeyPress::rightKey);
    EXPECT_FALSE(rig.press('T', kCmd)) << "Cmd+T is the app's Add Track";
    EXPECT_EQ(rig.editor.getModel().getNumNodes(), 5);
#endif
}

//==============================================================================
// Holding an arrow: the selection glides and the whole hold is one gesture

namespace {
// A rig whose arrows are "physically" held through the editor's key-hold host.
struct HeldRig : Rig {
    key_glide_test::FakeKeys keys;

    explicit HeldRig(CurveModel model)
        : Rig(std::move(model)) {
        editor.getKeyHoldForTest().host = keys.host();
    }
    void hold(int code, int mods = 0) {
        keys.down = {code};
        keys.alt = (mods & kAlt) != 0;
        keys.shift = (mods & kShift) != 0;
        EXPECT_TRUE(press(code, mods));
    }
    void frame(double glideMs) {
        keys.now = 1000.0 + 150.0 + glideMs;
        editor.getKeyHoldForTest().tick();
    }
    void release() {
        keys.down.clear();
        editor.getKeyHoldForTest().checkKeyUp();
    }
    int count(const juce::String& event) const { return (int)std::count(events.begin(), events.end(), event); }
};
} // namespace

TEST(CurveEditorKeyHold, HoldingUpMovesTheLevelContinuouslyAcceleratesAndIsOneGesture) {
    HeldRig rig(freeModel(3));
    rig.press(juce::KeyPress::rightKey);
    rig.press(juce::KeyPress::rightKey);
    rig.events.clear();
    const float start = rig.node(1).y;

    rig.hold(juce::KeyPress::upKey);
    const float afterTap = rig.node(1).y;
    EXPECT_NEAR(afterTap - start, 0.05f, 1e-5f) << "the tap is the existing one step";
    rig.frame(0.0);
    rig.frame(500.0);
    const float half = rig.node(1).y;
    rig.frame(1000.0);
    const float full = rig.node(1).y;
    EXPECT_GT(half, afterTap);
    EXPECT_GT(full - half, half - afterTap) << "the second half covers more than the first";
    EXPECT_NE(std::fmod((double)full, 0.05), 0.0) << "continuous, not in steps";

    EXPECT_EQ(rig.count("start"), 1);
    EXPECT_EQ(rig.count("end"), 0) << "the gesture stays open while the key is down";
    rig.release();
    EXPECT_EQ(rig.count("start"), 1);
    EXPECT_EQ(rig.count("end"), 1) << "one undo step for the tap and the hold";
    EXPECT_FALSE(rig.editor.getKeyHoldForTest().active());
}

TEST(CurveEditorKeyHold, ATapIsOneStepAndAHoldAgainstALimitOpensNoGesture) {
    HeldRig rig(freeModel(3));
    rig.press(juce::KeyPress::rightKey);
    rig.press(juce::KeyPress::rightKey);
    rig.events.clear();
    EXPECT_TRUE(rig.press(juce::KeyPress::upKey)) << "no key physically down: a plain step";
    EXPECT_EQ(rig.count("start"), 1);
    EXPECT_EQ(rig.count("end"), 1);
    EXPECT_FALSE(rig.editor.getKeyHoldForTest().active());

    HeldRig edge(freeModel(3));
    edge.press(juce::KeyPress::rightKey);
    edge.press(juce::KeyPress::rightKey);
    edge.hold(juce::KeyPress::upKey);
    for (int i = 0; i < 4; ++i)
        edge.frame(1000.0 * (i + 1));
    ASSERT_NEAR(edge.node(1).y, 1.0f, 1e-6f);
    edge.events.clear();
    edge.release();
    EXPECT_EQ(edge.count("end"), 1) << "it moved before the limit, so the gesture closes";

    HeldRig pinned(freeModel(3));
    pinned.press(juce::KeyPress::rightKey);
    pinned.press(juce::KeyPress::rightKey);
    pinned.editor.getModel().setNodeY(1, 1.0f);
    pinned.events.clear();
    pinned.hold(juce::KeyPress::upKey);
    pinned.frame(500.0);
    pinned.release();
    EXPECT_EQ(pinned.count("start"), 0) << "a hold that never moved reaches no undo step";
    EXPECT_EQ(pinned.count("end"), 0);
}

TEST(CurveEditorKeyHold, WithSnapOnAHoldStepsFromGridLineToGridLine) {
    HeldRig rig(freeModel(1));
    rig.editor.setGrid(CurveEditorComponent::CurveGrid{8, 2, 0.0, 0.0});
    rig.editor.setSnapToGrid(true);
    rig.press(juce::KeyPress::rightKey);
    rig.press(juce::KeyPress::rightKey);
    rig.hold(juce::KeyPress::rightKey, kAlt);
    EXPECT_NEAR(rig.node(1).x, 0.625, 1e-9) << "the tap is one grid cell";
    for (const double glideMs : {200.0, 800.0, 1500.0}) {
        rig.frame(glideMs);
        EXPECT_NEAR(rig.node(1).x * 8.0, std::round(rig.node(1).x * 8.0), 1e-9) << "on a grid line at " << glideMs;
    }
    EXPECT_GT(rig.node(1).x, 0.625);
    rig.release();
    EXPECT_EQ(rig.count("end"), 1);
}

TEST(CurveEditorKeyHold, ATapEasesTheDrawnPointAndPlainlyLandsUnderReduceMotion) {
    HeldRig rig(freeModel(3));
    rig.editor.getPointTweenForTest().forceAnimateForTest(true);
    rig.press(juce::KeyPress::rightKey);
    rig.press(juce::KeyPress::rightKey);

    auto& tween = rig.editor.getPointTweenForTest();
    const float before = rig.posOf(1).y;
    rig.press(juce::KeyPress::upKey);
    ASSERT_TRUE(tween.isAnimating());
    EXPECT_GT(tween.offsetFor(1).y, 0.0f) << "drawn below its new place, at the old one";
    EXPECT_NEAR(tween.drawnAt(1, rig.posOf(1)).y, before, 1e-3f);
    tween.stepForTest(40.0);
    EXPECT_GT(tween.offsetFor(1).y, 0.0f);
    EXPECT_LT(tween.offsetFor(1).y, before - rig.posOf(1).y);
    tween.stepForTest(300.0);
    EXPECT_FALSE(tween.isAnimating());

    synth::ui::setReducedMotionForTest(true);
    rig.press(juce::KeyPress::upKey);
    EXPECT_FALSE(tween.isAnimating());
    synth::ui::setReducedMotionForTest(std::nullopt);
}

//==============================================================================
