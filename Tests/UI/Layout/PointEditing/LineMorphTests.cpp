// LineMorphTests.cpp -- the pure maths of a line easing between two shapes (LineMorph.h): the curve editor's blend by x
// (by length where dots stack at one x) and the lane's blend across steps, at both ends of the weight and between.
#include "UI/Layout/PointEditing/LineMorph.h"
#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>

using namespace synth::ui::line_morph;

namespace {

// A peak at x = 50 (the old line) and the straight line under it (the peak's dot deleted).
const Line kPeak{{0.0f, 100.0f}, {50.0f, 0.0f}, {100.0f, 100.0f}};
const Line kFlat{{0.0f, 100.0f}, {100.0f, 100.0f}};

} // namespace

TEST(LineMorph, TheBlendIsTheOldLineAtWeightOneAndTheNewLineAtWeightZero) {
    for (const auto blender : {&blend, &blendAcrossSteps}) {
        const auto old = blender(kPeak, kFlat, 1.0f);
        EXPECT_FLOAT_EQ(lineYAt(old, 50.0f), 0.0f);
        EXPECT_FLOAT_EQ(lineYAt(old, 25.0f), 50.0f);
        const auto now = blender(kPeak, kFlat, 0.0f);
        EXPECT_EQ(now, kFlat) << "weight zero is the new line itself";
    }
}

TEST(LineMorph, HalfWayEveryLevelLiesBetweenTheTwoLines) {
    for (const auto blender : {&blend, &blendAcrossSteps}) {
        const auto mid = blender(kPeak, kFlat, 0.5f);
        for (float x = 0.0f; x <= 100.0f; x += 5.0f) {
            const float was = lineYAt(kPeak, x), is = lineYAt(kFlat, x);
            EXPECT_NEAR(lineYAt(mid, x), (was + is) * 0.5f, 1e-3f) << "at x " << x;
        }
    }
}

// Dots stacked at one x: one x names two levels, so the curve editor blends by the fraction of the length.
TEST(LineMorph, AVerticalStepBlendsByLength) {
    const Line step{{0.0f, 100.0f}, {50.0f, 100.0f}, {50.0f, 0.0f}, {100.0f, 0.0f}};
    EXPECT_TRUE(hasVerticalStep(step));
    EXPECT_FALSE(hasVerticalStep(kPeak));
    const auto mid = blend(step, kFlat, 0.5f);
    EXPECT_EQ(mid, blendByLength(step, kFlat, 0.5f));
    // Each sample a fraction of the way along one line meets the point as far along the other: the step's foot (a
    // quarter of its 200 px) with (25, 100), and its top corner (three quarters) with (75, 100).
    const auto has = [&mid](float x, float y) {
        return std::any_of(mid.begin(), mid.end(),
                           [x, y](auto p) { return std::abs(p.x - x) < 1e-3f && std::abs(p.y - y) < 1e-3f; });
    };
    EXPECT_TRUE(has(37.5f, 100.0f));
    EXPECT_TRUE(has(62.5f, 50.0f));
}

// A lane's line has a step at a point (the value just before it, then its own): the step stays a step while it eases,
// and its two levels each lie between the old line's and the new one's.
TEST(LineMorph, AcrossStepsAStepStaysAStepAndEasesBothItsLevels) {
    const Line hold{{0.0f, 100.0f}, {50.0f, 100.0f}, {50.0f, 0.0f}, {100.0f, 0.0f}};
    const auto mid = blendAcrossSteps(hold, kFlat, 0.5f);
    std::vector<float> atStep;
    for (const auto& p : mid)
        if (p.x == 50.0f)
            atStep.push_back(p.y);
    ASSERT_EQ(atStep.size(), 2u) << "the step keeps its arriving and its leaving level";
    EXPECT_FLOAT_EQ(atStep[0], 100.0f) << "arriving: both lines are at 100";
    EXPECT_FLOAT_EQ(atStep[1], 50.0f) << "leaving: half way from 0 to 100";
    for (std::size_t i = 1; i < mid.size(); ++i)
        EXPECT_GE(mid[i].x, mid[i - 1].x) << "the line still runs left to right";
}
