// CanvasEdgeDragTests.cpp
//
// The left/top half of the growing canvas: a drag past the canvas origin is held at the edge, and the drop slides the
// rest of the patch right/down by the overshoot (one undo step, view panned so nothing jumps on screen). Driven through
// the real mouseDown/mouseDrag/mouseUp handlers. (docs/layout/layout.md#canvas-frame)

#include "../../../Macros/MacroContainer/MacroContainerTestHelpers.h"

#include "AppUndoManager.h"
#include "Modules/OscillatorModule.h"
#include "Project/ViewDoc.h"
#include "UI/Graph/CanvasFrame/CanvasEdgeDrag.h"
#include "UI/Graph/CanvasFrame/CanvasFrame.h"
#include "UI/Macros/MacroCardComponent/MacroCardComponent.h"
#include <gtest/gtest.h>

namespace {

juce::MouseEvent mouseAt(juce::Component& comp, juce::Point<int> pos, juce::Point<int> downPos, bool dragged) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos.toFloat(),
                            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &comp, &comp, juce::Time::getCurrentTime(), downPos.toFloat(), juce::Time::getCurrentTime(),
                            1, dragged);
}

// Press on the component's body, drag it by `by`, release: the whole real gesture.
void dragBy(juce::Component& comp, juce::Point<int> by) {
    const juce::Point<int> press(comp.getWidth() / 2, ModuleComponent::kHeaderHeight + 10);
    comp.mouseDown(mouseAt(comp, press, press, false));
    comp.mouseDrag(mouseAt(comp, press + by, press, true));
    comp.mouseUp(mouseAt(comp, press + by, press, true));
}

struct Canvas {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};

    Canvas() {
        undo.setGraphEditor(&editor);
        editor.setSize(3200, 2400);
    }
    NodeID osc(int x, int y) { return addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), x, y); }
    juce::Point<int> posOf(NodeID id) {
        auto* node = engine.getGraph().getNodeForId(id);
        return {static_cast<int>(node->properties["x"]), static_cast<int>(node->properties["y"])};
    }
    juce::String collapsedMacroAt(int x, int y) {
        editor.setSelectedNodes({osc(x, y), osc(x + 300, y)});
        const auto id = editor.getMacroController().groupSelectionIntoMacro();
        editor.getMacroController().setMacroCollapsed(id, true);
        editor.updateComponents();
        return id;
    }
};

} // namespace

TEST(CanvasEdgeDragClamp, HoldsTheMovingUnionAtTheFloorAndRemembersTheOvershoot) {
    CanvasEdgeDrag drag;
    drag.begin({40, 100, 280, 200}, {0, 0}, {"n:1"});
    EXPECT_EQ(drag.clampDelta({-300, -20}), juce::Point<int>(-40, -20));
    EXPECT_EQ(drag.takeOvershoot(), juce::Point<int>(264, 0)) << "260 px past the edge, rounded up to the 8 px grid";
    EXPECT_EQ(drag.takeOvershoot(), juce::Point<int>()) << "taking it clears it";
    EXPECT_TRUE(drag.isMoving("n:1"));
    EXPECT_FALSE(drag.isMoving("n:2"));
}

TEST(CanvasEdgeDragClamp, AnUnarmedDragPassesThrough) {
    CanvasEdgeDrag drag;
    EXPECT_EQ(drag.clampDelta({-300, -300}), juce::Point<int>(-300, -300));
    EXPECT_EQ(drag.takeOvershoot(), juce::Point<int>());
}

TEST(CanvasEdgeDrag, ACardDraggedPastTheLeftEdgeStaysVisibleAndTheRestSlidesRight) {
    Canvas c;
    const auto dragged = c.osc(40, 40);
    const auto other = c.osc(600, 40);
    const auto macroId = c.collapsedMacroAt(600, 700);
    const auto macroBefore = c.editor.getMacros().find(macroId)->bounds;
    const float panBefore = c.editor.getViewDoc().panX;

    auto* comp = findComponent(c.editor, dragged);
    ASSERT_NE(comp, nullptr);
    const juce::Point<int> press(comp->getWidth() / 2, ModuleComponent::kHeaderHeight + 10);
    comp->mouseDown(mouseAt(*comp, press, press, false));
    comp->mouseDrag(mouseAt(*comp, press + juce::Point<int>(-300, 0), press, true));
    EXPECT_EQ(comp->getX(), 0) << "held at the canvas edge while dragging, never clipped past it";
    comp->mouseUp(mouseAt(*comp, press + juce::Point<int>(-300, 0), press, true));

    EXPECT_EQ(c.posOf(dragged).x, 0);
    EXPECT_EQ(c.posOf(other).x, 600 + 264) << "the rest of the patch slid right by the overshoot";
    EXPECT_EQ(c.editor.getMacros().find(macroId)->bounds.getX(), macroBefore.getX() + 264)
        << "a collapsed macro card slides too";
    EXPECT_FLOAT_EQ(c.editor.getViewDoc().panX, panBefore - 264.0f * c.editor.getViewDoc().zoom)
        << "the view pans with the slide, so nothing jumps on screen";
    auto& frame = c.editor.getCanvasFrameForTest();
    EXPECT_EQ(frame.target().getX(), 0);
    EXPECT_FLOAT_EQ(frame.current().getX(), 264.0f) << "the frame starts where it was on screen and glides out left";

    ASSERT_TRUE(c.undo.undo());
    EXPECT_EQ(c.posOf(dragged), juce::Point<int>(40, 40));
    EXPECT_EQ(c.posOf(other).x, 600);
    EXPECT_EQ(c.editor.getMacros().find(macroId)->bounds, macroBefore) << "one undo step restores the macro card too";
}

TEST(CanvasEdgeDrag, AGroupDraggedPastTheTopEdgeIsHeldAsOne) {
    Canvas c;
    const auto a = c.osc(400, 40);
    const auto b = c.osc(800, 120);
    const auto still = c.osc(400, 800);
    c.editor.setSelectedNodes({a, b});

    auto* comp = findComponent(c.editor, a);
    ASSERT_NE(comp, nullptr);
    dragBy(*comp, {0, -200});

    EXPECT_EQ(c.posOf(a).y, 0);
    EXPECT_EQ(c.posOf(b).y, 80) << "the group keeps its shape";
    EXPECT_EQ(c.posOf(still).y, 800 + 160) << "160 px past the top edge: everything else slid down";
}

TEST(CanvasEdgeDrag, ADragThatStaysOnTheCanvasSlidesNothing) {
    Canvas c;
    const auto dragged = c.osc(400, 400);
    const auto other = c.osc(1000, 400);
    const float panBefore = c.editor.getViewDoc().panX;

    auto* comp = findComponent(c.editor, dragged);
    ASSERT_NE(comp, nullptr);
    dragBy(*comp, {-200, 0});

    EXPECT_EQ(c.posOf(other).x, 1000);
    EXPECT_FLOAT_EQ(c.editor.getViewDoc().panX, panBefore);
}

TEST(CanvasEdgeDrag, ACollapsedMacroCardDraggedPastTheLeftEdgeSlidesTheRest) {
    Canvas c;
    const auto macroId = c.collapsedMacroAt(80, 80);
    const auto other = c.osc(900, 80);

    auto* card = c.editor.getMacroController().getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);
    dragBy(*card, {-200, 0});

    EXPECT_EQ(c.editor.getMacros().find(macroId)->bounds.getX(), 0);
    EXPECT_EQ(c.posOf(other).x, 900 + 120);
}

TEST(CanvasEdgeDrag, TheFrameGrowsDuringTheDragAndShrinksOnlyOnTheDrop) {
    Canvas c;
    const auto dragged = c.osc(400, 400);
    auto* comp = findComponent(c.editor, dragged);
    ASSERT_NE(comp, nullptr);
    auto& frame = c.editor.getCanvasFrameForTest();
    ASSERT_EQ(frame.target().getWidth(), CanvasFrame::kStartW);

    const juce::Point<int> press(comp->getWidth() / 2, ModuleComponent::kHeaderHeight + 10);
    comp->mouseDown(mouseAt(*comp, press, press, false));
    comp->mouseDrag(mouseAt(*comp, press + juce::Point<int>(3000, 0), press, true));
    EXPECT_GE(frame.target().getRight(), comp->getRight() + CanvasFrame::kPad)
        << "the frame steps out ahead of the card while it is dragged, not on the drop";
    // Event positions are relative to the card where it now is, so this brings it back to its start.
    const auto back = press - juce::Point<int>(3000, 0);
    comp->mouseDrag(mouseAt(*comp, back, press, true));
    ASSERT_EQ(comp->getX(), 400);
    EXPECT_GT(frame.target().getWidth(), CanvasFrame::kStartW) << "a live drag never shrinks it";
    comp->mouseUp(mouseAt(*comp, press, press, true));
    EXPECT_EQ(frame.target().getWidth(), CanvasFrame::kStartW) << "the drop fits it back";
}
