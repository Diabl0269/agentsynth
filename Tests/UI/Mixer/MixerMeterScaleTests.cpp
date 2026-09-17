// MixerMeterScaleTests.cpp -- FRO146: the meter's -60..+3 dBFS scale (Source/UI/Mixer/
// MixerMeterScale.h), tested at its boundaries directly rather than reverse-engineered from
// pixels. FRO146 follow-up: the dB<->position mapping is Cubase's own piecewise-linear taper, not
// linear in dB -- exact breakpoints, monotonicity end to end, and the forward/inverse round-trip.
#include "UI/Mixer/MixerMeterScale.h"
#include <cmath>
#include <gtest/gtest.h>
#include <limits>

using namespace synth::ui;

TEST(MixerMeterScaleTest, LinearToDbBoundaries) {
    EXPECT_NEAR(meterLinearToDb(1.0f), 0.0f, 1.0e-4f);
    EXPECT_NEAR(meterLinearToDb(juce::Decibels::decibelsToGain(-18.0f)), -18.0f, 1.0e-3f);
    EXPECT_NEAR(meterLinearToDb(juce::Decibels::decibelsToGain(3.0f)), 3.0f, 1.0e-3f);
}

TEST(MixerMeterScaleTest, LinearToDbFloorsAtMinDbForSilenceAndInvalidInput) {
    EXPECT_EQ(meterLinearToDb(0.0f), kMeterMinDb);
    EXPECT_EQ(meterLinearToDb(-1.0f), kMeterMinDb) << "a negative magnitude should never occur, but must not crash";
    EXPECT_EQ(meterLinearToDb(std::numeric_limits<float>::quiet_NaN()), kMeterMinDb);
}

TEST(MixerMeterScaleTest, DbToFractionBoundaries) {
    EXPECT_FLOAT_EQ(meterDbToFraction(kMeterMinDb), 0.0f);
    EXPECT_FLOAT_EQ(meterDbToFraction(kMeterMaxDb), 1.0f);
}

TEST(MixerMeterScaleTest, DbToFractionClampsBeyondTheScale) {
    EXPECT_FLOAT_EQ(meterDbToFraction(kMeterMinDb - 20.0f), 0.0f);
    EXPECT_FLOAT_EQ(meterDbToFraction(kMeterMaxDb + 20.0f), 1.0f) << "well above +3 dB still clamps to full";
}

TEST(MixerMeterScaleTest, TickTableIsOrderedLoudestFirstAndBracketsTheScale) {
    ASSERT_EQ(kMeterTickDb.size(), 10u);
    EXPECT_FLOAT_EQ(kMeterTickDb.front(), kMeterMaxDb);
    EXPECT_FLOAT_EQ(kMeterTickDb.back(), kMeterMinDb);
    for (size_t i = 1; i < kMeterTickDb.size(); ++i)
        EXPECT_LT(kMeterTickDb[i], kMeterTickDb[i - 1]) << "ticks must strictly descend";
}

// ============================================================================
// FRO146 follow-up: the Cubase-style taper -- exact breakpoints, monotonicity, inverse round-trip.
// ============================================================================

TEST(MixerMeterScaleTest, DbToFractionHitsEveryBreakpointExactly) {
    // Every kMeterTickDb entry is one of the taper's own breakpoints -- see MixerMeterScale.h's
    // detail::kMeterTaperBreakpoints table this pins against directly.
    EXPECT_NEAR(meterDbToFraction(3.0f), 1.00f, 1.0e-5f);
    EXPECT_NEAR(meterDbToFraction(0.0f), 0.92f, 1.0e-5f);
    EXPECT_NEAR(meterDbToFraction(-6.0f), 0.795f, 1.0e-5f);
    EXPECT_NEAR(meterDbToFraction(-12.0f), 0.67f, 1.0e-5f);
    EXPECT_NEAR(meterDbToFraction(-18.0f), 0.545f, 1.0e-5f);
    EXPECT_NEAR(meterDbToFraction(-24.0f), 0.42f, 1.0e-5f);
    EXPECT_NEAR(meterDbToFraction(-30.0f), 0.31f, 1.0e-5f);
    EXPECT_NEAR(meterDbToFraction(-40.0f), 0.165f, 1.0e-5f);
    EXPECT_NEAR(meterDbToFraction(-50.0f), 0.07f, 1.0e-5f);
    EXPECT_NEAR(meterDbToFraction(-60.0f), 0.00f, 1.0e-5f);
}

TEST(MixerMeterScaleTest, TheWorkingRangeGetsFarMoreRoomPerDbThanTheQuietTail) {
    // The whole point of the taper (Cubase's proportions): 0..-12 dB, where a mix actually sits,
    // reads with several times the resolution per dB of the -50..-60 dB tail.
    const float workingPerDb = (meterDbToFraction(0.0f) - meterDbToFraction(-12.0f)) / 12.0f;
    const float tailPerDb = (meterDbToFraction(-50.0f) - meterDbToFraction(-60.0f)) / 10.0f;
    EXPECT_GT(workingPerDb, tailPerDb * 2.5f) << "0..-12 dB must read far more legibly than the -50..-60 tail";
}

TEST(MixerMeterScaleTest, DbToFractionIsMonotonicAcrossTheWholeScale) {
    float previous = meterDbToFraction(kMeterMinDb);
    for (float db = kMeterMinDb; db <= kMeterMaxDb; db += 0.25f) {
        const float fraction = meterDbToFraction(db);
        EXPECT_GE(fraction, previous) << "db=" << db;
        previous = fraction;
    }
}

TEST(MixerMeterScaleTest, FractionToDbIsMonotonicAcrossTheWholeScale) {
    float previous = meterFractionToDb(0.0f);
    for (float f = 0.0f; f <= 1.0f; f += 0.01f) {
        const float db = meterFractionToDb(f);
        EXPECT_GE(db, previous) << "fraction=" << f;
        previous = db;
    }
}

TEST(MixerMeterScaleTest, FractionToDbBoundaries) {
    EXPECT_NEAR(meterFractionToDb(0.0f), kMeterMinDb, 1.0e-4f);
    EXPECT_NEAR(meterFractionToDb(1.0f), kMeterMaxDb, 1.0e-4f);
}

TEST(MixerMeterScaleTest, FractionToDbClampsBeyondZeroAndOne) {
    EXPECT_NEAR(meterFractionToDb(-0.5f), kMeterMinDb, 1.0e-4f);
    EXPECT_NEAR(meterFractionToDb(1.5f), kMeterMaxDb, 1.0e-4f);
}

TEST(MixerMeterScaleTest, DbToFractionThenBackRoundTripsAcrossTheWholeScale) {
    for (float db = kMeterMinDb; db <= kMeterMaxDb; db += 0.5f)
        EXPECT_NEAR(meterFractionToDb(meterDbToFraction(db)), db, 1.0e-3f) << "db=" << db;
}

TEST(MixerMeterScaleTest, FractionToDbThenBackRoundTripsAcrossTheWholeScale) {
    for (float f = 0.0f; f <= 1.0f; f += 0.02f)
        EXPECT_NEAR(meterDbToFraction(meterFractionToDb(f)), f, 1.0e-3f) << "fraction=" << f;
}
