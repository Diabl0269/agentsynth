// CurveEditorInteractionTests.cpp
// Tests for synth::ui::CurveEditorComponent's interaction: the public primitives (dragNodeTo,
// dragBendBy, addPointAt, removeNode, resetBend) AND the real mouse path (mouseDown/mouseDrag/
// mouseUp/mouseDoubleClick) synthesised via juce::MouseEvent, per FRO111.

#include "CurveEditorTestHelpers.h"
#include <gtest/gtest.h>

using namespace synth::ui;
using namespace synth::ui::test;

namespace {

constexpr int kWidth = 400;
constexpr int kHeight = 200;

CurveModel buildFreeModel() {
    CurveModel model(CurveMode::Free);
    std::vector<CurveNode> nodes(3);
    nodes[0] = CurveNode{0.0, 0.0f, false, true};
    nodes[1] = CurveNode{0.5, 0.5f, true, true};
    nodes[2] = CurveNode{1.0, 1.0f, false, true};
    model.setNodes(nodes);
    return model;
}

// Four movable interior points so a drag can reorder past an INTERIOR neighbour without tying
// the pinned boundary node (which would make the sort's stability keep the original order).
CurveModel buildFourPointFreeModel() {
    CurveModel model(CurveMode::Free);
    std::vector<CurveNode> nodes(4);
    nodes[0] = CurveNode{0.0, 0.0f, true, true};
    nodes[1] = CurveNode{0.3, 0.3f, true, true};
    nodes[2] = CurveNode{0.6, 0.6f, true, true};
    nodes[3] = CurveNode{1.0, 1.0f, true, true};
    model.setNodes(nodes);
    return model;
}

} // namespace

// ---------------------------------------------------------------------------
// dragNodeTo
// ---------------------------------------------------------------------------

TEST(CurveEditorInteractionTest, DragNodeToAppliesXOnlyWhenYNotMovable) {
    CurveEditorComponent comp;
    comp.setSize(kWidth, kHeight);
    comp.setModel(buildEnvelopeModel());

    const CurveEditorGeometry geometry(comp.getModel(), comp.getLocalBounds().toFloat());
    const auto attackPos = geometry.nodePosition(kAttackPeak);

    const int newIndex = comp.dragNodeTo(kAttackPeak, {attackPos.x + 20.0f, attackPos.y - 50.0f});

    EXPECT_EQ(newIndex, kAttackPeak);
    EXPECT_FLOAT_EQ(comp.getModel().getNode(kAttackPeak).y, 1.0f) << "y is pinned on the attack peak";
    EXPECT_GT(comp.getModel().getNode(kAttackPeak).x, 0.0) << "x should have moved right";
}

TEST(CurveEditorInteractionTest, DragNodeToAppliesBothAxesOnTheSustainNode) {
    CurveEditorComponent comp;
    comp.setSize(kWidth, kHeight);
    comp.setModel(buildEnvelopeModel());

    const CurveEditorGeometry geometry(comp.getModel(), comp.getLocalBounds().toFloat());
    const double originalSustainX = comp.getModel().getNode(kSustain).x;
    const auto sustainPos = geometry.nodePosition(kSustain);

    comp.dragNodeTo(kSustain, {sustainPos.x + 10.0f, sustainPos.y - 30.0f});

    EXPECT_NE(comp.getModel().getNode(kSustain).x, originalSustainX);
    EXPECT_GT(comp.getModel().getNode(kSustain).y, 0.4f) << "moving the point up should raise the level";
}

TEST(CurveEditorInteractionTest, DragNodeToInFreeModeCanReorderAndReturnsNewIndex) {
    CurveEditorComponent comp;
    comp.setSize(kWidth, kHeight);
    comp.setModel(buildFourPointFreeModel());

    int changedIndex = -1;
    comp.onNodeChanged = [&](int index) { changedIndex = index; };

    // Node 1 sits at model x=0.3; dragging it to x=0.8 crosses node 2 (x=0.6) -- a genuine
    // reorder past an INTERIOR neighbour (not the pinned/boundary node).
    const CurveEditorGeometry geometry(comp.getModel(), comp.getLocalBounds().toFloat());
    const juce::Point<float> target{geometry.xForTime(0.8), geometry.nodePosition(1).y};
    const int newIndex = comp.dragNodeTo(1, target);

    EXPECT_EQ(newIndex, 2);
    EXPECT_EQ(changedIndex, 2);
}

// ---------------------------------------------------------------------------
// dragBendBy
// ---------------------------------------------------------------------------

TEST(CurveEditorInteractionTest, DragBendByIncreasesBendWhenDraggingUpOnARisingSegment) {
    CurveEditorComponent comp;
    comp.setModel(buildEnvelopeModel());
    ASSERT_GT(comp.getModel().getNode(kAttackPeak).y, comp.getModel().getNode(kOrigin).y) << "attack is rising";

    comp.dragBendBy(kAttackSeg, -15.0f); // mouse moved UP
    EXPECT_GT(comp.getModel().getBend(kAttackSeg), 0.0f);
}

TEST(CurveEditorInteractionTest, DragBendByIncreasesBendWhenDraggingDownOnAFallingSegment) {
    CurveEditorComponent comp;
    comp.setModel(buildEnvelopeModel());
    ASSERT_LT(comp.getModel().getNode(kSustain).y, comp.getModel().getNode(kHoldEnd).y) << "decay is falling";

    comp.dragBendBy(kDecaySeg, 15.0f); // mouse moved DOWN
    EXPECT_GT(comp.getModel().getBend(kDecaySeg), 0.0f);
}

TEST(CurveEditorInteractionTest, DragBendByOppositeDirectionDecreasesBend) {
    CurveEditorComponent comp;
    comp.setModel(buildEnvelopeModel());

    comp.dragBendBy(kAttackSeg, 15.0f); // mouse moved DOWN on a rising segment
    EXPECT_LT(comp.getModel().getBend(kAttackSeg), 0.0f);
}

TEST(CurveEditorInteractionTest, DragBendByClampsToUnitRange) {
    CurveEditorComponent comp;
    comp.setModel(buildEnvelopeModel());

    comp.dragBendBy(kAttackSeg, -10000.0f);
    EXPECT_FLOAT_EQ(comp.getModel().getBend(kAttackSeg), 1.0f);

    comp.dragBendBy(kAttackSeg, 20000.0f);
    EXPECT_FLOAT_EQ(comp.getModel().getBend(kAttackSeg), -1.0f);
}

TEST(CurveEditorInteractionTest, DragBendByIsANoOpOnANonBendableSegment) {
    CurveEditorComponent comp;
    comp.setModel(buildEnvelopeModel());
    ASSERT_FALSE(comp.getModel().isBendable(kHoldSeg));

    comp.dragBendBy(kHoldSeg, -50.0f);
    EXPECT_FLOAT_EQ(comp.getModel().getBend(kHoldSeg), 0.0f);
}

TEST(CurveEditorInteractionTest, ResetBendSetsToZeroAndFiresCallback) {
    CurveEditorComponent comp;
    EnvelopeShape shape;
    shape.decayBend = 0.6f;
    comp.setModel(buildEnvelopeModel(shape));

    int firedSegment = -1;
    comp.onBendChanged = [&](int seg) { firedSegment = seg; };
    comp.resetBend(kDecaySeg);

    EXPECT_FLOAT_EQ(comp.getModel().getBend(kDecaySeg), 0.0f);
    EXPECT_EQ(firedSegment, kDecaySeg);
}

// ---------------------------------------------------------------------------
// addPointAt / removeNode
// ---------------------------------------------------------------------------

TEST(CurveEditorInteractionTest, AddPointAtRefusedOutsideFreeMode) {
    CurveEditorComponent comp;
    comp.setSize(kWidth, kHeight);
    comp.setModel(buildEnvelopeModel());
    EXPECT_EQ(comp.addPointAt({50.0f, 50.0f}), -1);
    EXPECT_EQ(comp.getModel().getNumNodes(), 5);
}

TEST(CurveEditorInteractionTest, AddPointAtInsertsAndFiresOnPointsChanged) {
    CurveEditorComponent comp;
    comp.setSize(kWidth, kHeight);
    comp.setModel(buildFreeModel());

    bool pointsChangedFired = false;
    comp.onPointsChanged = [&] { pointsChangedFired = true; };

    const int index = comp.addPointAt({(float)kWidth * 0.25f, (float)kHeight * 0.5f});

    EXPECT_NE(index, -1);
    EXPECT_EQ(comp.getModel().getNumNodes(), 4);
    EXPECT_TRUE(pointsChangedFired);
}

TEST(CurveEditorInteractionTest, RemoveNodeRefusesAPinnedNode) {
    CurveEditorComponent comp;
    comp.setSize(kWidth, kHeight);
    comp.setModel(buildFreeModel());
    EXPECT_FALSE(comp.removeNode(0)); // node 0 is pinned (xMovable == false)
    EXPECT_EQ(comp.getModel().getNumNodes(), 3);
}

TEST(CurveEditorInteractionTest, RemoveNodeSucceedsAndFiresOnPointsChanged) {
    CurveEditorComponent comp;
    comp.setSize(kWidth, kHeight);
    comp.setModel(buildFreeModel());

    bool pointsChangedFired = false;
    comp.onPointsChanged = [&] { pointsChangedFired = true; };

    EXPECT_TRUE(comp.removeNode(1));
    EXPECT_EQ(comp.getModel().getNumNodes(), 2);
    EXPECT_TRUE(pointsChangedFired);
}

// ---------------------------------------------------------------------------
// Real mouse path
// ---------------------------------------------------------------------------

TEST(CurveEditorInteractionTest, PlainClickWithNoDragOpensNoGesture) {
    CurveEditorComponent comp;
    comp.setSize(kWidth, kHeight);
    comp.setModel(buildEnvelopeModel());

    int startCount = 0, endCount = 0;
    comp.onGestureStart = [&] { ++startCount; };
    comp.onGestureEnd = [&] { ++endCount; };

    const CurveEditorGeometry geometry(comp.getModel(), comp.getLocalBounds().toFloat());
    const auto pos = geometry.nodePosition(kAttackPeak);

    comp.mouseDown(curveLeftClick(comp, pos));
    comp.mouseUp(curveLeftClick(comp, pos));

    EXPECT_EQ(startCount, 0);
    EXPECT_EQ(endCount, 0);
}

TEST(CurveEditorInteractionTest, DragThroughRealMouseEventsOpensOneGestureAndMovesTheNode) {
    CurveEditorComponent comp;
    comp.setSize(kWidth, kHeight);
    comp.setModel(buildEnvelopeModel());

    int startCount = 0, endCount = 0;
    comp.onGestureStart = [&] { ++startCount; };
    comp.onGestureEnd = [&] { ++endCount; };

    const CurveEditorGeometry geometry(comp.getModel(), comp.getLocalBounds().toFloat());
    const auto startPos = geometry.nodePosition(kAttackPeak);
    const double originalX = comp.getModel().getNode(kAttackPeak).x;

    comp.mouseDown(curveLeftClick(comp, startPos));
    EXPECT_EQ(startCount, 0) << "mouseDown alone must not open a gesture";

    const juce::Point<float> dragPos1 = startPos.translated(10.0f, 0.0f);
    const juce::Point<float> dragPos2 = startPos.translated(25.0f, 0.0f);
    comp.mouseDrag(curveLeftDrag(comp, dragPos1, startPos));
    EXPECT_EQ(startCount, 1) << "the first drag opens exactly one gesture";
    comp.mouseDrag(curveLeftDrag(comp, dragPos2, startPos));
    EXPECT_EQ(startCount, 1) << "further drags in the same gesture must not reopen it";

    comp.mouseUp(curveLeftClick(comp, dragPos2));
    EXPECT_EQ(endCount, 1);
    EXPECT_GT(comp.getModel().getNode(kAttackPeak).x, originalX);
}

TEST(CurveEditorInteractionTest, RealDoubleClickAddsAPointInFreeMode) {
    CurveEditorComponent comp;
    comp.setSize(kWidth, kHeight);
    comp.setModel(buildFreeModel());

    // Empty space, far from every node and handle.
    comp.mouseDoubleClick(curveDoubleClick(comp, {(float)kWidth * 0.75f, (float)kHeight * 0.1f}));
    EXPECT_EQ(comp.getModel().getNumNodes(), 4);
}

TEST(CurveEditorInteractionTest, RealDoubleClickOnANodeRemovesItInFreeMode) {
    CurveEditorComponent comp;
    comp.setSize(kWidth, kHeight);
    comp.setModel(buildFreeModel());

    const CurveEditorGeometry geometry(comp.getModel(), comp.getLocalBounds().toFloat());
    const auto pos = geometry.nodePosition(1);

    comp.mouseDoubleClick(curveDoubleClick(comp, pos));
    EXPECT_EQ(comp.getModel().getNumNodes(), 2);
}

TEST(CurveEditorInteractionTest, RealDoubleClickOnABendHandleResetsIt) {
    CurveEditorComponent comp;
    comp.setSize(kWidth, kHeight);
    EnvelopeShape shape;
    shape.decayBend = 0.7f;
    comp.setModel(buildEnvelopeModel(shape));

    const CurveEditorGeometry geometry(comp.getModel(), comp.getLocalBounds().toFloat());
    const auto handle = geometry.bendHandlePosition(kDecaySeg);
    ASSERT_TRUE(handle.has_value());

    comp.mouseDoubleClick(curveDoubleClick(comp, *handle));
    EXPECT_FLOAT_EQ(comp.getModel().getBend(kDecaySeg), 0.0f);
}
