// MixerMeterPaintTests.cpp -- FRO146: MixerMeter::paint actually draws POSITIONAL colour bands
// (Cubase/most DAWs' meter convention -- MeterColourStops::forEachBand), not one flat colour for
// the whole filled bar. Pixel-sampled off a real offscreen render, not just asserted through the
// colour-model unit tests (MeterColourStopsTests.cpp) -- this is the one place that proves
// MixerMeter itself actually calls forEachBand rather than colourForDb(displayedDb) for the fill.
#include "UI/Mixer/MeterColourStops.h"
#include "UI/Mixer/MixerMeter.h"
#include "UI/Mixer/MixerMeterScale.h"
#include "UI/Theme/Theme.h"
#include <gtest/gtest.h>
#include <juce_audio_basics/juce_audio_basics.h>

using namespace synth::ui;

namespace {

// Narrow enough to land in MixerMeter::paint's no-tick-labels branch (bounds.getWidth() <
// kBarsAreaWidth + kLabelMinWidth), so the ENTIRE component width is the bars area -- no need to
// know that branch's exact private pixel budget to find leg 0's bar column.
constexpr int kWidth = 16;
// 252 px tall -- coarse enough to build fast, fine enough that every sampled midpoint (computed via
// the SAME meterDbToFraction() the painter itself uses, so this works regardless of the exact
// taper) sits comfortably inside its own band, away from any edge.
constexpr int kHeight = 252;

int yForDb(float db) { return kHeight - juce::roundToInt(meterDbToFraction(db) * (float)kHeight); }

} // namespace

TEST(MixerMeterPaintTest, PaintsPositionalBandsNotOneFlatColourAcrossTheBar) {
    MixerMeter meter;
    meter.setSize(kWidth, kHeight);

    // Leg 0 well into the clip zone; leg 1 silent (so it never competes for pixels at x=2 below).
    meter.peakProvider = [](int leg) { return leg == 0 ? juce::Decibels::decibelsToGain(4.0f) : 0.0f; };
    meter.refresh(1.0f); // attack is instant regardless of the elapsed time passed in
    ASSERT_GT(meter.getDisplayedDbForTest(0), MeterColourStops::kClipFromDb)
        << "the bar must actually reach the clip zone for this test to mean anything";

    juce::Image img(juce::Image::ARGB, kWidth, kHeight, true);
    juce::Graphics g(img);
    EXPECT_NO_THROW(meter.paint(g));

    // No AppLookAndFeel installed -- MixerMeter::paint falls back to these same literal defaults.
    const synth::theme::Colors defaults;
    constexpr int x = 2; // inside leg 0's (the louder leg's) bar column regardless of the exact
                         // bar-width/gap split, since leg 0 always starts at the bars area's own
                         // left edge.

    const float midLow = (kMeterMinDb + MeterColourStops::kMidFromDb) / 2.0f;
    const float midMid = (MeterColourStops::kMidFromDb + MeterColourStops::kHighFromDb) / 2.0f;
    const float midHigh = (MeterColourStops::kHighFromDb + MeterColourStops::kClipFromDb) / 2.0f;
    const float midClip = (MeterColourStops::kClipFromDb + meter.getDisplayedDbForTest(0)) / 2.0f;

    EXPECT_EQ(img.getPixelAt(x, yForDb(midLow)), defaults.meterFill) << "low band";
    EXPECT_EQ(img.getPixelAt(x, yForDb(midMid)), defaults.meterMid) << "mid band";
    EXPECT_EQ(img.getPixelAt(x, yForDb(midHigh)), defaults.meterHigh) << "high band";
    EXPECT_EQ(img.getPixelAt(x, yForDb(midClip)), defaults.meterClip) << "clip band";

    // The four sampled colours must be pairwise distinct -- otherwise the assertions above could
    // pass by coincidence (e.g. every sample landing on the same solid colour).
    EXPECT_NE(defaults.meterFill, defaults.meterMid);
    EXPECT_NE(defaults.meterMid, defaults.meterHigh);
    EXPECT_NE(defaults.meterHigh, defaults.meterClip);
}

TEST(MixerMeterPaintTest, ABarThatNeverLeavesTheLowZonePaintsOnlyThatOneColour) {
    MixerMeter meter;
    meter.setSize(kWidth, kHeight);
    meter.peakProvider = [](int leg) { return leg == 0 ? juce::Decibels::decibelsToGain(-40.0f) : 0.0f; };
    meter.refresh(1.0f);
    ASSERT_LT(meter.getDisplayedDbForTest(0), MeterColourStops::kMidFromDb);

    juce::Image img(juce::Image::ARGB, kWidth, kHeight, true);
    juce::Graphics g(img);
    meter.paint(g);

    const synth::theme::Colors defaults;
    constexpr int x = 2;
    const float midLow = (kMeterMinDb + meter.getDisplayedDbForTest(0)) / 2.0f;
    EXPECT_EQ(img.getPixelAt(x, yForDb(midLow)), defaults.meterFill);
    // Nothing at the mid zone's own height -- the bar never reached that far.
    EXPECT_NE(img.getPixelAt(x, yForDb(MeterColourStops::kMidFromDb + 5.0f)), defaults.meterMid);
}
