// CurveEditorGeometryTests.cpp
// Tests for synth::ui::CurveEditorGeometry — the pure pixel mapping behind the reusable
// breakpoint curve editor (FRO111): time/level <-> pixel round trips, the zero-duration-segment
// display plateau, hit-testing priority/tie rules, bend-handle tracking, and playhead mapping.

#include "CurveEditorTestHelpers.h"
#include "Modules/Envelope/EnvelopeGenerator.h"
#include "UI/ModuleViews/CurveEditor/CurveEditorGeometry.h"
#include <gtest/gtest.h>

using namespace synth::ui;
using namespace synth::ui::test;

namespace {
constexpr float kWidth = 400.0f;
constexpr float kHeight = 200.0f;
const juce::Rectangle<float> kBounds{0.0f, 0.0f, kWidth, kHeight};
} // namespace

// ---------------------------------------------------------------------------
// time/level <-> pixel round trips
// ---------------------------------------------------------------------------

TEST(CurveEditorGeometryTest, LevelPixelRoundTrips) {
    const auto model = buildEnvelopeModel();
    const CurveEditorGeometry geometry(model, kBounds);
    for (float level : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        const float y = geometry.yForLevel(level);
        EXPECT_NEAR(geometry.levelForY(y), level, 1e-4f);
    }
}

TEST(CurveEditorGeometryTest, LevelZeroAndOneAreNotClippedAgainstEdges) {
    const auto model = buildEnvelopeModel();
    const CurveEditorGeometry geometry(model, kBounds);
    EXPECT_LT(geometry.yForLevel(1.0f), kBounds.getBottom());
    EXPECT_GT(geometry.yForLevel(1.0f), kBounds.getY());
    EXPECT_LT(geometry.yForLevel(0.0f), kBounds.getBottom());
    EXPECT_GT(geometry.yForLevel(0.0f), kBounds.getY());
}

TEST(CurveEditorGeometryTest, TimePixelRoundTripsWithinEachNode) {
    const auto model = buildEnvelopeModel();
    const CurveEditorGeometry geometry(model, kBounds);
    for (int i = 0; i < model.getNumNodes(); ++i) {
        const double time = model.getNode(i).x;
        const float x = geometry.xForTime(time);
        EXPECT_NEAR(geometry.timeForX(x), time, 1e-6) << "node " << i;
    }
}

TEST(CurveEditorGeometryTest, TimePixelRoundTripsMidSegment) {
    EnvelopeShape shape;
    shape.hold = 0.02; // give the decay segment real width to sample mid-segment points from
    const auto model = buildEnvelopeModel(shape);
    const CurveEditorGeometry geometry(model, kBounds);

    const double midDecayTime = (model.getNode(kHoldEnd).x + model.getNode(kSustain).x) * 0.5;
    const float x = geometry.xForTime(midDecayTime);
    EXPECT_NEAR(geometry.timeForX(x), midDecayTime, 1e-6);
    // Strictly between the segment's endpoints in pixel space.
    EXPECT_GT(x, geometry.xForTime(model.getNode(kHoldEnd).x));
    EXPECT_LT(x, geometry.xForTime(model.getNode(kSustain).x));
}

// ---------------------------------------------------------------------------
// Zero-duration segment: the 12px display plateau
// ---------------------------------------------------------------------------

TEST(CurveEditorGeometryTest, ZeroDurationSegmentRendersExactly12PxPlateau) {
    const auto model = buildEnvelopeModel(); // default shape.hold == 0.0
    ASSERT_DOUBLE_EQ(model.segmentDuration(kHoldSeg), 0.0);
    const CurveEditorGeometry geometry(model, kBounds);

    const float attackPeakX = geometry.nodePosition(kAttackPeak).x;
    const float holdEndX = geometry.nodePosition(kHoldEnd).x;
    EXPECT_FLOAT_EQ(holdEndX - attackPeakX, CurveEditorGeometry::kZeroSegmentPx);
}

TEST(CurveEditorGeometryTest, ZeroDurationSegmentHasNoBendHandle) {
    const auto model = buildEnvelopeModel();
    const CurveEditorGeometry geometry(model, kBounds);
    EXPECT_FALSE(geometry.bendHandlePosition(kHoldSeg).has_value());
}

TEST(CurveEditorGeometryTest, TimeForXInsideZeroWidthPlateauResolvesToSegmentStartTime) {
    const auto model = buildEnvelopeModel();
    const CurveEditorGeometry geometry(model, kBounds);
    const float attackPeakX = geometry.nodePosition(kAttackPeak).x;
    const float holdEndX = geometry.nodePosition(kHoldEnd).x;
    const float midPlateauX = (attackPeakX + holdEndX) * 0.5f;
    EXPECT_NEAR(geometry.timeForX(midPlateauX), model.getNode(kAttackPeak).x, 1e-9);
}

// ---------------------------------------------------------------------------
// Bend handle: absent on flat/non-bendable segments, tracks the curve midpoint otherwise
// ---------------------------------------------------------------------------

TEST(CurveEditorGeometryTest, NonBendableSegmentHasNoBendHandle) {
    const auto model = buildEnvelopeModel(); // hold segment is marked non-bendable
    const CurveEditorGeometry geometry(model, kBounds);
    EXPECT_FALSE(geometry.bendHandlePosition(kHoldSeg).has_value());
}

TEST(CurveEditorGeometryTest, FlatSegmentHasNoBendHandleEvenIfBendable) {
    CurveModel model(CurveMode::Fixed);
    std::vector<CurveNode> nodes(2);
    nodes[0] = CurveNode{0.0, 0.5f, false, false};
    nodes[1] = CurveNode{1.0, 0.5f, true, false}; // same level -> flat
    model.setNodes(nodes);
    ASSERT_TRUE(model.isBendable(0));

    const CurveEditorGeometry geometry(model, kBounds);
    EXPECT_FALSE(geometry.bendHandlePosition(0).has_value());
}

TEST(CurveEditorGeometryTest, BendHandleTracksCurveMidpointAsBendChanges) {
    auto model = buildEnvelopeModel();

    model.setBend(kDecaySeg, 0.0f);
    const CurveEditorGeometry linearGeometry(model, kBounds);
    const auto linearHandle = linearGeometry.bendHandlePosition(kDecaySeg);
    ASSERT_TRUE(linearHandle.has_value());
    EXPECT_FLOAT_EQ(linearHandle->y, linearGeometry.yForLevel(model.valueAt(kDecaySeg, 0.5f)));

    model.setBend(kDecaySeg, 0.8f);
    const CurveEditorGeometry bentGeometry(model, kBounds);
    const auto bentHandle = bentGeometry.bendHandlePosition(kDecaySeg);
    ASSERT_TRUE(bentHandle.has_value());
    EXPECT_FLOAT_EQ(bentHandle->y, bentGeometry.yForLevel(model.valueAt(kDecaySeg, 0.5f)));

    EXPECT_NE(linearHandle->y, bentHandle->y) << "changing bend should move the handle";
    // x is the pixel midpoint of the segment regardless of bend.
    EXPECT_FLOAT_EQ(linearHandle->x, bentHandle->x);
}

TEST(CurveEditorGeometryTest, BendHandleValueMatchesEnvelopeGeneratorShapeDirectly) {
    auto model = buildEnvelopeModel();
    model.setBend(kDecaySeg, -0.5f);
    const CurveEditorGeometry geometry(model, kBounds);

    const float start = model.getNode(kHoldEnd).y;
    const float end = model.getNode(kSustain).y;
    const float expectedLevel = start + (end - start) * synth::EnvelopeGenerator::shape(0.5f, -0.5f);

    const auto handle = geometry.bendHandlePosition(kDecaySeg);
    ASSERT_TRUE(handle.has_value());
    EXPECT_FLOAT_EQ(handle->y, geometry.yForLevel(expectedLevel));
}

// ---------------------------------------------------------------------------
// Hit-testing
// ---------------------------------------------------------------------------

TEST(CurveEditorGeometryTest, PinnedOriginIsNeverHit) {
    const auto model = buildEnvelopeModel();
    ASSERT_FALSE(model.getNode(kOrigin).xMovable);
    ASSERT_FALSE(model.getNode(kOrigin).yMovable);
    const CurveEditorGeometry geometry(model, kBounds);

    const auto result = geometry.hitTest(geometry.nodePosition(kOrigin));
    EXPECT_EQ(result.kind, CurveHitKind::None);
}

TEST(CurveEditorGeometryTest, PlateauSeparatedNodesAreEachSeparatelyGrabbable) {
    const auto model = buildEnvelopeModel();
    const CurveEditorGeometry geometry(model, kBounds);

    const auto atAttackPeak = geometry.hitTest(geometry.nodePosition(kAttackPeak));
    EXPECT_EQ(atAttackPeak.kind, CurveHitKind::Node);
    EXPECT_EQ(atAttackPeak.index, kAttackPeak);

    const auto atHoldEnd = geometry.hitTest(geometry.nodePosition(kHoldEnd));
    EXPECT_EQ(atHoldEnd.kind, CurveHitKind::Node);
    EXPECT_EQ(atHoldEnd.index, kHoldEnd);
}

TEST(CurveEditorGeometryTest, NearestNodeWinsRegardlessOfEncounterOrder) {
    const auto model = buildEnvelopeModel();
    const CurveEditorGeometry geometry(model, kBounds);

    // A point 9px right of attack-peak (index 1, checked FIRST) is only 3px left of hold-end
    // (index 2, checked SECOND and a HIGHER index) -- the nearer one must still win.
    const juce::Point<float> probe = geometry.nodePosition(kAttackPeak).translated(9.0f, 0.0f);
    const auto result = geometry.hitTest(probe);
    EXPECT_EQ(result.kind, CurveHitKind::Node);
    EXPECT_EQ(result.index, kHoldEnd);
}

TEST(CurveEditorGeometryTest, ExactPixelTieGoesToTheEarlierIndex) {
    // Degenerate zero-width bounds collapses every node to the same x; giving every interior
    // node the same level collapses them to the same y too, forcing a genuine pixel tie.
    CurveModel model(CurveMode::Free);
    std::vector<CurveNode> nodes(4);
    nodes[0] = CurveNode{0.0, 0.0f, false, false}; // pinned, not hittable
    nodes[1] = CurveNode{0.3, 0.5f, true, true};
    nodes[2] = CurveNode{0.6, 0.5f, true, true};
    nodes[3] = CurveNode{1.0, 0.5f, true, true};
    model.setNodes(nodes);

    const juce::Rectangle<float> degenerateBounds{0.0f, 0.0f, 0.0f, kHeight};
    const CurveEditorGeometry geometry(model, degenerateBounds);

    ASSERT_FLOAT_EQ(geometry.nodePosition(1).x, geometry.nodePosition(2).x);
    ASSERT_FLOAT_EQ(geometry.nodePosition(1).y, geometry.nodePosition(3).y);

    const auto result = geometry.hitTest(geometry.nodePosition(1));
    EXPECT_EQ(result.kind, CurveHitKind::Node);
    EXPECT_EQ(result.index, 1);
}

TEST(CurveEditorGeometryTest, NodeBeatsBendHandleEvenWhenHandleIsExactlyUnderThePoint) {
    // A segment short in BOTH duration and level span puts its bend-handle's pixel position
    // (the pixel midpoint between its two nodes, in both x and y) within hit radius of both
    // surrounding nodes -- the node priority rule must still win.
    CurveModel model(CurveMode::Fixed);
    std::vector<CurveNode> nodes(3);
    nodes[0] = CurveNode{0.0, 0.50f, true, true};
    nodes[1] = CurveNode{0.02, 0.51f, true, true};
    nodes[2] = CurveNode{1.0, 0.50f, true, true};
    model.setNodes(nodes);

    const CurveEditorGeometry geometry(model, kBounds);
    const auto handle = geometry.bendHandlePosition(0);
    ASSERT_TRUE(handle.has_value());
    ASSERT_LT(handle->getDistanceFrom(geometry.nodePosition(0)), CurveEditorGeometry::kHitRadiusPx);
    ASSERT_LT(handle->getDistanceFrom(geometry.nodePosition(1)), CurveEditorGeometry::kHitRadiusPx);

    const auto result = geometry.hitTest(*handle);
    EXPECT_EQ(result.kind, CurveHitKind::Node);
}

// ---------------------------------------------------------------------------
// Playhead mapping
// ---------------------------------------------------------------------------

TEST(CurveEditorGeometryTest, PlayheadAtProgressHalfMatchesTheBendHandlePosition) {
    const auto model = buildEnvelopeModel();
    const CurveEditorGeometry geometry(model, kBounds);

    const auto handle = geometry.bendHandlePosition(kDecaySeg);
    ASSERT_TRUE(handle.has_value());
    const auto playheadPos = geometry.playheadPosition(CurvePlayhead{kDecaySeg, 0.5f});
    EXPECT_FLOAT_EQ(playheadPos.x, handle->x);
    EXPECT_FLOAT_EQ(playheadPos.y, handle->y);
}

TEST(CurveEditorGeometryTest, PlayheadAtProgressZeroAndOneMatchNodePositions) {
    const auto model = buildEnvelopeModel();
    const CurveEditorGeometry geometry(model, kBounds);

    const auto start = geometry.playheadPosition(CurvePlayhead{kDecaySeg, 0.0f});
    EXPECT_FLOAT_EQ(start.x, geometry.nodePosition(kHoldEnd).x);
    EXPECT_FLOAT_EQ(start.y, geometry.nodePosition(kHoldEnd).y);

    const auto end = geometry.playheadPosition(CurvePlayhead{kDecaySeg, 1.0f});
    EXPECT_FLOAT_EQ(end.x, geometry.nodePosition(kSustain).x);
    EXPECT_FLOAT_EQ(end.y, geometry.nodePosition(kSustain).y);
}

// ---------------------------------------------------------------------------
// Grid ticks / label formatting
// ---------------------------------------------------------------------------

TEST(CurveEditorGeometryTest, GridTicksStartAtZeroAndStayWithinVisibleRange) {
    const auto model = buildEnvelopeModel();
    const CurveEditorGeometry geometry(model, kBounds);
    const auto ticks = geometry.computeGridTicks();
    ASSERT_FALSE(ticks.empty());
    EXPECT_DOUBLE_EQ(ticks.front(), 0.0);
    for (double t : ticks)
        EXPECT_LE(t, geometry.getVisibleRange() + 1e-6);
}

TEST(CurveEditorGeometryTest, DefaultTimeLabelFormatting) {
    EXPECT_EQ(CurveEditorGeometry::defaultTimeLabel(0.0), "0");
    EXPECT_EQ(CurveEditorGeometry::defaultTimeLabel(0.25), "250ms");
    EXPECT_EQ(CurveEditorGeometry::defaultTimeLabel(1.0), "1s");
    EXPECT_EQ(CurveEditorGeometry::defaultTimeLabel(1.5), "1.5s");
}
