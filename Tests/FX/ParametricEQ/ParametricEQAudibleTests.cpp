// The Parametric EQ's knobs must reach the sound when they are moved AFTER prepareToPlay (the
// smoothers snap to the knobs there, so a test that sets parameters first hides a frozen target).
// Every case prepares first, then moves one parameter, then measures a tone against a reference.

#include "ParametricEQTestHelpers.h"

using namespace eqtest;

namespace {

/** A prepared EQ with `band` on at (freq, gain, q); the caller moves a knob afterwards. */
struct PreparedEQ {
    ParametricEQModule eq;
    PreparedEQ(int band, float freq, float gain, float q) {
        enableBand(eq, band, freq, gain, q);
        eq.setPlayConfigDetails(kNumChannels, kNumChannels, kSampleRate, kBlockSize);
        eq.prepareToPlay(kSampleRate, kBlockSize);
    }
    static constexpr int kNumChannels = 17; // audio + every CV jack, as the engine allocates them
    float db(float probeHz) { return measureGainDb(eq, probeHz, kNumChannels); }
};

} // namespace

TEST(ParametricEQAudible, Band1GainMovedAfterPrepareLiftsTheLows) {
    PreparedEQ p(0, 100.0f, 0.0f, ParametricEQModule::kDefaultQ);
    EXPECT_NEAR(p.db(40.0f), 0.0f, 0.5f);
    p.eq.setBandGain(0, 12.0f);
    EXPECT_GT(p.db(40.0f), 9.0f);
}

TEST(ParametricEQAudible, Band4GainMovedAfterPrepareLiftsTheHighs) {
    PreparedEQ p(3, 8000.0f, 0.0f, ParametricEQModule::kDefaultQ);
    EXPECT_NEAR(p.db(16000.0f), 0.0f, 0.5f);
    p.eq.setBandGain(3, 12.0f);
    EXPECT_GT(p.db(16000.0f), 9.0f);
}

TEST(ParametricEQAudible, Band1FreqMovedAfterPrepareShiftsTheShelfCorner) {
    PreparedEQ p(0, 100.0f, 12.0f, ParametricEQModule::kDefaultQ);
    const float before = p.db(1000.0f); // a decade above the corner: the shelf is gone
    EXPECT_LT(before, 1.5f);
    p.eq.setBandFreq(0, 4000.0f);
    EXPECT_GT(p.db(1000.0f), before + 6.0f);
}

TEST(ParametricEQAudible, Band4FreqMovedAfterPrepareShiftsTheShelfCorner) {
    PreparedEQ p(3, 8000.0f, 12.0f, ParametricEQModule::kDefaultQ);
    const float before = p.db(1000.0f); // far below the corner: the shelf is gone
    EXPECT_LT(before, 1.5f);
    p.eq.setBandFreq(3, 300.0f);
    EXPECT_GT(p.db(1000.0f), before + 6.0f);
}

TEST(ParametricEQAudible, BellQMovedAfterPrepareNarrowsTheBell) {
    for (const int band : {1, 2}) {
        const float centre = ParametricEQModule::defaultFreqFor(band);
        PreparedEQ p(band, centre, 12.0f, 0.7f);
        const float offCentre = centre * 1.6f;
        const float wide = p.db(offCentre);
        p.eq.setBandQ(band, 8.0f);
        EXPECT_LT(p.db(offCentre), wide - 4.0f) << "band " << band + 1;
    }
}

TEST(ParametricEQAudible, ShelfQMovedAfterPrepareReshapesTheCorner) {
    for (const int band : {0, 3}) {
        const float corner = ParametricEQModule::defaultFreqFor(band);
        PreparedEQ p(band, corner, 12.0f, 0.7f);
        // The corner itself sits at half the shelf gain whatever Q is; the overshoot is an octave out.
        const float probe = band == 0 ? corner * 2.0f : corner * 0.5f;
        const float before = p.db(probe);
        p.eq.setBandQ(band, 8.0f);
        EXPECT_GT(std::abs(p.db(probe) - before), 1.0f) << "band " << band + 1;
    }
}
