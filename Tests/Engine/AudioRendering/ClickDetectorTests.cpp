// The shared click detector (ClickDetector.h) on signals whose answer is known.

#include "ClickDetector.h"
#include <cmath>
#include <gtest/gtest.h>

namespace {
std::vector<float> sine(int n, float hz = 220.0f, float amp = 0.5f) {
    std::vector<float> x((size_t)n);
    for (int i = 0; i < n; ++i)
        x[(size_t)i] = amp * std::sin(2.0f * 3.14159265f * hz * (float)i / 48000.0f);
    return x;
}
} // namespace

TEST(ClickDetector, ACleanSineHasNoClick) { EXPECT_TRUE(clickdetector::findClicks(sine(9600)).empty()); }

TEST(ClickDetector, AStepIsOneClickAtItsSample) {
    auto x = sine(9600);
    for (size_t i = 4000; i < x.size(); ++i)
        x[i] += 0.05f;
    const auto clicks = clickdetector::findClicks(x);
    ASSERT_EQ(clicks.size(), 1u);
    EXPECT_NEAR(clicks[0].index, 4000, 2);
    EXPECT_GT(clicks[0].size, 0.04f);
}

// A sine that changes pitch at sample 4000 (the phase carries on, so the value is continuous): a corner in the slope
// when it jumps to the new pitch, none when the pitch glides there over 64 samples.
TEST(ClickDetector, ASlopeCornerIsFoundButTheSameBendTakenSlowlyIsNot) {
    constexpr int kAt = 4000;
    const auto render = [](int glide) {
        std::vector<float> x(9600);
        double phase = 0.0;
        for (int i = 0; i < 9600; ++i) {
            const double u = glide == 0 ? (i >= kAt ? 1.0 : 0.0) : std::clamp((i - kAt) / (double)glide, 0.0, 1.0);
            const double hz = 220.0 + (330.0 - 220.0) * 0.5 * (1.0 - std::cos(3.14159265358979 * u));
            x[(size_t)i] = 0.5f * (float)std::sin(phase);
            phase += 2.0 * 3.14159265358979 * hz / 48000.0;
        }
        return x;
    };
    EXPECT_GE(clickdetector::findClicks(render(0)).size(), 1u);
    EXPECT_TRUE(clickdetector::findClicks(render(64)).empty());
}
