// Topic: the 4-point Hermite interpolator the clip streamer resamples with.

#include "Timeline/ClipResample.h"
#include <gtest/gtest.h>

TEST(ClipResampleTest, ExactAtIntegerPositions) {
    EXPECT_FLOAT_EQ(synth::hermite4(0.3f, -0.7f, 0.2f, 0.9f, 0.0f), -0.7f);
    // At x = 1 the next frame's own value comes back (continuity across segments).
    EXPECT_NEAR(synth::hermite4(0.3f, -0.7f, 0.2f, 0.9f, 1.0f), 0.2f, 1.0e-6f);
}

TEST(ClipResampleTest, ReproducesALinearRampExactly) {
    for (float x = 0.0f; x < 1.0f; x += 0.125f)
        EXPECT_NEAR(synth::hermite4(-1.0f, 0.0f, 1.0f, 2.0f, x), x, 1.0e-6f) << "x = " << x;
}
