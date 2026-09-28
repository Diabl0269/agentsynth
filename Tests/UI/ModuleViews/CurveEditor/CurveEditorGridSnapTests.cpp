// CurveEditorGridSnapTests.cpp -- the generic CurveEditorComponent/CurveEditorGeometry/
// CurveModel additions the LFO custom-wave card needs: setGrid/setSnapToGrid (+ Shift bypass),
// zeroSegmentPx, the right-click context-menu hook, and CurveModel::playheadForX. The envelope
// card never touches any of these -- see CurveEditorInteractionTests.cpp for its own coverage,
// unaffected by any of this.
#include "CurveEditorTestHelpers.h"
#include <gtest/gtest.h>

using namespace synth::ui;
using namespace synth::ui::test;

namespace {

constexpr int kWidth = 400;
constexpr int kHeight = 200;

// A Free-mode LFO-shaped model: phase 0..1, level 0..1, three points (a mid-point movable in both
// axes, endpoints pinned in x only) -- mirrors buildLfoCurveModel's real topology closely enough
// to exercise snap/grid/context-menu/playhead without depending on ModuleComponentLfoCard.cpp.
CurveModel buildLfoShapedModel() {
    CurveModel model(CurveMode::Free);
    std::vector<CurveNode> nodes(3);
    nodes[0] = CurveNode{0.0, 0.0f, false, true, 0.0, std::numeric_limits<double>::infinity(), 0.0f, 1.0f};
    nodes[1] = CurveNode{0.5, 1.0f, true, true, 0.0, std::numeric_limits<double>::infinity(), 0.0f, 1.0f};
    nodes[2] = CurveNode{1.0, 0.0f, false, true, 0.0, std::numeric_limits<double>::infinity(), 0.0f, 1.0f};
    model.setNodes(nodes);
    return model;
}

// A Square-like step: two points sharing x = 0.5 (a zero-length segment between them).
CurveModel buildStepModel() {
    CurveModel model(CurveMode::Free);
    std::vector<CurveNode> nodes(4);
    nodes[0] = CurveNode{0.0, 1.0f, false, true};
    nodes[1] = CurveNode{0.5, 1.0f, true, true};
    nodes[2] = CurveNode{0.5, 0.0f, true, true};
    nodes[3] = CurveNode{1.0, 0.0f, false, true};
    model.setNodes(nodes);
    return model;
}

} // namespace

TEST(CurveEditorGridSnapTest, SnapRoundsAddedPointToGrid) {
    CurveEditorComponent comp;
    comp.setSize(kWidth, kHeight);
    comp.setModel(buildLfoShapedModel());
    comp.setZeroSegmentPx(0.0f);
    comp.setGrid(CurveEditorComponent::CurveGrid{8, 8});
    comp.setSnapToGrid(true);

    // Aim at a pixel a few px off any 1/8 grid line -- the resulting point must still land
    // exactly on one.
    const CurveEditorGeometry geometry(comp.getModel(), comp.getLocalBounds().toFloat());
    const float offX = geometry.xForTime(0.26); // near 0.25, not on it
    const float offY = geometry.yForLevel(0.29f);

    comp.mouseDoubleClick(curveDoubleClick(comp, {offX, offY}));

    bool foundSnapped = false;
    for (int i = 0; i < comp.getModel().getNumNodes(); ++i) {
        const auto& n = comp.getModel().getNode(i);
        if (std::abs(n.x - 0.25) < 1.0e-6 && std::abs(n.y - 0.25f) < 1.0e-4f)
            foundSnapped = true;
    }
    EXPECT_TRUE(foundSnapped) << "the added point must snap to the nearest 1/8 x 1/8 grid cell";
}

TEST(CurveEditorGridSnapTest, SnapRoundsDraggedPointToGridAndShiftBypasses) {
    CurveEditorComponent comp;
    comp.setSize(kWidth, kHeight);
    comp.setModel(buildLfoShapedModel());
    comp.setGrid(CurveEditorComponent::CurveGrid{8, 8});
    comp.setSnapToGrid(true);

    const CurveEditorGeometry geometry(comp.getModel(), comp.getLocalBounds().toFloat());
    const auto midPos = geometry.nodePosition(1);
    const juce::Point<float> off = {midPos.x + 3.0f, midPos.y + 3.0f}; // a few px off-grid

    comp.mouseDown(curveLeftClick(comp, midPos));
    comp.mouseDrag(curveLeftDrag(comp, off, midPos));
    const auto& snappedNode = comp.getModel().getNode(1);
    EXPECT_NEAR(std::fmod(snappedNode.x * 8.0, 1.0), 0.0, 1.0e-6) << "dragged x snaps to a 1/8 line";
    EXPECT_NEAR(std::fmod((double)snappedNode.y * 8.0, 1.0), 0.0, 1.0e-3) << "dragged y snaps to a 1/8 line";
    comp.mouseUp(curveLeftDrag(comp, off, midPos));

    // Shift bypasses snap for a fresh drag -- reset and redo off-grid with Shift held.
    comp.setModel(buildLfoShapedModel());
    const auto midPos2 = CurveEditorGeometry(comp.getModel(), comp.getLocalBounds().toFloat()).nodePosition(1);
    const juce::Point<float> off2 = {midPos2.x + 3.0f, midPos2.y + 3.0f};
    comp.mouseDown(curveLeftClick(comp, midPos2));
    comp.mouseDrag(curveLeftDragShift(comp, off2, midPos2));
    const auto& unsnappedNode = comp.getModel().getNode(1);
    EXPECT_GT(std::fmod(unsnappedNode.x * 8.0, 1.0), 1.0e-6) << "Shift bypasses snap for this drag";
    comp.mouseUp(curveLeftDragShift(comp, off2, midPos2));
}

TEST(CurveEditorGridSnapTest, SnapNeverTouchesBendDrags) {
    CurveEditorComponent comp;
    comp.setSize(kWidth, kHeight);
    auto model = buildLfoShapedModel();
    model.setBend(0, 0.3f);
    comp.setModel(model);
    comp.setGrid(CurveEditorComponent::CurveGrid{4, 4}); // coarse grid -- an unsnapped bend value
    comp.setSnapToGrid(true);                            // would virtually never land on a 1/4 line

    const CurveEditorGeometry geometry(comp.getModel(), comp.getLocalBounds().toFloat());
    const auto handlePos = *geometry.bendHandlePosition(0);

    comp.mouseDown(curveLeftClick(comp, handlePos));
    comp.mouseDrag(curveLeftDrag(comp, {handlePos.x, handlePos.y - 17.0f}, handlePos));
    comp.mouseUp(curveLeftDrag(comp, {handlePos.x, handlePos.y - 17.0f}, handlePos));

    const float bend = comp.getModel().getBend(0);
    EXPECT_NE(bend, 0.3f);
    EXPECT_NE(std::fmod((double)bend * 4.0, 1.0), 0.0) << "a bend drag is never snapped, coarse grid or not";
}

TEST(CurveEditorGridSnapTest, ZeroSegmentPxZeroDrawsAStepAsVerticalLine) {
    CurveGeometryConfig withZero;
    withZero.zeroSegmentPx = 0.0f;
    const auto model = buildStepModel();
    const CurveEditorGeometry geometry(model, {0.0f, 0.0f, (float)kWidth, (float)kHeight}, withZero);

    const auto beforeStep = geometry.nodePosition(1);
    const auto afterStep = geometry.nodePosition(2);
    EXPECT_FLOAT_EQ(beforeStep.x, afterStep.x) << "zeroSegmentPx == 0 draws the step as a true vertical line";
}

TEST(CurveEditorGridSnapTest, GridPaintDrawsDivisionLines) {
    CurveEditorComponent comp;
    comp.setSize(kWidth, kHeight);
    comp.setModel(buildLfoShapedModel());
    comp.setGrid(CurveEditorComponent::CurveGrid{8, 8});

    juce::Image img(juce::Image::ARGB, kWidth, kHeight, true, juce::SoftwareImageType());
    juce::Graphics g(img);
    comp.paintEntireComponent(g, false);

    // A faint grid line somewhere along the top row must differ from the plain background fill
    // at (1, 1) -- a crude but real "something was drawn" pixel check.
    bool sawNonBackground = false;
    const auto background = img.getPixelAt(1, 1);
    for (int x = 0; x < kWidth; x += 4)
        if (img.getPixelAt(x, 1) != background)
            sawNonBackground = true;
    EXPECT_TRUE(sawNonBackground) << "setGrid must actually paint division lines";
}

TEST(CurveEditorGridSnapTest, RightClickInvokesContextHookAndStartsNoDrag) {
    CurveEditorComponent comp;
    comp.setSize(kWidth, kHeight);
    comp.setModel(buildLfoShapedModel());

    int callCount = 0;
    CurveHitResult capturedHit;
    comp.onContextMenu = [&](const juce::MouseEvent&, CurveHitResult hit) {
        ++callCount;
        capturedHit = hit;
    };

    const CurveEditorGeometry geometry(comp.getModel(), comp.getLocalBounds().toFloat());
    const auto midPos = geometry.nodePosition(1);
    comp.mouseDown(curveRightClick(comp, midPos));
    EXPECT_EQ(callCount, 1);
    EXPECT_EQ(capturedHit.kind, CurveHitKind::Node);
    EXPECT_EQ(capturedHit.index, 1);

    // No drag was armed: a mouseDrag right afterwards must not move anything.
    const auto before = comp.getModel().getNode(1);
    comp.mouseDrag(curveLeftDrag(comp, {midPos.x + 30.0f, midPos.y}, midPos));
    EXPECT_EQ(comp.getModel().getNode(1).x, before.x);
}

TEST(CurveEditorGridSnapTest, PlayheadForXMapsAcrossZeroLengthSegments) {
    const auto model = buildStepModel();
    // Just before, and exactly at, the step: still segment 0 (0 -> 0.5) -- playheadForX picks
    // the first segment with x <= its end and non-zero duration (mirrors
    // CurveEditorGeometry::segmentForTime's own convention), so the boundary itself still belongs
    // to the segment ENDING there.
    const auto before = model.playheadForX(0.49);
    EXPECT_EQ(before.segment, 0);
    EXPECT_GT(before.progress, 0.9f);

    const auto atStep = model.playheadForX(0.5);
    EXPECT_EQ(atStep.segment, 0);
    EXPECT_FLOAT_EQ(atStep.progress, 1.0f);

    // Just after the step: the zero-length segment (0.5 -> 0.5) is skipped entirely; segment 2
    // (0.5 -> 1.0) claims it, at a progress near 0.
    const auto afterStep = model.playheadForX(0.50001);
    EXPECT_EQ(afterStep.segment, 2);
    EXPECT_NEAR(afterStep.progress, 0.0f, 1.0e-2f);
}

TEST(CurveEditorGridSnapTest, AddRemoveResetFireChangeCallbacksBeforeGestureEnd) {
    CurveEditorComponent comp;
    comp.setSize(kWidth, kHeight);
    auto model = buildLfoShapedModel();
    model.setBend(0, 0.4f);
    comp.setModel(model);

    bool pointsChangedFiredBeforeGestureEnd = false;
    comp.onPointsChanged = [&] { pointsChangedFiredBeforeGestureEnd = true; };
    comp.onGestureEnd = [&] { EXPECT_TRUE(pointsChangedFiredBeforeGestureEnd) << "add"; };
    comp.addPointAt({10.0f, 10.0f});

    pointsChangedFiredBeforeGestureEnd = false;
    comp.onGestureEnd = [&] { EXPECT_TRUE(pointsChangedFiredBeforeGestureEnd) << "remove"; };
    comp.removeNode(1);

    bool bendChangedFiredBeforeGestureEnd = false;
    comp.onBendChanged = [&](int) { bendChangedFiredBeforeGestureEnd = true; };
    comp.onGestureEnd = [&] { EXPECT_TRUE(bendChangedFiredBeforeGestureEnd) << "resetBend"; };
    comp.resetBend(0);
}
