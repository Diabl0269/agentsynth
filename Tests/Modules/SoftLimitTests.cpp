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

TEST(PolySumHeadroom, LeavesEverythingUpToTheKneeAndNeverMakesALouderChordQuieter) {
    EXPECT_EQ(synth::polySumHeadroom(0.0f), 1.0f);
    EXPECT_EQ(synth::polySumHeadroom(synth::kPolySumKnee), 1.0f);
    float previousGain = 1.0f;
    float previousLevel = 0.0f;
    for (float peak = 0.05f; peak <= 8.0f; peak += 0.05f) {
        const float gain = synth::polySumHeadroom(peak);
        EXPECT_LE(gain, previousGain) << peak;
        EXPECT_GE(peak * gain, previousLevel) << "a bigger nominal sum is never quieter: " << peak;
        previousGain = gain;
        previousLevel = peak * gain;
    }
    EXPECT_NEAR(synth::polySumHeadroom(synth::kPolySumKnee * 1.0001f), 1.0f, 1e-3f) << "no step at the knee";
}
