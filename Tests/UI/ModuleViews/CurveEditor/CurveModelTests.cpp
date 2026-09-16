// CurveModelTests.cpp
// Tests for synth::ui::CurveModel — the pure data + edit model behind the reusable breakpoint
// curve editor (FRO111): node/segment bookkeeping, bend shaping via EnvelopeGenerator::shape,
// Fixed-mode ripple x-edits, and Free-mode add/remove/reorder.

#include "CurveEditorTestHelpers.h"
#include "Modules/Envelope/EnvelopeGenerator.h"
#include "UI/ModuleViews/CurveEditor/CurveModel.h"
#include <gtest/gtest.h>

using namespace synth::ui;
using namespace synth::ui::test;

// ---------------------------------------------------------------------------
// Construction / basic bookkeeping
// ---------------------------------------------------------------------------

TEST(CurveModelTest, EnvelopeTopologyHasFiveNodesAndFourSegments) {
    const auto model = buildEnvelopeModel();
    EXPECT_EQ(model.getNumNodes(), 5);
    EXPECT_EQ(model.getNumSegments(), 4);
    EXPECT_DOUBLE_EQ(model.getMinX(), 0.0);
}

TEST(CurveModelTest, SegmentDurationMatchesStageTimes) {
    EnvelopeShape shape;
    shape.attack = 0.01;
    shape.hold = 0.02;
    shape.decay = 0.3;
    shape.release = 0.5;
    const auto model = buildEnvelopeModel(shape);
    EXPECT_NEAR(model.segmentDuration(kAttackSeg), 0.01, 1e-12);
    EXPECT_NEAR(model.segmentDuration(kHoldSeg), 0.02, 1e-12);
    EXPECT_NEAR(model.segmentDuration(kDecaySeg), 0.3, 1e-12);
    EXPECT_NEAR(model.segmentDuration(kReleaseSeg), 0.5, 1e-12);
}

TEST(CurveModelTest, ValueAtMatchesEnvelopeGeneratorShapeExactly) {
    EnvelopeShape shape;
    shape.decayBend = 0.65f;
    auto model = buildEnvelopeModel(shape);

    for (float progress : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        const float start = model.getNode(kSustain - 1).y; // hold-end level (1.0)
        const float end = model.getNode(kSustain).y;       // sustain level
        const float expected = start + (end - start) * synth::EnvelopeGenerator::shape(progress, shape.decayBend);
        EXPECT_FLOAT_EQ(model.valueAt(kDecaySeg, progress), expected);
    }
}

TEST(CurveModelTest, ValueAtEndpointsAreExactRegardlessOfBend) {
    auto model = buildEnvelopeModel();
    model.setBend(kDecaySeg, 0.9f);
    EXPECT_FLOAT_EQ(model.valueAt(kDecaySeg, 0.0f), model.getNode(kHoldEnd).y);
    EXPECT_FLOAT_EQ(model.valueAt(kDecaySeg, 1.0f), model.getNode(kSustain).y);
}

// ---------------------------------------------------------------------------
// Bend get/set/bendable
// ---------------------------------------------------------------------------

TEST(CurveModelTest, SetBendClampsToUnitRange) {
    auto model = buildEnvelopeModel();
    model.setBend(kDecaySeg, 5.0f);
    EXPECT_FLOAT_EQ(model.getBend(kDecaySeg), 1.0f);
    model.setBend(kDecaySeg, -5.0f);
    EXPECT_FLOAT_EQ(model.getBend(kDecaySeg), -1.0f);
}

TEST(CurveModelTest, HoldSegmentIsNotBendableByConstruction) {
    const auto model = buildEnvelopeModel();
    EXPECT_FALSE(model.isBendable(kHoldSeg));
    EXPECT_TRUE(model.isBendable(kAttackSeg));
}

// ---------------------------------------------------------------------------
// Fixed mode: ripple x-edit
// ---------------------------------------------------------------------------

TEST(CurveModelTest, FixedDragAttackPeakRightLengthensAttackAndShiftsLaterNodesBySameDelta) {
    auto model = buildEnvelopeModel();
    const double holdEndBefore = model.getNode(kHoldEnd).x;
    const double sustainBefore = model.getNode(kSustain).x;
    const double releaseEndBefore = model.getNode(kReleaseEnd).x;
    const double holdDurationBefore = model.segmentDuration(kHoldSeg);
    const double decayDurationBefore = model.segmentDuration(kDecaySeg);
    const double releaseDurationBefore = model.segmentDuration(kReleaseSeg);

    const double newAttackTime = model.getNode(kAttackPeak).x + 0.05; // +50ms
    const auto result = model.setNodeX(kAttackPeak, newAttackTime);

    EXPECT_EQ(result.newIndex, kAttackPeak) << "Fixed mode never changes a node's index";
    EXPECT_NEAR(model.getNode(kAttackPeak).x, newAttackTime, 1e-12);
    EXPECT_NEAR(model.segmentDuration(kAttackSeg), newAttackTime, 1e-12);

    const double delta = 0.05;
    EXPECT_NEAR(model.getNode(kHoldEnd).x, holdEndBefore + delta, 1e-9);
    EXPECT_NEAR(model.getNode(kSustain).x, sustainBefore + delta, 1e-9);
    EXPECT_NEAR(model.getNode(kReleaseEnd).x, releaseEndBefore + delta, 1e-9);

    // Later segment durations are preserved -- only the attack segment actually changed length.
    EXPECT_NEAR(model.segmentDuration(kHoldSeg), holdDurationBefore, 1e-9);
    EXPECT_NEAR(model.segmentDuration(kDecaySeg), decayDurationBefore, 1e-9);
    EXPECT_NEAR(model.segmentDuration(kReleaseSeg), releaseDurationBefore, 1e-9);
}

TEST(CurveModelTest, FixedDragClampsToMinMaxSegmentConstraints) {
    std::vector<CurveNode> nodes(3);
    nodes[0] = CurveNode{0.0, 0.0f, false, false};
    nodes[1] = CurveNode{0.1, 1.0f, true, true, /*minSegment*/ 0.02, /*maxSegment*/ 0.2, 0.0f, 1.0f};
    nodes[2] = CurveNode{0.5, 0.0f, true, false};
    CurveModel model(CurveMode::Fixed);
    model.setNodes(nodes);

    // Below minSegment: clamps to prevX + minSegment.
    model.setNodeX(1, 0.005);
    EXPECT_NEAR(model.getNode(1).x, 0.02, 1e-9);

    // Above maxSegment: clamps to prevX + maxSegment.
    model.setNodeX(1, 10.0);
    EXPECT_NEAR(model.getNode(1).x, 0.2, 1e-9);
}

TEST(CurveModelTest, FixedYIgnoredWhenNotYMovable) {
    auto model = buildEnvelopeModel();
    ASSERT_FALSE(model.getNode(kAttackPeak).yMovable);
    model.setNodeY(kAttackPeak, 0.2f);
    EXPECT_FLOAT_EQ(model.getNode(kAttackPeak).y, 1.0f) << "attack peak's level is pinned at 1.0";
}

TEST(CurveModelTest, FixedSustainNodeXAndYBothApply) {
    auto model = buildEnvelopeModel();
    const double releaseEndBefore = model.getNode(kReleaseEnd).x;
    const double releaseDurationBefore = model.segmentDuration(kReleaseSeg);

    const double newSustainX = model.getNode(kSustain).x + 0.03;
    model.setNodeX(kSustain, newSustainX);
    model.setNodeY(kSustain, 0.7f);

    EXPECT_NEAR(model.getNode(kSustain).x, newSustainX, 1e-9);
    EXPECT_FLOAT_EQ(model.getNode(kSustain).y, 0.7f);
    // Release-end shifts by the same delta, preserving the release segment's own duration.
    EXPECT_NEAR(model.getNode(kReleaseEnd).x, releaseEndBefore + 0.03, 1e-9);
    EXPECT_NEAR(model.segmentDuration(kReleaseSeg), releaseDurationBefore, 1e-9);
}

TEST(CurveModelTest, FixedOriginXIsPinnedAndNeverMoves) {
    auto model = buildEnvelopeModel();
    const auto result = model.setNodeX(kOrigin, 0.5);
    EXPECT_EQ(result.newIndex, kOrigin);
    EXPECT_DOUBLE_EQ(model.getNode(kOrigin).x, 0.0);
}

// ---------------------------------------------------------------------------
// Free mode: add / remove / reorder
// ---------------------------------------------------------------------------

namespace {
CurveModel buildFreeModel() {
    CurveModel model(CurveMode::Free);
    std::vector<CurveNode> nodes(3);
    nodes[0] = CurveNode{0.0, 0.0f, false, true};
    nodes[1] = CurveNode{0.5, 0.5f, true, true};
    nodes[2] = CurveNode{1.0, 1.0f, false, true};
    model.setNodes(nodes);
    return model;
}
} // namespace

TEST(CurveModelTest, FreeAddPointInsertsAtSortedPosition) {
    auto model = buildFreeModel();
    model.setBend(0, 0.4f); // the segment (0 -> 1) being split

    const int newIndex = model.addPoint(0.25, 0.3f);

    EXPECT_EQ(newIndex, 1);
    EXPECT_EQ(model.getNumNodes(), 4);
    EXPECT_DOUBLE_EQ(model.getNode(1).x, 0.25);
    EXPECT_FLOAT_EQ(model.getNode(1).y, 0.3f);
    // Both halves of the split segment keep the original bend.
    EXPECT_FLOAT_EQ(model.getBend(0), 0.4f);
    EXPECT_FLOAT_EQ(model.getBend(1), 0.4f);
}

TEST(CurveModelTest, FreeRemovePointRefusesPinnedNode) {
    auto model = buildFreeModel();
    ASSERT_FALSE(model.getNode(0).xMovable);
    EXPECT_FALSE(model.canRemovePoint(0));
    EXPECT_FALSE(model.removePoint(0));
    EXPECT_EQ(model.getNumNodes(), 3);
}

TEST(CurveModelTest, FreeRemovePointRefusesBelowTwoNodes) {
    CurveModel model(CurveMode::Free);
    std::vector<CurveNode> nodes(2);
    nodes[0] = CurveNode{0.0, 0.0f, true, true};
    nodes[1] = CurveNode{1.0, 1.0f, true, true};
    model.setNodes(nodes);

    EXPECT_FALSE(model.canRemovePoint(0));
    EXPECT_FALSE(model.removePoint(0));
    EXPECT_EQ(model.getNumNodes(), 2);
}

TEST(CurveModelTest, FreeRemovePointRemovesAMovableMiddleNode) {
    auto model = buildFreeModel();
    EXPECT_TRUE(model.removePoint(1));
    EXPECT_EQ(model.getNumNodes(), 2);
}

TEST(CurveModelTest, FreeDragAcrossNeighbourReordersAndReportsNewIndex) {
    CurveModel model(CurveMode::Free);
    std::vector<CurveNode> nodes(4);
    nodes[0] = CurveNode{0.0, 0.0f, true, true};
    nodes[1] = CurveNode{0.3, 0.3f, true, true};
    nodes[2] = CurveNode{0.6, 0.6f, true, true};
    nodes[3] = CurveNode{1.0, 1.0f, true, true};
    model.setNodes(nodes);

    // Drag node 1 (x=0.3) past node 2 (x=0.6) to x=0.8.
    const auto result = model.setNodeX(1, 0.8);

    EXPECT_EQ(result.newIndex, 2);
    // Post-sort order by x: 0, 0.6, 0.8, 1.0.
    EXPECT_DOUBLE_EQ(model.getNode(0).x, 0.0);
    EXPECT_DOUBLE_EQ(model.getNode(1).x, 0.6);
    EXPECT_DOUBLE_EQ(model.getNode(2).x, 0.8);
    EXPECT_DOUBLE_EQ(model.getNode(3).x, 1.0);
}

TEST(CurveModelTest, FreeDragClampsToModelRange) {
    auto model = buildFreeModel();
    model.setNodeX(1, 5.0); // far past the last node
    EXPECT_DOUBLE_EQ(model.getNode(1).x, model.getMaxX());
    model.setNodeX(1, -5.0); // far before the first node
    EXPECT_DOUBLE_EQ(model.getNode(1).x, model.getMinX());
}

TEST(CurveModelTest, CanRemovePointFalseOutsideFreeMode) {
    const auto model = buildEnvelopeModel();
    EXPECT_FALSE(model.canRemovePoint(kSustain));
}
