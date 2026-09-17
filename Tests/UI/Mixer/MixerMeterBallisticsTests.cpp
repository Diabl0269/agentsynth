// MixerMeterBallisticsTests.cpp -- FRO146: instant attack, ~20 dB/s release, and a 1.5 s
// peak-hold that then falls at ~20 dB/s (Source/UI/Mixer/MixerMeterBallistics.h). Every case
// drives advanceMeterBallistics() with an explicit elapsed time -- no wall clock, no juce::Timer.
#include "UI/Mixer/MixerMeterBallistics.h"
#include <gtest/gtest.h>

using namespace synth::ui;

TEST(MixerMeterBallisticsTest, AttackIsInstant) {
    MeterBallisticsState state;
    advanceMeterBallistics(state, -3.0f, 0.001f); // a tiny elapsed time -- attack must not ramp
    EXPECT_FLOAT_EQ(state.displayedDb, -3.0f);
}

TEST(MixerMeterBallisticsTest, ReleaseFallsAtTheStatedRateScaledByElapsedTime) {
    MeterBallisticsState state;
    advanceMeterBallistics(state, 0.0f, 0.0f); // snap to 0 dB with zero elapsed time
    ASSERT_FLOAT_EQ(state.displayedDb, 0.0f);

    advanceMeterBallistics(state, kMeterMinDb, 0.5f); // input drops to silence, half a second passes
    EXPECT_NEAR(state.displayedDb, 0.0f - kMeterReleaseDbPerSecond * 0.5f, 1.0e-4f);
}

TEST(MixerMeterBallisticsTest, ReleaseNeverFallsBelowTheInput) {
    MeterBallisticsState state;
    advanceMeterBallistics(state, -5.0f, 0.0f);
    advanceMeterBallistics(state, -6.0f, 100.0f); // an absurdly long elapsed time
    EXPECT_FLOAT_EQ(state.displayedDb, -6.0f) << "release clamps at the (louder) input, never overshoots";
}

TEST(MixerMeterBallisticsTest, PeakHoldSnapsInstantlyToANewLouderPeak) {
    MeterBallisticsState state;
    advanceMeterBallistics(state, -10.0f, 0.01f);
    ASSERT_FLOAT_EQ(state.peakHoldDb, -10.0f);
    advanceMeterBallistics(state, 2.0f, 0.01f);
    EXPECT_FLOAT_EQ(state.peakHoldDb, 2.0f);
}

TEST(MixerMeterBallisticsTest, PeakHoldStaysPutForTheHoldDurationThenFalls) {
    MeterBallisticsState state;
    advanceMeterBallistics(state, 0.0f, 0.0f);
    ASSERT_FLOAT_EQ(state.peakHoldDb, 0.0f);

    // Quiet input for just under the hold duration: the line must not have moved yet.
    advanceMeterBallistics(state, kMeterMinDb, kMeterPeakHoldSeconds - 0.1f);
    EXPECT_FLOAT_EQ(state.peakHoldDb, 0.0f) << "still within the hold window";

    // One more tick past the hold window: now it falls, at the stated rate.
    advanceMeterBallistics(state, kMeterMinDb, 0.2f);
    const float expectedFall = kMeterPeakHoldFallDbPerSecond * 0.1f; // only the portion past the hold
    EXPECT_NEAR(state.peakHoldDb, 0.0f - expectedFall, 1.0e-3f);
}

TEST(MixerMeterBallisticsTest, ALouderInputDuringTheFallResetsTheHoldWindow) {
    MeterBallisticsState state;
    advanceMeterBallistics(state, 0.0f, 0.0f);
    advanceMeterBallistics(state, kMeterMinDb, kMeterPeakHoldSeconds + 1.0f); // well into the fall
    ASSERT_LT(state.peakHoldDb, 0.0f);

    advanceMeterBallistics(state, -1.0f, 0.001f); // a new peak, quieter than the original but louder
                                                  // than where the fall had gotten to
    EXPECT_FLOAT_EQ(state.peakHoldDb, -1.0f);
    EXPECT_FLOAT_EQ(state.peakHoldRemainingSeconds, kMeterPeakHoldSeconds) << "the hold window restarts";
}
