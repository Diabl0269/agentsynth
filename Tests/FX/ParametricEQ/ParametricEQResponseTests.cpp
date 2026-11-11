// ParametricEQResponseTests.cpp
//
// Every band type and cut slope: the analytic curve (what the EQ window draws) matches the tone the module
// actually plays within 0.5 dB, and a cut falls by its slope an octave past its corner. Corners sit at 1 kHz
// and probes stay at or below 4 kHz, where the bilinear transform's warping is a few hundredths of a dB for
// these shapes; far above a quarter of the sample rate the drawn curve and the audio drift apart by design
// (the analog prototype is drawn, prewarped to the centre).

#include "ParametricEQTestHelpers.h"

using namespace eqtest;

namespace {

constexpr float kCorner = 1000.0f;
constexpr float kProbes[] = {125.0f, 250.0f, 500.0f, 707.0f, 1000.0f, 1414.0f, 2000.0f, 4000.0f};
// Below this the measured tone is too quiet to compare to 0.5 dB.
constexpr float kFloorDb = -50.0f;

struct Shape {
    BandType type;
    float gainDb;
    float q;
};

const Shape kShapes[] = {
    {BandType::Peak, 9.0f, 1.5f},        {BandType::Peak, -12.0f, 0.7f}, {BandType::LowShelf, 8.0f, 0.7f},
    {BandType::HighShelf, -10.0f, 1.0f}, {BandType::Notch, 0.0f, 2.0f},  {BandType::BandPass, 0.0f, 1.0f},
    {BandType::Tilt, 12.0f, 0.7f},       {BandType::Tilt, -6.0f, 0.7f},
};

const char* typeName(BandType type) {
    static const char* names[] = {"LowShelf", "Peak", "HighShelf", "LowCut", "HighCut", "Notch", "BandPass", "Tilt"};
    return names[(int)type];
}

/** A prepared EQ with slot 6 (a hidden slot, so every slot's engine path is the same) at the given shape. */
float measuredDb(BandType type, float gainDb, float q, int slope, float probe) {
    ParametricEQModule eq;
    enableShapedBand(eq, 5, type, kCorner, gainDb, q, slope);
    eq.setPlayConfigDetails(17, 17, kSampleRate, kBlockSize);
    eq.prepareToPlay(kSampleRate, kBlockSize);
    return measureGainDb(eq, probe, 17);
}

} // namespace

TEST(ParametricEQResponse, EveryNonCutTypeSoundsAsItIsDrawn) {
    for (const auto& shape : kShapes)
        for (const float probe : kProbes) {
            const float drawn = ParametricEQModule::bandMagnitudeDb(shape.type, kCorner, shape.gainDb, shape.q, probe);
            if (drawn < kFloorDb)
                continue;
            EXPECT_NEAR(measuredDb(shape.type, shape.gainDb, shape.q, 12, probe), drawn, 0.5f)
                << typeName(shape.type) << " gain " << shape.gainDb << " at " << probe << " Hz";
        }
}

TEST(ParametricEQResponse, EveryCutSlopeSoundsAsItIsDrawn) {
    for (const BandType type : {BandType::LowCut, BandType::HighCut})
        for (const int slope : {6, 12, 18, 24, 48, 96})
            for (const float probe : kProbes) {
                const float drawn = ParametricEQModule::bandMagnitudeDb(type, kCorner, 0.0f,
                                                                        ParametricEQModule::kDefaultQ, probe, slope);
                // Above 2 kHz a steep high cut's warping (slope x the octaves tan() stretches) passes 0.5 dB.
                if (drawn < kFloorDb || probe > 2000.0f)
                    continue;
                EXPECT_NEAR(measuredDb(type, 0.0f, ParametricEQModule::kDefaultQ, slope, probe), drawn, 0.5f)
                    << typeName(type) << " " << slope << " dB/oct at " << probe << " Hz";
            }
}

// The digital cascade against the drawn curve across the whole slope list, down to -120 dB: a check on the stage
// design itself that needs no audio measurement floor. Warping grows with depth (a 24 dB/oct high cut is about
// 0.75 dB under its drawn -48 dB at 4 kHz), so the tolerance is 2% of the depth.
TEST(ParametricEQResponse, EveryCutCascadeMatchesItsDrawnCurve) {
    for (const BandType type : {BandType::LowCut, BandType::HighCut})
        for (const int slope : synth::eq::kSlopesDbPerOct)
            for (const float q : {ParametricEQModule::kDefaultQ, 2.0f})
                for (const float probe : kProbes) {
                    const float drawn = ParametricEQModule::bandMagnitudeDb(type, kCorner, 0.0f, q, probe, slope);
                    if (drawn < -120.0f)
                        continue;
                    const float digital = digitalCascadeDb(type, slope, kCorner, 0.0f, q, probe);
                    EXPECT_NEAR(digital, drawn, std::max(0.5f, std::abs(drawn) * 0.02f))
                        << typeName(type) << " " << slope << " dB/oct Q " << q << " at " << probe << " Hz";
                }
}

TEST(ParametricEQResponse, ACutFallsByItsSlopeOneOctavePastTheCorner) {
    for (const BandType type : {BandType::LowCut, BandType::HighCut}) {
        const float octavePast = type == BandType::LowCut ? kCorner * 0.5f : kCorner * 2.0f;
        for (const int slope : synth::eq::kSlopesDbPerOct) {
            const auto label = std::string(typeName(type)) + " " + std::to_string(slope) + " dB/oct";
            // Butterworth: -3 dB at the corner for every order, then the slope.
            EXPECT_NEAR(digitalCascadeDb(type, slope, kCorner, 0.0f, ParametricEQModule::kDefaultQ, kCorner), -3.01f,
                        0.1f)
                << label;
            EXPECT_NEAR(digitalCascadeDb(type, slope, kCorner, 0.0f, ParametricEQModule::kDefaultQ, octavePast),
                        -(float)slope, 1.5f)
                << label;
            EXPECT_NEAR(ParametricEQModule::bandMagnitudeDb(type, kCorner, 0.0f, ParametricEQModule::kDefaultQ,
                                                            octavePast, slope),
                        -(float)slope, 1.5f)
                << label;
            if (slope <= 36) // a measurable level: deeper than this the tone sits in the float noise
                EXPECT_NEAR(measuredDb(type, 0.0f, ParametricEQModule::kDefaultQ, slope, octavePast), -(float)slope,
                            1.5f)
                    << label;
        }
    }
}

TEST(ParametricEQResponse, StageCountsFollowTheSlope) {
    const int expected[] = {1, 1, 2, 2, 3, 3, 4, 6, 8};
    for (size_t i = 0; i < synth::eq::kSlopesDbPerOct.size(); ++i) {
        EXPECT_EQ(synth::eq::stageCount(BandType::LowCut, synth::eq::kSlopesDbPerOct[i]), expected[i]);
        EXPECT_EQ(synth::eq::stageCount(BandType::Peak, synth::eq::kSlopesDbPerOct[i]), 1)
            << "slope applies to cuts only";
    }
}

// Slope is a cut's setting only: a bell drawn and played at 96 dB/oct is the same bell.
TEST(ParametricEQResponse, SlopeDoesNotChangeANonCutBand) {
    for (const auto& shape : kShapes)
        for (const float probe : {250.0f, 1000.0f, 4000.0f})
            EXPECT_FLOAT_EQ(ParametricEQModule::bandMagnitudeDb(shape.type, kCorner, shape.gainDb, shape.q, probe, 96),
                            ParametricEQModule::bandMagnitudeDb(shape.type, kCorner, shape.gainDb, shape.q, probe, 12))
                << typeName(shape.type);
}

TEST(ParametricEQResponse, TiltPivotsAtItsFrequencyAndSpansItsGain) {
    EXPECT_NEAR(ParametricEQModule::bandMagnitudeDb(BandType::Tilt, kCorner, 12.0f, 0.7f, kCorner), 0.0f, 1.0e-3f);
    EXPECT_NEAR(ParametricEQModule::bandMagnitudeDb(BandType::Tilt, kCorner, 12.0f, 0.7f, 20.0f), -6.0f, 0.1f);
    EXPECT_NEAR(ParametricEQModule::bandMagnitudeDb(BandType::Tilt, kCorner, 12.0f, 0.7f, 20000.0f), 6.0f, 0.1f);
}

TEST(ParametricEQResponse, ResponseDbSumsHiddenSlotsWithTheirTypeAndSlope) {
    auto bands = oneBandSnapshot(10, kCorner, 0.0f, ParametricEQModule::kDefaultQ);
    bands[10].type = BandType::HighCut;
    bands[10].slopeDbPerOct = 48;
    EXPECT_NEAR(ParametricEQModule::responseDb(bands, 0.0f, 2000.0f), -48.0f, 1.5f);
    bands[10].enabled = false;
    EXPECT_NEAR(ParametricEQModule::responseDb(bands, 0.0f, 2000.0f), 0.0f, 1.0e-4f);
}
