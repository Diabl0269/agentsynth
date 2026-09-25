// FRO287: pure geometry for the modulation-ring depth band (modDepthBandRange /
// modDepthBandUsesNegativeColour, ModuleComponentModBand.h) -- no ModuleComponent, no LookAndFeel,
// no graph. See ModuleComponentModAmountGestureTests.cpp for the live-paint/gesture side.

#include "UI/Graph/ModuleComponent/ModuleComponentModBand.h"
#include <gtest/gtest.h>

using synth::ui::modDepthBandRange;
using synth::ui::modDepthBandUsesNegativeColour;

TEST(ModDepthBandRangeTest, BipolarSpansBothSidesOfBase) {
    // An LFO at full amount (1.0) around a centred knob reaches the whole 0..1 range.
    const auto r = modDepthBandRange(0.5f, 1.0f, /*bipolar*/ true);
    EXPECT_FLOAT_EQ(r.startNorm, 0.0f);
    EXPECT_FLOAT_EQ(r.endNorm, 1.0f);
}

TEST(ModDepthBandRangeTest, BipolarUsesTheMagnitudeOfANegativeAmount) {
    // A negative amount inverts the LFO's phase, not the reachable RANGE -- bipolar always spans
    // base +/- |amount|.
    const auto r = modDepthBandRange(0.5f, -0.2f, /*bipolar*/ true);
    EXPECT_NEAR(r.startNorm, 0.3f, 1e-5f);
    EXPECT_NEAR(r.endNorm, 0.7f, 1e-5f);
}

TEST(ModDepthBandRangeTest, UnipolarPositiveAmountRisesFromBase) {
    const auto r = modDepthBandRange(0.2f, 0.5f, /*bipolar*/ false);
    EXPECT_NEAR(r.startNorm, 0.2f, 1e-5f);
    EXPECT_NEAR(r.endNorm, 0.7f, 1e-5f);
}

TEST(ModDepthBandRangeTest, UnipolarNegativeAmountFallsFromBase) {
    const auto r = modDepthBandRange(0.6f, -0.4f, /*bipolar*/ false);
    EXPECT_NEAR(r.startNorm, 0.2f, 1e-5f);
    EXPECT_NEAR(r.endNorm, 0.6f, 1e-5f);
}

TEST(ModDepthBandRangeTest, ClampsToZeroOneAtBothEnds) {
    // base 0.9 +/- 0.3 -> [0.6, 1.2]: only the high end needs clamping.
    const auto hi = modDepthBandRange(0.9f, 0.3f, /*bipolar*/ true);
    EXPECT_NEAR(hi.startNorm, 0.6f, 1e-5f) << "the low end must not ALSO clamp just because the high end did";
    EXPECT_FLOAT_EQ(hi.endNorm, 1.0f);

    // base 0.05, a negative-amount pull below 0 must clamp too.
    const auto below = modDepthBandRange(0.05f, -0.5f, /*bipolar*/ false);
    EXPECT_FLOAT_EQ(below.startNorm, 0.0f);
}

TEST(ModDepthBandRangeTest, ZeroAmountCollapsesToAPoint) {
    const auto bipolar = modDepthBandRange(0.5f, 0.0f, true);
    EXPECT_FLOAT_EQ(bipolar.startNorm, bipolar.endNorm);
    const auto unipolar = modDepthBandRange(0.3f, 0.0f, false);
    EXPECT_FLOAT_EQ(unipolar.startNorm, unipolar.endNorm);
}

TEST(ModDepthBandColourTest, BipolarNeverUsesTheNegativeColour) {
    EXPECT_FALSE(modDepthBandUsesNegativeColour(-0.8f, /*bipolar*/ true));
    EXPECT_FALSE(modDepthBandUsesNegativeColour(0.8f, /*bipolar*/ true));
}

TEST(ModDepthBandColourTest, UnipolarUsesNegativeColourOnlyForANegativeAmount) {
    EXPECT_TRUE(modDepthBandUsesNegativeColour(-0.1f, /*bipolar*/ false));
    EXPECT_FALSE(modDepthBandUsesNegativeColour(0.1f, /*bipolar*/ false));
    EXPECT_FALSE(modDepthBandUsesNegativeColour(0.0f, /*bipolar*/ false));
}
