// MeterColourStopsLiveApplyTests.cpp -- FRO147: proves the ONE shared cache
// (synth::theme::AppLookAndFeel::getMeterColourStops()) actually reaches both meter painters --
// MixerMeter (mixer columns/Master/a detached window) and ChannelChipComponent (track header
// chip) -- by pixel-sampling a real offscreen render before and after
// AppLookAndFeel::setMeterColourStopsOverride(), the same "prove the painter itself, not just the
// colour model" idiom MixerMeterPaintTests.cpp already uses. MainComponent's own re-read-on-
// settings-notify push (MainComponentCallbacks.cpp) is exercised by driving AppLookAndFeel
// directly here -- see that file's own FRO147 comment for why the tab has no more direct a route.

#include "UI/Mixer/MeterColourStops.h"
#include "UI/Mixer/MixerMeter.h"
#include "UI/Mixer/MixerMeterScale.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include "UI/Timeline/ChannelChipComponent.h"
#include <gtest/gtest.h>

using namespace synth::ui;

namespace {
constexpr int kWidth = 16;
constexpr int kHeight = 252; // matches MixerMeterPaintTests.cpp's own sizing rationale
int yForDb(float db) { return kHeight - juce::roundToInt(meterDbToFraction(db) * (float)kHeight); }
} // namespace

TEST(MeterColourStopsLiveApplyTest, MixerMeterPaintsTheOverrideStopsOnceOneIsSet) {
    synth::theme::AppLookAndFeel laf;
    laf.applyTheme(synth::theme::makeObsidian());

    MixerMeter meter;
    meter.setLookAndFeel(&laf);
    meter.setSize(kWidth, kHeight);
    meter.peakProvider = [](int leg) { return leg == 0 ? juce::Decibels::decibelsToGain(-40.0f) : 0.0f; };
    meter.refresh(1.0f);
    ASSERT_LT(meter.getDisplayedDbForTest(0), MeterColourStops::kMidFromDb);

    const juce::Colour customLow(0xffAA00AA);
    laf.setMeterColourStopsOverride(MeterColourStops({{kMeterMinDb, customLow}, {0.0f, juce::Colour(0xff00AAAA)}}));

    // SoftwareImageType(): on Windows the default (native) image type is Direct2D-backed, and
    // painting into it then reading pixels back on a GPU-less CI runner yields an all-zero image
    // (FRO242). Force a software-backed bitmap so getPixelAt() reads what paint() actually drew.
    juce::Image img(juce::Image::ARGB, kWidth, kHeight, true, juce::SoftwareImageType());
    juce::Graphics g(img);
    meter.paint(g);

    constexpr int x = 2;
    const float midLow = (kMeterMinDb + meter.getDisplayedDbForTest(0)) / 2.0f;
    EXPECT_EQ(img.getPixelAt(x, yForDb(midLow)), customLow) << "the painter must read the OVERRIDE, not the theme";

    meter.setLookAndFeel(nullptr);
}

TEST(MeterColourStopsLiveApplyTest, MixerMeterFollowsTheThemeAgainOnceTheOverrideIsCleared) {
    synth::theme::AppLookAndFeel laf;
    laf.applyTheme(synth::theme::makeObsidian());
    laf.setMeterColourStopsOverride(MeterColourStops({{kMeterMinDb, juce::Colour(0xffAA00AA)}}));
    laf.setMeterColourStopsOverride(std::nullopt); // "Reset to Theme"

    MixerMeter meter;
    meter.setLookAndFeel(&laf);
    meter.setSize(kWidth, kHeight);
    meter.peakProvider = [](int leg) { return leg == 0 ? juce::Decibels::decibelsToGain(-40.0f) : 0.0f; };
    meter.refresh(1.0f);

    juce::Image img(juce::Image::ARGB, kWidth, kHeight, true, juce::SoftwareImageType());
    juce::Graphics g(img);
    meter.paint(g);

    constexpr int x = 2;
    const float midLow = (kMeterMinDb + meter.getDisplayedDbForTest(0)) / 2.0f;
    EXPECT_EQ(img.getPixelAt(x, yForDb(midLow)), synth::theme::makeObsidian().colors.meterFill);

    meter.setLookAndFeel(nullptr);
}

TEST(MeterColourStopsLiveApplyTest, ChannelChipReadsTheSameOverrideAsMixerMeter) {
    synth::theme::AppLookAndFeel laf;
    laf.applyTheme(synth::theme::makeObsidian());

    ChannelChipComponent chip;
    chip.setLookAndFeel(&laf);
    chip.setSize(120, 24);
    chip.setChannelName("Test");
    chip.setMeterLevel(juce::Decibels::decibelsToGain(-20.0f)); // well into the low zone, and
                                                                // loud enough for a wide fill

    const juce::Colour customLow(0xff00FF88);
    laf.setMeterColourStopsOverride(MeterColourStops({{kMeterMinDb, customLow}, {0.0f, juce::Colour(0xffFF0088)}}));

    juce::Image img(juce::Image::ARGB, chip.getWidth(), chip.getHeight(), true, juce::SoftwareImageType());
    juce::Graphics g(img);
    chip.paintButton(g, false, false);

    // Scan the whole chip rather than assuming the meter strip's exact geometry (content is
    // reduced(4, 1) before the meter area is removed from ITS right edge, not from the chip's own
    // bounds) -- this test only cares whether the override colour was painted anywhere at all.
    bool sawCustomLow = false;
    for (int x = 0; x < img.getWidth() && !sawCustomLow; ++x)
        for (int y = 0; y < img.getHeight() && !sawCustomLow; ++y)
            if (img.getPixelAt(x, y) == customLow)
                sawCustomLow = true;
    EXPECT_TRUE(sawCustomLow) << "the chip must read the SAME cached override MixerMeter does";

    chip.setLookAndFeel(nullptr);
}
