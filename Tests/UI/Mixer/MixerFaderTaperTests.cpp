// MixerFaderTaperTests.cpp -- FRO150 (docs/mixer_fader.md): the FADER's own Cubase-like taper
// (Source/UI/Mixer/MixerFaderTaper.h), tested directly against its breakpoints the same way
// MixerMeterScaleTests.cpp already tests the METER's taper -- exact breakpoints, monotonicity both
// directions, the forward/inverse round-trip, and clamping beyond -60/+12 dB and beyond 0/1.
#include "UI/Mixer/MixerFaderTaper.h"
#include <gtest/gtest.h>

using namespace synth::ui;

TEST(MixerFaderTaperTest, DbToFractionHitsEveryBreakpointExactly) {
    EXPECT_NEAR(faderDbToFraction(-60.0f), 0.000f, 1.0e-5f);
    EXPECT_NEAR(faderDbToFraction(-50.0f), 0.040f, 1.0e-5f);
    EXPECT_NEAR(faderDbToFraction(-40.0f), 0.073f, 1.0e-5f);
    EXPECT_NEAR(faderDbToFraction(-30.0f), 0.130f, 1.0e-5f);
    EXPECT_NEAR(faderDbToFraction(-20.0f), 0.230f, 1.0e-5f);
    EXPECT_NEAR(faderDbToFraction(-15.0f), 0.308f, 1.0e-5f);
    EXPECT_NEAR(faderDbToFraction(-10.0f), 0.409f, 1.0e-5f);
    EXPECT_NEAR(faderDbToFraction(-5.0f), 0.537f, 1.0e-5f);
    EXPECT_NEAR(faderDbToFraction(0.0f), 0.710f, 1.0e-5f) << "0 dB must sit exactly at 0.71 (the ticket's own anchor)";
    EXPECT_NEAR(faderDbToFraction(6.0f), 0.900f, 1.0e-5f);
    EXPECT_NEAR(faderDbToFraction(12.0f), 1.000f, 1.0e-5f);
}

TEST(MixerFaderTaperTest, FractionToDbHitsEveryBreakpointExactly) {
    EXPECT_NEAR(faderFractionToDb(0.000f), -60.0f, 1.0e-4f);
    EXPECT_NEAR(faderFractionToDb(0.040f), -50.0f, 1.0e-3f);
    EXPECT_NEAR(faderFractionToDb(0.073f), -40.0f, 1.0e-3f);
    EXPECT_NEAR(faderFractionToDb(0.130f), -30.0f, 1.0e-3f);
    EXPECT_NEAR(faderFractionToDb(0.230f), -20.0f, 1.0e-3f);
    EXPECT_NEAR(faderFractionToDb(0.308f), -15.0f, 1.0e-3f);
    EXPECT_NEAR(faderFractionToDb(0.409f), -10.0f, 1.0e-3f);
    EXPECT_NEAR(faderFractionToDb(0.537f), -5.0f, 1.0e-3f);
    EXPECT_NEAR(faderFractionToDb(0.710f), 0.0f, 1.0e-3f);
    EXPECT_NEAR(faderFractionToDb(0.900f), 6.0f, 1.0e-3f);
    EXPECT_NEAR(faderFractionToDb(1.000f), 12.0f, 1.0e-4f);
}

TEST(MixerFaderTaperTest, BoundariesMatchKMinKMax) {
    EXPECT_FLOAT_EQ(faderDbToFraction(kFaderMinDb), 0.0f);
    EXPECT_FLOAT_EQ(faderDbToFraction(kFaderMaxDb), 1.0f);
    EXPECT_FLOAT_EQ(faderFractionToDb(0.0f), kFaderMinDb);
    EXPECT_FLOAT_EQ(faderFractionToDb(1.0f), kFaderMaxDb);
}

TEST(MixerFaderTaperTest, ClampsBeyondTheScale) {
    EXPECT_FLOAT_EQ(faderDbToFraction(kFaderMinDb - 40.0f), 0.0f);
    EXPECT_FLOAT_EQ(faderDbToFraction(kFaderMaxDb + 40.0f), 1.0f);
    EXPECT_FLOAT_EQ(faderFractionToDb(-5.0f), kFaderMinDb);
    EXPECT_FLOAT_EQ(faderFractionToDb(5.0f), kFaderMaxDb);
}

TEST(MixerFaderTaperTest, DbToFractionIsMonotonicAcrossTheWholeScale) {
    float prevFraction = faderDbToFraction(kFaderMinDb);
    for (float db = kFaderMinDb + 0.1f; db <= kFaderMaxDb; db += 0.1f) {
        const float fraction = faderDbToFraction(db);
        EXPECT_GE(fraction, prevFraction) << "db=" << db;
        prevFraction = fraction;
    }
}

TEST(MixerFaderTaperTest, FractionToDbIsMonotonicAcrossTheWholeScale) {
    float prevDb = faderFractionToDb(0.0f);
    for (float fraction = 0.001f; fraction <= 1.0f; fraction += 0.001f) {
        const float db = faderFractionToDb(fraction);
        EXPECT_GE(db, prevDb) << "fraction=" << fraction;
        prevDb = db;
    }
}

TEST(MixerFaderTaperTest, ForwardAndInverseRoundTrip) {
    for (float db = kFaderMinDb; db <= kFaderMaxDb; db += 0.37f) {
        const float fraction = faderDbToFraction(db);
        const float roundTripped = faderFractionToDb(fraction);
        EXPECT_NEAR(roundTripped, db, 0.02f) << "db=" << db;
    }
    for (float fraction = 0.0f; fraction <= 1.0f; fraction += 0.031f) {
        const float db = faderFractionToDb(fraction);
        const float roundTripped = faderDbToFraction(db);
        EXPECT_NEAR(roundTripped, fraction, 0.005f) << "fraction=" << fraction;
    }
}

TEST(MixerFaderTaperTest, BottomOfTheTaperCompressesMoreDbPerFractionThanNearUnity) {
    // The ticket's whole point: dragging the SAME px near the bottom moves MORE dB than the same
    // px near 0 dB, because the taper compresses -60..-50 into a small slice of travel.
    const float fractionPerDbNearBottom = (faderDbToFraction(-50.0f) - faderDbToFraction(-60.0f)) / 10.0f;
    const float fractionPerDbNearUnity = (faderDbToFraction(0.0f) - faderDbToFraction(-5.0f)) / 5.0f;
    EXPECT_LT(fractionPerDbNearBottom, fractionPerDbNearUnity)
        << "the bottom decade should occupy LESS fraction-per-dB than the -5..0 dB segment";
}
