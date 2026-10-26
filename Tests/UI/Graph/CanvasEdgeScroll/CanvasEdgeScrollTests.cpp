// CanvasEdgeScrollTests.cpp
//
// Dragging a card to the edge of the canvas pans it: the speed curve on its own, then the real gestures (a module
// card pressed and dragged through its mouse handlers, a library drag through GraphEditor's drag-and-drop
// overrides) with the pointer parked in the edge zone and the scroller ticked by hand.
// (docs/layout/layout.md#edge-scroll-while-dragging)

#include "../../../Macros/MacroContainer/MacroContainerTestHelpers.h"

#include "Modules/OscillatorModule.h"
#include "Project/ViewDoc.h"
#include "UI/Graph/CanvasEdgeScroll/CanvasEdgeScroll.h"
#include "UI/Graph/CanvasEdgeScroll/CanvasEdgeScrollDriver.h"
#include "UI/Macros/MacroCardComponent/MacroCardComponent.h"
#include <gtest/gtest.h>

namespace {

using synth::ui::canvasEdgePanDelta;
using synth::ui::kCanvasEdgeMaxPxPerTick;

juce::MouseEvent mouseAt(juce::Component& comp, juce::Point<int> pos, juce::Point<int> downPos, bool dragged) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos.toFloat(),
                            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &comp, &comp, juce::Time::getCurrentTime(), downPos.toFloat(), juce::Time::getCurrentTime(),
                            1, dragged);
}

struct Canvas {
    AudioEngine engine;
    GraphEditor editor{engine, nullptr};
    juce::Point<float> pointer; // editor-local, what the scroller sees as the mouse

    Canvas() {
        editor.setSize(800, 600);
        editor.getEdgeScroll().setPointerForTest([this] { return pointer; });
    }
    NodeID osc(int x, int y) { return addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), x, y); }
    float panX() const { return editor.getViewDoc().panX; }
    float panY() const { return editor.getViewDoc().panY; }
    void tick(int times) {
        for (int i = 0; i < times; ++i)
            editor.getEdgeScroll().tickForTest();
    }
};

} // namespace

TEST(CanvasEdgePanDelta, IsZeroInTheMiddleAndGrowsTowardTheEdge) {
    const juce::Rectangle<int> view(0, 0, 800, 600);
    EXPECT_EQ(canvasEdgePanDelta({400, 300}, view), juce::Point<float>());
    EXPECT_EQ(canvasEdgePanDelta({800 - synth::ui::kCanvasEdgeZonePx, 300}, view), juce::Point<float>())
        << "the zone starts exactly at its inner border";

    const float nearer = canvasEdgePanDelta({800 - 30, 300}, view).x;
    const float closer = canvasEdgePanDelta({800 - 10, 300}, view).x;
    EXPECT_LT(nearer, 0.0f) << "pointer at the right: content moves left, so the view moves right";
    EXPECT_LT(closer, nearer) << "closer to the edge is faster";
    EXPECT_FLOAT_EQ(canvasEdgePanDelta({800, 300}, view).x, -kCanvasEdgeMaxPxPerTick);
    EXPECT_FLOAT_EQ(canvasEdgePanDelta({5000, 300}, view).x, -kCanvasEdgeMaxPxPerTick) << "off the view is capped";
}

TEST(CanvasEdgePanDelta, MirrorsOnTheOtherSidesAndMovesAxesIndependently) {
    const juce::Rectangle<int> view(0, 0, 800, 600);
    EXPECT_FLOAT_EQ(canvasEdgePanDelta({0, 300}, view).x, kCanvasEdgeMaxPxPerTick);
    EXPECT_FLOAT_EQ(canvasEdgePanDelta({400, 0}, view).y, kCanvasEdgeMaxPxPerTick);
    EXPECT_FLOAT_EQ(canvasEdgePanDelta({400, 600}, view).y, -kCanvasEdgeMaxPxPerTick);
    const auto corner = canvasEdgePanDelta({800, 600}, view);
    EXPECT_FLOAT_EQ(corner.x, -kCanvasEdgeMaxPxPerTick);
    EXPECT_FLOAT_EQ(corner.y, -kCanvasEdgeMaxPxPerTick);
}

TEST(CanvasEdgePanDelta, ATinyViewKeepsADeadMiddle) {
    const juce::Rectangle<int> view(0, 0, 90, 90);
    EXPECT_EQ(canvasEdgePanDelta({45, 45}, view), juce::Point<float>());
}

// Regression test for FRO799: dragging a card to the window edge did not scroll the canvas.
TEST(CanvasEdgeScroll, ACardHeldAtTheRightEdgeScrollsTheCanvasAndKeepsTrackingUnderThePointer) {
    Canvas c;
    const auto id = c.osc(300, 200);
    auto* card = findComponent(c.editor, id);
    ASSERT_NE(card, nullptr);

    const juce::Point<int> press(card->getWidth() / 2, ModuleComponent::kHeaderHeight + 10);
    card->mouseDown(mouseAt(*card, press, press, false));
    // The real drag: to the right edge of the view.
    const juce::Point<int> toEdge(795, 220);
    c.pointer = toEdge.toFloat();
    card->mouseDrag(mouseAt(*card, card->getLocalPoint(&c.editor, toEdge), press, true));
    const float panBefore = c.panX();
    const int xBefore = card->getX();
    ASSERT_GT(xBefore, 300);

    c.tick(10);

    EXPECT_LT(c.panX(), panBefore) << "the canvas scrolled to the right";
    EXPECT_GT(card->getX(), xBefore) << "the card went with it";
    EXPECT_TRUE(card->getBounds().contains(card->getParentComponent()->getLocalPoint(&c.editor, toEdge)))
        << "the card is still under the stationary pointer";
    EXPECT_EQ(card->getY(), 220 - (ModuleComponent::kHeaderHeight + 10)) << "no vertical drift";

    // Back inward: the pan stops.
    c.pointer = {400.0f, 220.0f};
    const float panStopped = c.panX();
    c.tick(5);
    EXPECT_FLOAT_EQ(c.panX(), panStopped);

    // Release ends the scroller for good.
    card->mouseUp(mouseAt(*card, press, press, true));
    EXPECT_FALSE(c.editor.getEdgeScroll().isActive());
    c.pointer = toEdge.toFloat();
    c.tick(5);
    EXPECT_FLOAT_EQ(c.panX(), panStopped);
}

TEST(CanvasEdgeScroll, TheCloserToTheEdgeTheFasterItScrolls) {
    float step[2] = {};
    for (int i = 0; i < 2; ++i) {
        Canvas c;
        const auto id = c.osc(300, 200);
        auto* card = findComponent(c.editor, id);
        const juce::Point<int> press(card->getWidth() / 2, ModuleComponent::kHeaderHeight + 10);
        card->mouseDown(mouseAt(*card, press, press, false));
        c.pointer = {i == 0 ? 770.0f : 797.0f, 220.0f};
        card->mouseDrag(mouseAt(*card, card->getLocalPoint(&c.editor, c.pointer.roundToInt()), press, true));
        const float before = c.panX();
        c.tick(1);
        step[i] = before - c.panX();
        card->mouseUp(mouseAt(*card, press, press, true));
    }
    EXPECT_GT(step[0], 0.0f);
    EXPECT_GT(step[1], step[0]);
}

TEST(CanvasEdgeScroll, TheCanvasNeverScrollsPastItsOriginWhileTheCardIsHeldAtTheLeftEdge) {
    Canvas c;
    const auto id = c.osc(100, 200);
    auto* card = findComponent(c.editor, id);
    const juce::Point<int> press(card->getWidth() / 2, ModuleComponent::kHeaderHeight + 10);
    card->mouseDown(mouseAt(*card, press, press, false));
    c.pointer = {2.0f, 220.0f};
    card->mouseDrag(mouseAt(*card, card->getLocalPoint(&c.editor, c.pointer.roundToInt()), press, true));
    c.tick(10);
    EXPECT_LE(c.panX(), 0.0f) << "the left half of the canvas does not exist; nothing scrolls into it";
    EXPECT_FLOAT_EQ(c.panX(), 0.0f);
    card->mouseUp(mouseAt(*card, press, press, true));
}

// Regression test for FRO799: a card dragged in from the module library did not scroll the canvas either.
TEST(CanvasEdgeScroll, ALibraryDragHeldAtTheBottomEdgeScrollsAndTheGhostFollows) {
    Canvas c;
    c.osc(100, 100);
    struct Source : juce::Component {
    } source;
    juce::DragAndDropTarget::SourceDetails details("Oscillator", &source, juce::Point<int>(400, 590));
    c.pointer = {400.0f, 590.0f};
    c.editor.itemDragEnter(details);
    ASSERT_TRUE(c.editor.getEdgeScroll().isActive());
    const float panBefore = c.panY();
    const int ghostBefore = c.editor.getDragDropController().getDragPreviewGhost().getY();

    c.tick(8);

    EXPECT_LT(c.panY(), panBefore) << "the canvas scrolled down";
    EXPECT_GT(c.editor.getDragDropController().getDragPreviewGhost().getY(), ghostBefore)
        << "the landing ghost stayed under the pointer, so it moved down in canvas space";

    c.editor.itemDragExit(details);
    EXPECT_FALSE(c.editor.getEdgeScroll().isActive());
    const float stopped = c.panY();
    c.tick(3);
    EXPECT_FLOAT_EQ(c.panY(), stopped);
}
