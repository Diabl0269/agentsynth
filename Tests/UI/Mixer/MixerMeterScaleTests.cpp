// MixerMeterScaleTests.cpp -- FRO146: the meter's -60..+3 dBFS scale (Source/UI/Mixer/
// MixerMeterScale.h), tested at its boundaries directly rather than reverse-engineered from
// pixels.
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
    EXPECT_NEAR(meterDbToFraction(0.0f), 60.0f / 63.0f, 1.0e-5f);
}

TEST(MixerMeterScaleTest, DbToFractionClampsBeyondTheScale) {
    EXPECT_FLOAT_EQ(meterDbToFraction(kMeterMinDb - 20.0f), 0.0f);
    EXPECT_FLOAT_EQ(meterDbToFraction(kMeterMaxDb + 20.0f), 1.0f) << "well above +3 dB still clamps to full";
}

TEST(MixerMeterScaleTest, TickTableIsOrderedLoudestFirstAndBracketsTheScale) {
    ASSERT_EQ(kMeterTickDb.size(), 9u);
    EXPECT_FLOAT_EQ(kMeterTickDb.front(), kMeterMaxDb);
    EXPECT_FLOAT_EQ(kMeterTickDb.back(), kMeterMinDb);
    for (size_t i = 1; i < kMeterTickDb.size(); ++i)
        EXPECT_LT(kMeterTickDb[i], kMeterTickDb[i - 1]) << "ticks must strictly descend";
}
