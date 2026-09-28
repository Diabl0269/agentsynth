// ControllerSurfaceViewTests.cpp -- FRO331 (docs/control/midi-remote-ui.md#surface-centre): the
// surface's own pan/zoom, driven through the real mouseDown/mouseDrag/mouseUp/mouseWheelMove/
// mouseMagnify overrides with synthesized juce::MouseEvents (Source/UI/CLAUDE.md's "test the real
// mouse path" convention), mirroring Tests/UI/Graph/GraphEditor/GraphEditorViewportTests.cpp's own
// shape for the module canvas's equivalent behaviour.

#include "ControllerSurfaceTestHelpers.h"

#include <gtest/gtest.h>

using midiremote_surface_test::eightPadsInARowModel;
using midiremote_surface_test::fourControlModel;
using midiremote_surface_test::ScreenSpaceDragDriver;
using midiremote_surface_test::surfaceMouseEvent;
using synth::ui::ControllerSurfaceCell;
using synth::ui::ControllerSurfaceComponent;

namespace {

ControllerSurfaceCell* findCell(ControllerSurfaceComponent& surface, const juce::String& controlId) {
    return midiremote_surface_test::findCell(surface, controlId);
}

// Same shape as ControllerSurfaceSelectionTests.cpp/ControllerSurfaceGroupDragTests.cpp's own
// local `click()` helper -- a real mouseDown+mouseUp with no movement, driving selection through
// the cell's actual click path rather than calling its onSelected callback directly.
void click(ControllerSurfaceCell& cell, juce::ModifierKeys mods = juce::ModifierKeys()) {
    const auto pos = cell.getLocalBounds().getCentre().toFloat();
    cell.mouseDown(surfaceMouseEvent(cell, pos, pos, false, mods));
    cell.mouseUp(surfaceMouseEvent(cell, pos, pos, false, mods));
}

// Maps a surface-local screen point to the content point currently under it, derived purely from
// getVisibleContentRect() -- same construction as GraphEditorViewportTests.cpp's own screenToCanvas.
juce::Point<float> screenToContent(const ControllerSurfaceComponent& surface, juce::Point<float> screenPt) {
    const auto rect = surface.getVisibleContentRect();
    const auto w = (float)surface.getWidth();
    const auto h = (float)surface.getHeight();
    return {rect.getX() + (screenPt.x / w) * rect.getWidth(), rect.getY() + (screenPt.y / h) * rect.getHeight()};
}

} // namespace

// ---- Pan ----------------------------------------------------------------------------------

TEST(ControllerSurfaceViewTest, DragOnEmptySpacePansTheView) {
    ControllerSurfaceComponent surface;
    surface.setSize(400, 400);
    surface.setControls("profileA", fourControlModel());

    const juce::Point<float> start(350.0f, 350.0f); // empty space, far from every cell
    const juce::Point<float> end = start + juce::Point<float>(40.0f, -25.0f);

    surface.mouseDown(surfaceMouseEvent(surface, start, start, false));
    surface.mouseDrag(surfaceMouseEvent(surface, end, start, true));
    surface.mouseUp(surfaceMouseEvent(surface, end, start, true));

    EXPECT_NEAR(surface.getPanOffsetForTest().x, 40.0f, 0.01f);
    EXPECT_NEAR(surface.getPanOffsetForTest().y, -25.0f, 0.01f);
}

TEST(ControllerSurfaceViewTest, PanningDoesNotClearAnExistingSelection) {
    ControllerSurfaceComponent surface;
    surface.setSize(400, 400);
    surface.setControls("profileA", fourControlModel());
    surface.setSelectedControlId("knob1");

    const juce::Point<float> start(350.0f, 350.0f);
    const juce::Point<float> end = start + juce::Point<float>(10.0f, 10.0f);
    surface.mouseDown(surfaceMouseEvent(surface, start, start, false));
    surface.mouseDrag(surfaceMouseEvent(surface, end, start, true));
    surface.mouseUp(surfaceMouseEvent(surface, end, start, true));

    EXPECT_EQ(surface.getSelectedControlIds(), std::vector<juce::String>{"knob1"});
}

// ---- Zoom -----------------------------------------------------------------------------------

TEST(ControllerSurfaceViewTest, PlainWheelPansInsteadOfZooming) {
    ControllerSurfaceComponent surface;
    surface.setSize(400, 400);
    surface.setControls("profileA", fourControlModel());

    const float zoomBefore = surface.getZoomLevelForTest();
    juce::MouseWheelDetails wheel{};
    wheel.deltaY = 1.0f;
    surface.mouseWheelMove(surfaceMouseEvent(surface, {100.0f, 100.0f}, {100.0f, 100.0f}, false), wheel);

    EXPECT_FLOAT_EQ(surface.getZoomLevelForTest(), zoomBefore);
    EXPECT_NE(surface.getPanOffsetForTest(), juce::Point<float>()) << "a plain wheel event must still have panned";
}

TEST(ControllerSurfaceViewTest, CmdWheelZoomKeepsContentPointUnderCursorFixed) {
    ControllerSurfaceComponent surface;
    surface.setSize(400, 400);
    surface.setControls("profileA", fourControlModel());

    const juce::Point<float> cursor(150.0f, 250.0f);
    const auto contentBefore = screenToContent(surface, cursor);
    const auto widthBefore = surface.getVisibleContentRect().getWidth();

    juce::MouseWheelDetails wheel{};
    wheel.deltaY = 1.5f;
    const juce::ModifierKeys cmd(juce::ModifierKeys::commandModifier);
    surface.mouseWheelMove(surfaceMouseEvent(surface, cursor, cursor, false, cmd), wheel);

    const auto contentAfter = screenToContent(surface, cursor);
    EXPECT_NEAR(contentAfter.x, contentBefore.x, 0.5f);
    EXPECT_NEAR(contentAfter.y, contentBefore.y, 0.5f);
    EXPECT_LT(surface.getVisibleContentRect().getWidth(), widthBefore) << "the wheel event must still have zoomed";
}

TEST(ControllerSurfaceViewTest, ZoomStaysClampedUnderRepeatedWheelTicks) {
    ControllerSurfaceComponent surface;
    surface.setSize(400, 400);
    surface.setControls("profileA", fourControlModel());
    const juce::ModifierKeys cmd(juce::ModifierKeys::commandModifier);

    juce::MouseWheelDetails wheelIn{};
    wheelIn.deltaY = 10.0f;
    for (int i = 0; i < 200; ++i)
        surface.mouseWheelMove(surfaceMouseEvent(surface, {100.0f, 100.0f}, {100.0f, 100.0f}, false, cmd), wheelIn);
    EXPECT_NEAR(surface.getZoomLevelForTest(), ControllerSurfaceComponent::kMaxZoom, 0.001f);

    juce::MouseWheelDetails wheelOut{};
    wheelOut.deltaY = -10.0f;
    for (int i = 0; i < 200; ++i)
        surface.mouseWheelMove(surfaceMouseEvent(surface, {100.0f, 100.0f}, {100.0f, 100.0f}, false, cmd), wheelOut);
    EXPECT_NEAR(surface.getZoomLevelForTest(), ControllerSurfaceComponent::kMinZoom, 0.001f);
}

TEST(ControllerSurfaceViewTest, PinchZoomKeepsContentPointUnderPinchFixed) {
    ControllerSurfaceComponent surface;
    surface.setSize(400, 400);
    surface.setControls("profileA", fourControlModel());

    const juce::Point<float> pinchPoint(200.0f, 180.0f);
    const auto contentBefore = screenToContent(surface, pinchPoint);

    surface.mouseMagnify(surfaceMouseEvent(surface, pinchPoint, pinchPoint, false), 1.2f);

    const auto contentAfter = screenToContent(surface, pinchPoint);
    EXPECT_NEAR(contentAfter.x, contentBefore.x, 0.5f);
    EXPECT_NEAR(contentAfter.y, contentBefore.y, 0.5f);
    EXPECT_GT(surface.getZoomLevelForTest(), 1.0f);
}

// ---- Per-controller view memory (trivial, in-memory only) -----------------------------------

TEST(ControllerSurfaceViewTest, ViewIsRestoredPerControllerAcrossProfileSwitches) {
    ControllerSurfaceComponent surface;
    surface.setSize(400, 400);
    surface.setControls("profileA", fourControlModel());

    const juce::Point<float> start(350.0f, 350.0f);
    const juce::Point<float> end = start + juce::Point<float>(30.0f, 15.0f);
    surface.mouseDown(surfaceMouseEvent(surface, start, start, false));
    surface.mouseDrag(surfaceMouseEvent(surface, end, start, true));
    surface.mouseUp(surfaceMouseEvent(surface, end, start, true));

    juce::MouseWheelDetails wheel{};
    wheel.deltaY = 2.0f;
    surface.mouseWheelMove(
        surfaceMouseEvent(surface, end, end, false, juce::ModifierKeys(juce::ModifierKeys::commandModifier)), wheel);

    const auto panA = surface.getPanOffsetForTest();
    const auto zoomA = surface.getZoomLevelForTest();
    ASSERT_NE(zoomA, 1.0f);

    // A different profile starts at the identity view.
    surface.setControls("profileB", fourControlModel());
    EXPECT_FLOAT_EQ(surface.getZoomLevelForTest(), 1.0f);
    EXPECT_EQ(surface.getPanOffsetForTest(), juce::Point<float>());

    // Switching back to profile A restores exactly the view it was left at.
    surface.setControls("profileA", fourControlModel());
    EXPECT_FLOAT_EQ(surface.getZoomLevelForTest(), zoomA);
    EXPECT_EQ(surface.getPanOffsetForTest(), panA);
}

// ---- FRO331: drag-lands-where-dropped must still hold under pan/zoom ------------------------

// Companion to ControllerSurfaceTests.cpp's MultiStepDragLandsExactlyOnOriginPlusDelta, at a
// non-1.0 zoom and non-zero pan (both reached through the surface's own real gestures) --
// ScreenSpaceDragDriver (not DragDriver, which stays entirely in parent-local units and so cannot
// exercise the transform at all) originates every step in the surface's own local space and lets
// JUCE's own coordinate conversion carry it through content_'s transform, same as a real event.
TEST(ControllerSurfaceViewTest, DragLandsWhereDroppedUnderNonTrivialZoomAndPan) {
    ControllerSurfaceComponent surface;
    surface.setSize(600, 600);
    surface.setControls("profileA", fourControlModel());

    juce::MouseWheelDetails wheel{};
    wheel.deltaY = 3.0f;
    surface.mouseWheelMove(surfaceMouseEvent(surface, {300.0f, 300.0f}, {300.0f, 300.0f}, false,
                                             juce::ModifierKeys(juce::ModifierKeys::commandModifier)),
                           wheel);

    const juce::Point<float> panStart(400.0f, 400.0f);
    const juce::Point<float> panEnd = panStart + juce::Point<float>(60.0f, -35.0f);
    surface.mouseDown(surfaceMouseEvent(surface, panStart, panStart, false));
    surface.mouseDrag(surfaceMouseEvent(surface, panEnd, panStart, true));
    surface.mouseUp(surfaceMouseEvent(surface, panEnd, panStart, true));

    ASSERT_NE(surface.getZoomLevelForTest(), 1.0f);
    ASSERT_NE(surface.getPanOffsetForTest(), juce::Point<float>());

    std::vector<ControllerSurfaceComponent::MovedCell> moves;
    surface.onControlsMoved = [&](const std::vector<ControllerSurfaceComponent::MovedCell>& m) { moves = m; };

    auto* knob = findCell(surface, "knob1"); // starts at (0,0)
    ASSERT_NE(knob, nullptr);

    ScreenSpaceDragDriver drag(surface, *knob);
    drag.stepToCanvasOffset(1, 0);
    drag.stepToCanvasOffset(2, 0);
    drag.stepToCanvasOffset(3, 0);
    drag.stepToCanvasOffset(3, 1);
    drag.end();

    ASSERT_EQ(moves.size(), 1u);
    EXPECT_EQ(moves[0].controlId, "knob1");
    EXPECT_EQ(moves[0].col, 3);
    EXPECT_EQ(moves[0].row, 1);
}

// FRO331 bugfix regression: the coordinator's own in-app repro (a GROUP drag crossing several
// OCCUPIED cells) reproduced at zoom ~0.7 and ~1.25 in the app, not only at the default 1.0 this
// file's other tests use -- ScreenSpaceDragDriver (which actually converts through content_'s
// transform, unlike DragDriver) is the only test shape that can cover that combination.
TEST(ControllerSurfaceViewTest, GroupDragCrossingOccupiedCellsLandsExactlyOnOriginPlusDeltaAtNonUnityZoom) {
    ControllerSurfaceComponent surface;
    surface.setSize(900, 300);
    surface.setControls("profileA", eightPadsInARowModel());

    // Zoom to roughly 0.7x, out from the default 1.0 -- one Cmd+wheel tick with a negative delta.
    juce::MouseWheelDetails wheel{};
    wheel.deltaY = -3.0f;
    surface.mouseWheelMove(surfaceMouseEvent(surface, {300.0f, 150.0f}, {300.0f, 150.0f}, false,
                                             juce::ModifierKeys(juce::ModifierKeys::commandModifier)),
                           wheel);
    ASSERT_NEAR(surface.getZoomLevelForTest(), 0.7f, 0.05f);

    click(*findCell(surface, "pad1"));
    click(*findCell(surface, "pad2"), juce::ModifierKeys(juce::ModifierKeys::shiftModifier));
    click(*findCell(surface, "pad3"), juce::ModifierKeys(juce::ModifierKeys::shiftModifier));
    click(*findCell(surface, "pad4"), juce::ModifierKeys(juce::ModifierKeys::shiftModifier));
    ASSERT_EQ(surface.getSelectedControlIds().size(), 4u);

    std::vector<ControllerSurfaceComponent::MovedCell> moves;
    surface.onControlsMoved = [&](const std::vector<ControllerSurfaceComponent::MovedCell>& m) { moves = m; };

    auto* pad1 = findCell(surface, "pad1"); // (0,0); pad2/3/4 at (1,0)/(2,0)/(3,0); pad5-8 occupy cols 4-7
    ASSERT_NE(pad1, nullptr);
    ScreenSpaceDragDriver drag(surface, *pad1);
    for (int dCols = 1; dCols <= 8; ++dCols)
        drag.stepToCanvasOffset(dCols, 0);
    drag.end();

    ASSERT_EQ(moves.size(), 4u);
    for (const auto& move : moves) {
        if (move.controlId == "pad1")
            EXPECT_EQ(move.col, 8);
        else if (move.controlId == "pad2")
            EXPECT_EQ(move.col, 9);
        else if (move.controlId == "pad3")
            EXPECT_EQ(move.col, 10);
        else if (move.controlId == "pad4")
            EXPECT_EQ(move.col, 11);
    }
}
