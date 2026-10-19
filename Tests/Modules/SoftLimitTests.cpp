// The shared output soft limiter (Source/Modules/SoftLimit.h): identity up to the knee, rounded above it.

#include "Modules/SoftLimit.h"

#include <cmath>
#include <gtest/gtest.h>

TEST(SoftLimit, IsTheIdentityAtOrBelowTheKnee) {
    for (float x = -0.9f; x <= 0.9f; x += 0.01f)
        EXPECT_EQ(synth::softLimit(x), x) << x;
    EXPECT_EQ(synth::softLimit(0.9f), 0.9f);
    EXPECT_EQ(synth::softLimit(-0.9f), -0.9f);
}

TEST(SoftLimit, IsMonotonicOddAndNeverAboveTheCeiling) {
    float previous = synth::softLimit(-100.0f);
    for (float x = -100.0f; x <= 100.0f; x += 0.01f) {
        const float y = synth::softLimit(x);
        EXPECT_GE(y, previous) << x;
        EXPECT_LE(std::fabs(y), 0.98f) << x;
        EXPECT_FLOAT_EQ(synth::softLimit(-x), -y) << x;
        previous = y;
    }
}

TEST(SoftLimit, RoundsAboveTheKneeWithoutAStep) {
    EXPECT_LT(synth::softLimit(1.0f), 1.0f);
    EXPECT_GT(synth::softLimit(1.0f), 0.9f);
    EXPECT_NEAR(synth::softLimit(0.9001f), 0.9001f, 1e-3f);
}
