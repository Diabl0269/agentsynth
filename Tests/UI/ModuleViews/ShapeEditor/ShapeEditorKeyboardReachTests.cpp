// ShapeEditorKeyboardReachTests.cpp -- the keys of docs/control/point-editing.md reach the shape editor window's
// focus stops and do what the doc says, delivered in the order a real key takes (the focused component, then each
// parent up to the window content) rather than by calling one control's keyPressed.
#include "ShapeEditorTestHelpers.h"
#include "UI/ModuleViews/CurveEditor/CurveEditorGeometry.h"
#include "UI/ModuleViews/ShapeEditor/ShapeListEditor.h"

using namespace synth::ui;
using namespace synth::ui::test;

namespace {

constexpr int kCtrl = juce::ModifierKeys::ctrlModifier;

bool dispatchKey(juce::Component& origin, const juce::KeyPress& press) {
    for (auto* target = &origin; target != nullptr; target = target->getParentComponent())
        if (target->keyPressed(press))
            return true;
    return false;
}

juce::KeyPress plain(int code) { return key(code); }
juce::KeyPress withMods(int code, int mods) { return key(code, juce::ModifierKeys(mods)); }

struct Rig {
    ShapeRig shapes;
    ShapeListEditor editor{shapes.adapter, 0};
    Rig() { editor.setSize(ShapeListEditor::kWidth, ShapeListEditor::kPerNoteHeight); }

    int numShapes() { return (int)shapes.adapter->getShapes().size(); }
    juce::Component& dot(int i) { return *editor.getMap().getDotStop(i); }
    CurveEditorComponent& curve() { return editor.getCurveEditor(); }

    void clickCurveNode(int index) {
        auto& c = curve();
        const auto at = CurveEditorGeometry(c.getModel(), c.getLocalBounds().toFloat()).nodePosition(index);
        c.mouseDown(curveLeftClick(c, at));
        c.mouseUp(curveLeftClick(c, at));
    }
};

} // namespace

// Regression test for FRO1185: Ctrl+T added a dot only on a curve dot reached by keyboard, and did nothing, silently,
// on a map dot or after a mouse click
TEST(ShapeEditorKeyboardReach, CtrlTOnACurveDotAfterAMouseClickInserts) {
    Rig rig;
    ASSERT_GE(rig.curve().getModel().getNumNodes(), 2);
    const int before = rig.curve().getModel().getNumNodes();
    rig.clickCurveNode(0);
    ASSERT_EQ(rig.curve().getSelectedIndex(), 0) << "a click puts the cursor on the dot";

    EXPECT_TRUE(dispatchKey(rig.curve(), withMods('T', kCtrl)));

    EXPECT_EQ(rig.curve().getModel().getNumNodes(), before + 1);
    EXPECT_EQ(rig.curve().getSelectedIndex(), 1);
    EXPECT_EQ(rig.editor.getStatusText(), "");
}

TEST(ShapeEditorKeyboardReach, CtrlTOnTheLastCurveDotInsertsTowardTheEndOfTheRange) {
    Rig rig;
    const int last = rig.curve().getModel().getNumNodes() - 1;
    rig.clickCurveNode(last);
    const int before = rig.curve().getModel().getNumNodes();
    const bool room = rig.curve().insertRefusal().isEmpty();
    EXPECT_TRUE(dispatchKey(rig.curve(), withMods('T', kCtrl)));
    EXPECT_EQ(rig.curve().getModel().getNumNodes(), before + (room ? 1 : 0));
}

TEST(ShapeEditorKeyboardReach, CtrlTOnACurveThatCannotInsertSaysWhy) {
    Rig rig;
    auto model = rig.curve().getModel();
    for (int i = 0; model.getNumNodes() < CurveModel::kMaxNodes && i < 4 * CurveModel::kMaxNodes; ++i)
        model.addPoint(
            model.getMinX() + (model.getMaxX() - model.getMinX()) * (i + 0.5) / (4.0 * CurveModel::kMaxNodes), 0.5f);
    ASSERT_EQ(model.getNumNodes(), CurveModel::kMaxNodes);
    rig.curve().setModel(model);
    rig.clickCurveNode(0);
    const int before = rig.curve().getModel().getNumNodes();

    EXPECT_TRUE(dispatchKey(rig.curve(), withMods('T', kCtrl)));

    EXPECT_EQ(rig.curve().getModel().getNumNodes(), before);
    EXPECT_EQ(rig.editor.getStatusText(), "No room: the curve has the most points it can hold");
}

TEST(ShapeEditorKeyboardReach, CtrlTOnAMapDotAddsAShapeHalfwayToTheNextDotAsOneUndoStep) {
    Rig rig;
    ASSERT_EQ(rig.numShapes(), 3);
    const auto before = rig.shapes.adapter->getShapes();
    const auto fromX = before[0].position[0], toX = before[1].position[0];
    const auto fromY = before[0].position[1], toY = before[1].position[1];
    const int serial = rig.shapes.undo.getEditSerial();

    EXPECT_TRUE(dispatchKey(rig.dot(0), withMods('T', kCtrl)));

    ASSERT_EQ(rig.numShapes(), 4);
    const auto after = rig.shapes.adapter->getShapes();
    EXPECT_NEAR(after[1].position[0], (fromX + toX) * 0.5f, 1e-4f);
    EXPECT_NEAR(after[1].position[1], (fromY + toY) * 0.5f, 1e-4f);
    EXPECT_EQ(after[2].name, before[1].name) << "the old second shape follows the new one";
    EXPECT_EQ(rig.shapes.undo.getEditSerial(), serial + 1);
    EXPECT_EQ(rig.editor.getSelected(), 1);
    EXPECT_EQ(rig.editor.getMap().getNumDots(), 4);
    EXPECT_EQ(rig.editor.getStatusText(), "");
    ASSERT_TRUE(rig.shapes.undo.undo());
    EXPECT_EQ(rig.numShapes(), 3);
}

TEST(ShapeEditorKeyboardReach, CtrlTOnTheLastMapDotSaysSo) {
    Rig rig;
    EXPECT_TRUE(dispatchKey(rig.dot(2), withMods('T', kCtrl)));
    EXPECT_EQ(rig.numShapes(), 3);
    EXPECT_EQ(rig.editor.getStatusText(), "This is the last dot");
}

TEST(ShapeEditorKeyboardReach, CtrlTOnAMapDotWithAFullListSaysThereIsNoRoom) {
    Rig rig;
    while (rig.numShapes() < rig.shapes.adapter->getMaxShapes())
        ASSERT_GE(rig.editor.getController().addShapeInFreeSpot(), 0);
    const int full = rig.numShapes();

    EXPECT_TRUE(dispatchKey(rig.dot(0), withMods('T', kCtrl)));

    EXPECT_EQ(rig.numShapes(), full);
    EXPECT_EQ(rig.editor.getStatusText(), "No room for another shape");
}

TEST(ShapeEditorKeyboardReach, CmdTStillReachesTheAppOnTheMac) {
#if JUCE_MAC
    Rig rig;
    int reached = 0;
    rig.editor.onAppShortcut = [&](const juce::KeyPress&) {
        ++reached;
        return true;
    };
    const auto cmdT = withMods('T', juce::ModifierKeys::commandModifier);
    EXPECT_TRUE(dispatchKey(rig.dot(0), cmdT));
    EXPECT_TRUE(dispatchKey(rig.curve(), cmdT));
    EXPECT_EQ(reached, 2);
    EXPECT_EQ(rig.numShapes(), 3);
#else
    GTEST_SKIP() << "off the Mac Cmd is Ctrl";
#endif
}

TEST(ShapeEditorKeyboardReach, DeleteAndTheArrowsStillWorkOnTheMapDots) {
    Rig rig;
    const float x = rig.shapes.adapter->getShapes()[0].position[0];
    EXPECT_TRUE(dispatchKey(rig.dot(0), plain(juce::KeyPress::rightKey)));
    EXPECT_GT(rig.shapes.adapter->getShapes()[0].position[0], x);

    EXPECT_TRUE(dispatchKey(rig.dot(1), plain(juce::KeyPress::deleteKey)));
    EXPECT_EQ(rig.numShapes(), 2);
}

TEST(ShapeEditorKeyboardReach, DeleteAndTheArrowsStillWorkOnTheCurve) {
    Rig rig;
    rig.clickCurveNode(1);
    const int before = rig.curve().getModel().getNumNodes();
    EXPECT_TRUE(dispatchKey(rig.curve(), plain(juce::KeyPress::leftKey)));
    EXPECT_EQ(rig.curve().getSelectedIndex(), 0);
    EXPECT_TRUE(dispatchKey(rig.curve(), plain(juce::KeyPress::rightKey)));
    EXPECT_EQ(rig.curve().getSelectedIndex(), 1);
    EXPECT_TRUE(dispatchKey(rig.curve(), withMods(juce::KeyPress::upKey, juce::ModifierKeys::altModifier)));
    EXPECT_TRUE(dispatchKey(rig.curve(), plain(juce::KeyPress::deleteKey)));
    EXPECT_EQ(rig.curve().getModel().getNumNodes(), before - 1);
}

TEST(ShapeEditorKeyboardReach, EscapeClosesFromEveryFocusStop) {
    Rig rig;
    int closed = 0;
    rig.editor.onDone = [&] { ++closed; };
    std::vector<juce::Component*> stops{&rig.curve(), &rig.editor.getMap(), &rig.editor.getAddButton(),
                                        &rig.editor.getDoneButton()};
    for (int i = 0; i < rig.numShapes(); ++i)
        stops.push_back(&rig.dot(i));
    stops.push_back(rig.editor.getMap().getCursorStop());
    for (auto* stop : stops)
        EXPECT_TRUE(dispatchKey(*stop, plain(juce::KeyPress::escapeKey)));
    EXPECT_EQ(closed, (int)stops.size());
}
