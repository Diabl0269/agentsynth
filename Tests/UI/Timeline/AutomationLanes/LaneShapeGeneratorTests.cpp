// LaneShapeGeneratorTests.cpp -- the pure breakpoint generation behind the Draw tool's stamped shapes:
// point counts per cycle, where each shape starts, the saw's drop, the square's holds, partial cycles and
// the closing point.

#include "UI/Timeline/AutomationLanes/LaneShapes/DrawShape.h"
#include "UI/Timeline/AutomationLanes/LaneShapes/LaneShapeGenerator.h"
#include <gtest/gtest.h>

using synth::AutomationLane;
using synth::BreakpointCurve;
using synth::ui::DrawShape;
using synth::ui::estimateShapePointCount;
using synth::ui::generateShapePoints;

namespace {

constexpr int kHold = static_cast<int>(BreakpointCurve::Hold);
constexpr int kLinear = static_cast<int>(BreakpointCurve::Linear);

void expectStrictlySorted(const std::vector<AutomationLane::Breakpoint>& pts) {
    for (size_t i = 1; i < pts.size(); ++i)
        EXPECT_LT(pts[i - 1].beat, pts[i].beat) << "beats must be unique and ascending at " << i;
}

} // namespace

TEST(LaneShapeGeneratorTest, SineHasSixteenLinearPointsPerCycleStartingAtTheMiddleGoingUp) {
    const auto pts = generateShapePoints(DrawShape::Sine, 0.0, 4.0, 1.0, 0.0, 100.0);
    ASSERT_EQ(pts.size(), 4u * synth::ui::kSinePointsPerCycle + 1u) << "4 cycles plus the closing point";
    expectStrictlySorted(pts);
    EXPECT_DOUBLE_EQ(pts[0].beat, 0.0);
    EXPECT_NEAR(pts[0].value, 50.0, 1e-9) << "starts at the middle";
    EXPECT_GT(pts[1].value, 50.0) << "going up";
    EXPECT_NEAR(pts[4].value, 100.0, 1e-9) << "a quarter cycle in is the top";
    EXPECT_NEAR(pts[12].value, 0.0, 1e-9) << "three quarters in is the bottom";
    for (const auto& p : pts)
        EXPECT_EQ(p.curve, kLinear);
    EXPECT_DOUBLE_EQ(pts.back().beat, 4.0);
    EXPECT_NEAR(pts.back().value, 50.0, 1e-9);
}

TEST(LaneShapeGeneratorTest, TriangleHasTwoPointsPerCycle) {
    const auto pts = generateShapePoints(DrawShape::Triangle, 2.0, 4.0, 0.5, 10.0, 30.0);
    ASSERT_EQ(pts.size(), 4u * 2u + 1u);
    expectStrictlySorted(pts);
    EXPECT_DOUBLE_EQ(pts[0].value, 10.0);
    EXPECT_DOUBLE_EQ(pts[1].beat, 2.25);
    EXPECT_DOUBLE_EQ(pts[1].value, 30.0);
    EXPECT_DOUBLE_EQ(pts[2].beat, 2.5);
    EXPECT_DOUBLE_EQ(pts[2].value, 10.0);
    EXPECT_DOUBLE_EQ(pts.back().beat, 4.0);
    EXPECT_DOUBLE_EQ(pts.back().value, 10.0);
}

TEST(LaneShapeGeneratorTest, SawRampsUpAndDropsThroughAHeldTopPointJustBeforeTheNextCycle) {
    const double cycle = 1.0;
    const auto pts = generateShapePoints(DrawShape::Saw, 0.0, 2.0, cycle, 0.0, 1.0);
    ASSERT_EQ(pts.size(), 5u) << "bottom, top, bottom, top, closing point";
    expectStrictlySorted(pts);
    EXPECT_DOUBLE_EQ(pts[0].value, 0.0);
    EXPECT_EQ(pts[0].curve, kLinear);
    EXPECT_DOUBLE_EQ(pts[1].value, 1.0);
    EXPECT_EQ(pts[1].curve, kHold) << "the top holds until the drop";
    EXPECT_LT(pts[1].beat, pts[2].beat) << "strictly before the next cycle's start";
    EXPECT_NEAR(pts[2].beat - pts[1].beat, synth::ui::sawDropBeats(cycle), 1e-12);
    EXPECT_DOUBLE_EQ(pts[2].beat, 1.0);
    EXPECT_DOUBLE_EQ(pts[2].value, 0.0);
    EXPECT_EQ(pts[3].curve, kHold);
    EXPECT_DOUBLE_EQ(pts.back().beat, 2.0);
    EXPECT_DOUBLE_EQ(pts.back().value, 1.0) << "the last ramp ends at the top";
}

TEST(LaneShapeGeneratorTest, SquareUsesHoldPoints) {
    const auto pts = generateShapePoints(DrawShape::Square, 0.0, 2.0, 1.0, 0.0, 1.0);
    ASSERT_EQ(pts.size(), 5u);
    expectStrictlySorted(pts);
    EXPECT_DOUBLE_EQ(pts[0].value, 1.0);
    EXPECT_EQ(pts[0].curve, kHold);
    EXPECT_DOUBLE_EQ(pts[1].beat, 0.5);
    EXPECT_DOUBLE_EQ(pts[1].value, 0.0);
    EXPECT_EQ(pts[1].curve, kHold);
    EXPECT_DOUBLE_EQ(pts.back().value, 0.0) << "the closing point holds the last half's value";
    EXPECT_EQ(pts.back().curve, kLinear);
}

TEST(LaneShapeGeneratorTest, APartialLastCycleClosesAtTheShapesValueThere) {
    // 1.5 triangle cycles: the half cycle ends on the top.
    const auto pts = generateShapePoints(DrawShape::Triangle, 0.0, 1.5, 1.0, 0.0, 1.0);
    expectStrictlySorted(pts);
    ASSERT_EQ(pts.size(), 4u);
    EXPECT_DOUBLE_EQ(pts.back().beat, 1.5);
    EXPECT_DOUBLE_EQ(pts.back().value, 1.0);
}

TEST(LaneShapeGeneratorTest, NonPeriodicShapesAndEmptySpansGenerateNothing) {
    EXPECT_TRUE(generateShapePoints(DrawShape::Free, 0.0, 4.0, 1.0, 0.0, 1.0).empty());
    EXPECT_TRUE(generateShapePoints(DrawShape::Line, 0.0, 4.0, 1.0, 0.0, 1.0).empty());
    EXPECT_TRUE(generateShapePoints(DrawShape::Sine, 4.0, 4.0, 1.0, 0.0, 1.0).empty());
    EXPECT_TRUE(generateShapePoints(DrawShape::Sine, 0.0, 4.0, 0.0, 0.0, 1.0).empty());
    EXPECT_EQ(estimateShapePointCount(DrawShape::Free, 0.0, 4.0, 1.0), 0);
}

TEST(LaneShapeGeneratorTest, TheEstimateIsAnUpperBoundAndNeedsNoGeneration) {
    for (auto shape : {DrawShape::Sine, DrawShape::Triangle, DrawShape::Saw, DrawShape::Square})
        for (double end : {1.0, 2.5, 7.75}) {
            const auto pts = generateShapePoints(shape, 0.0, end, 0.5, 0.0, 1.0);
            EXPECT_GE(estimateShapePointCount(shape, 0.0, end, 0.5), (long long)pts.size());
        }
    // A 1/128 grid over a thousand bars is millions of points: counted, not built.
    EXPECT_GT(estimateShapePointCount(DrawShape::Sine, 0.0, 4000.0, 1.0 / 32.0), 1000000);
}

TEST(LaneShapeGeneratorTest, DrawShapeStepsWrapAndKeysAreShiftDigitsInStripOrder) {
    EXPECT_EQ(synth::ui::nextDrawShape(DrawShape::Free), DrawShape::Line);
    EXPECT_EQ(synth::ui::nextDrawShape(DrawShape::Square), DrawShape::Free);
    EXPECT_EQ(synth::ui::drawShapeKeyDigit(DrawShape::Sine), 3);
    EXPECT_FALSE(synth::ui::isPeriodicShape(DrawShape::Line));
    EXPECT_TRUE(synth::ui::isPeriodicShape(DrawShape::Saw));
}
