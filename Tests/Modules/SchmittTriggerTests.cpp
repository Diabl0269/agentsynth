#include "Modules/SchmittTrigger.h"
#include <gtest/gtest.h>

TEST(SchmittTriggerTest, StartsLow) {
    SchmittTrigger t;
    EXPECT_FALSE(t.high);
    EXPECT_EQ(t.process(0.0f, 0.5f), SchmittTrigger::Edge::None);
    EXPECT_FALSE(t.high);
}

TEST(SchmittTriggerTest, RisingEdgeArmsAboveThreshold) {
    SchmittTrigger t;
    EXPECT_EQ(t.process(0.6f, 0.5f), SchmittTrigger::Edge::Rising);
    EXPECT_TRUE(t.high);
}

TEST(SchmittTriggerTest, StaysHighInsideHysteresisGap) {
    SchmittTrigger t;
    t.process(0.6f, 0.5f);
    EXPECT_EQ(t.process(0.48f, 0.5f), SchmittTrigger::Edge::None);
    EXPECT_TRUE(t.high);
}

TEST(SchmittTriggerTest, FallingEdgeRequiresHysteresisGap) {
    SchmittTrigger t;
    t.process(1.0f, 0.5f);
    EXPECT_EQ(t.process(0.44f, 0.5f), SchmittTrigger::Edge::Falling);
    EXPECT_FALSE(t.high);
}

TEST(SchmittTriggerTest, ResetClearsArmedState) {
    SchmittTrigger t;
    t.process(1.0f, 0.5f);
    t.reset();
    EXPECT_FALSE(t.high);
    EXPECT_EQ(t.process(0.6f, 0.5f), SchmittTrigger::Edge::Rising);
}

TEST(SchmittTriggerTest, EqualToThresholdDoesNotArm) {
    SchmittTrigger t;
    EXPECT_EQ(t.process(0.5f, 0.5f), SchmittTrigger::Edge::None);
}

// Regression test for FRO1427: a threshold below the hysteresis gap needed a sample below zero to fall, so a gate
// resting at 0 left the trigger high forever (an ADSR whose Threshold was 0.001 never released).
// Kinds: live-reconfigure
TEST(SchmittTriggerTest, GateRestingAtZeroFallsBelowAThresholdUnderTheGap) {
    for (const float threshold : {0.001f, 0.01f, 0.04f, 0.09f}) {
        SchmittTrigger t;
        EXPECT_EQ(t.process(1.0f, threshold), SchmittTrigger::Edge::Rising) << threshold;
        EXPECT_EQ(t.process(0.0f, threshold), SchmittTrigger::Edge::Falling) << threshold;
        EXPECT_FALSE(t.high) << threshold;
    }
}

TEST(SchmittTriggerTest, LowThresholdStillHasAGapSoItDoesNotChatter) {
    SchmittTrigger t;
    t.process(1.0f, 0.02f);
    EXPECT_EQ(t.process(0.015f, 0.02f), SchmittTrigger::Edge::None);
    EXPECT_EQ(t.process(0.005f, 0.02f), SchmittTrigger::Edge::Falling);
}
