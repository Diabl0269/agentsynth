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
