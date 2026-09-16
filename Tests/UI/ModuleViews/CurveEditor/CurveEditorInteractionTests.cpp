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

// ---------------------------------------------------------------------------
// Bug 1: frozen visible range during a node drag
// ---------------------------------------------------------------------------
// currentGeometry() derives its visible range from the model's CURRENT total duration, so a
// Fixed-mode x-drag of the last node re-maps pixels-to-time on every mouseDrag off a duration
// that the previous event just changed. Holding the pointer still in the drag headroom past the
// last node would runaway-grow (or, dragged left, runaway-shrink) the duration every event. The
// fix freezes the visible range at mouseDown (Node hits only) and restores auto-fit at mouseUp.

TEST(CurveEditorInteractionTest, NodeDragFreezesVisibleRangeAcrossRepeatedEventsAtTheSamePosition) {
    CurveEditorComponent comp;
    comp.setSize(kWidth, kHeight);
    comp.setModel(buildEnvelopeModel());

    const CurveEditorGeometry geometry(comp.getModel(), comp.getLocalBounds().toFloat());
    const auto releasePos = geometry.nodePosition(kReleaseEnd);
    // Well into the visible-range headroom past the last node.
    const juce::Point<float> target{(float)kWidth - 5.0f, releasePos.y};

    comp.mouseDown(curveLeftClick(comp, releasePos));

    comp.mouseDrag(curveLeftDrag(comp, target, releasePos));
    const double duration1 = comp.getModel().getMaxX();
    comp.mouseDrag(curveLeftDrag(comp, target, releasePos));
    const double duration2 = comp.getModel().getMaxX();
    comp.mouseDrag(curveLeftDrag(comp, target, releasePos));
    const double duration3 = comp.getModel().getMaxX();
    comp.mouseDrag(curveLeftDrag(comp, target, releasePos));
    const double duration4 = comp.getModel().getMaxX();

    // Without the fix, each identical-target event would re-fit off the just-grown duration and
    // grow it further (~5% per event, since range = 1.1x duration recomputes off an ever-growing
    // duration); with the frozen range, every event maps the same pixel to the same time.
    EXPECT_NEAR(duration2, duration1, 1e-9);
    EXPECT_NEAR(duration3, duration1, 1e-9);
    EXPECT_NEAR(duration4, duration1, 1e-9);

    comp.mouseUp(curveLeftClick(comp, target));
}

TEST(CurveEditorInteractionTest, NodeDragFrozenRangeMapsBackToTheOriginalDurationExactly) {
    CurveEditorComponent comp;
    comp.setSize(kWidth, kHeight);
    comp.setModel(buildEnvelopeModel());

    const CurveEditorGeometry geometry(comp.getModel(), comp.getLocalBounds().toFloat());
    const auto releasePos = geometry.nodePosition(kReleaseEnd);
    const double originalDuration = comp.getModel().getMaxX();
    const juce::Point<float> target{(float)kWidth - 5.0f, releasePos.y};

    comp.mouseDown(curveLeftClick(comp, releasePos));
    comp.mouseDrag(curveLeftDrag(comp, target, releasePos));
    EXPECT_NE(comp.getModel().getMaxX(), originalDuration) << "the node should have actually moved";

    // Drive it back to the original on-screen position within the SAME gesture.
    comp.mouseDrag(curveLeftDrag(comp, releasePos, releasePos));
    EXPECT_NEAR(comp.getModel().getMaxX(), originalDuration, 1e-6);

    comp.mouseUp(curveLeftClick(comp, releasePos));
}

TEST(CurveEditorInteractionTest, MouseUpClearsTheFrozenRangeAndRefitsTheGeometry) {
    CurveEditorComponent comp;
    comp.setSize(kWidth, kHeight);
    comp.setModel(buildEnvelopeModel());

    const CurveEditorGeometry geometry(comp.getModel(), comp.getLocalBounds().toFloat());
    const auto releasePos = geometry.nodePosition(kReleaseEnd);
    const juce::Point<float> target{(float)kWidth - 5.0f, releasePos.y};

    comp.mouseDown(curveLeftClick(comp, releasePos));
    comp.mouseDrag(curveLeftDrag(comp, target, releasePos));
    comp.mouseUp(curveLeftClick(comp, target));

    // A lingering frozen range would put the release node at a different on-screen pixel than a
    // freshly auto-fit geometry predicts, so hit-testing the fresh position would miss.
    const CurveEditorGeometry freshGeometry(comp.getModel(), comp.getLocalBounds().toFloat());
    const auto expectedPos = freshGeometry.nodePosition(kReleaseEnd);

    const CurveHitResult hit = comp.hitTest(expectedPos);
    EXPECT_EQ(hit.kind, CurveHitKind::Node);
    EXPECT_EQ(hit.index, kReleaseEnd);
}

// ---------------------------------------------------------------------------
// Bug 2: setModel mid-gesture
// ---------------------------------------------------------------------------
// setModel() used to unconditionally reset drag/hover/selection state. The envelope card calls
// setModel from its own parameter-listener while the user is mid-drag (the drag writes a
// parameter, the listener pushes the model back), which would cancel the gesture after its very
// first event. The fix only resets when the topology actually changed (node count or mode) or an
// active index is now out of range.

TEST(CurveEditorInteractionTest, SetModelPreservesALiveDragWhenTopologyIsUnchanged) {
    CurveEditorComponent comp;
    comp.setSize(kWidth, kHeight);
    comp.setModel(buildEnvelopeModel());

    int startCount = 0, endCount = 0;
    comp.onGestureStart = [&] { ++startCount; };
    comp.onGestureEnd = [&] { ++endCount; };

    const CurveEditorGeometry geometry(comp.getModel(), comp.getLocalBounds().toFloat());
    const auto sustainPos = geometry.nodePosition(kSustain);

    comp.mouseDown(curveLeftClick(comp, sustainPos));
    comp.mouseDrag(curveLeftDrag(comp, sustainPos.translated(5.0f, 0.0f), sustainPos));
    EXPECT_EQ(startCount, 1);

    // Same topology (5 nodes, Fixed) but different EnvelopeShape values -- as the envelope card's
    // parameter-listener round trip would push mid-drag.
    EnvelopeShape shape;
    shape.sustain = 0.6f;
    shape.decay = 0.250;
    comp.setModel(buildEnvelopeModel(shape));
    EXPECT_EQ(endCount, 0) << "an unchanged topology must not cancel the live gesture";

    const double xBeforeSecondDrag = comp.getModel().getNode(kSustain).x;
    const float yBeforeSecondDrag = comp.getModel().getNode(kSustain).y;
    comp.mouseDrag(curveLeftDrag(comp, sustainPos.translated(15.0f, -5.0f), sustainPos));
    EXPECT_EQ(startCount, 1) << "the drag must not have been reopened as a new gesture";
    EXPECT_TRUE(comp.getModel().getNode(kSustain).x != xBeforeSecondDrag ||
                comp.getModel().getNode(kSustain).y != yBeforeSecondDrag)
        << "the second drag must not be a no-op";

    comp.mouseUp(curveLeftClick(comp, sustainPos));
    EXPECT_EQ(endCount, 1);
}

TEST(CurveEditorInteractionTest, SetModelCancelsALiveDragOnATopologyChange) {
    CurveEditorComponent comp;
    comp.setSize(kWidth, kHeight);
    comp.setModel(buildEnvelopeModel());

    int startCount = 0, endCount = 0;
    comp.onGestureStart = [&] { ++startCount; };
    comp.onGestureEnd = [&] { ++endCount; };

    const CurveEditorGeometry geometry(comp.getModel(), comp.getLocalBounds().toFloat());
    const auto sustainPos = geometry.nodePosition(kSustain);

    comp.mouseDown(curveLeftClick(comp, sustainPos));
    comp.mouseDrag(curveLeftDrag(comp, sustainPos.translated(5.0f, 0.0f), sustainPos));
    EXPECT_EQ(startCount, 1);

    // Different topology: 3 nodes, Free mode (vs. 5 nodes, Fixed).
    comp.setModel(buildFreeModel());
    EXPECT_EQ(endCount, 1) << "the topology change must pair the still-open gesture right here";

    const CurveModel modelBeforeStaleDrag = comp.getModel();
    comp.mouseDrag(curveLeftDrag(comp, sustainPos.translated(50.0f, 50.0f), sustainPos));
    EXPECT_EQ(startCount, 1) << "a stale drag must not reopen a gesture";
    EXPECT_EQ(endCount, 1) << "a stale drag must not fire a second end";
    for (int i = 0; i < comp.getModel().getNumNodes(); ++i) {
        EXPECT_DOUBLE_EQ(comp.getModel().getNode(i).x, modelBeforeStaleDrag.getNode(i).x) << "node " << i;
        EXPECT_FLOAT_EQ(comp.getModel().getNode(i).y, modelBeforeStaleDrag.getNode(i).y) << "node " << i;
    }

    comp.mouseUp(curveLeftClick(comp, sustainPos));
    EXPECT_EQ(endCount, 1) << "mouseUp after cancellation must not double-fire onGestureEnd";
}
