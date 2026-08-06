// EQCurveTests.cpp
// Tests for the Parametric EQ visualiser and the axis maths it shares with
// FrequencyResponseComponent (issue #154):
//   • synth::ui::FrequencyGrid — log-frequency and dB mappings, their inverses, label formatting
//   • FrequencyResponseComponent's public statics still agree with the shared grid after the
//     mapping code was hoisted out of it
//   • EQCurveComponent — paint smoke tests (flat, boosted, with the spectrum overlay on) and
//     the dB window it plots over

#include "../Source/Modules/FX/ParametricEQModule.h"
#include "../Source/UI/EQCurveComponent.h"
#include "../Source/UI/FrequencyGrid.h"
#include "../Source/UI/FrequencyResponseComponent.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

using synth::ui::FrequencyGrid;

namespace {

// True if the image has at least one non-transparent pixel.
bool hasOpaquePixel(const juce::Image& img) {
    for (int y = 0; y < img.getHeight(); ++y)
        for (int x = 0; x < img.getWidth(); ++x)
            if (img.getPixelAt(x, y).getAlpha() > 0)
                return true;
    return false;
}

} // namespace

// ---------------------------------------------------------------------------
// FrequencyGrid — frequency axis
// ---------------------------------------------------------------------------

TEST(FrequencyGridTest, FreqToXSpansTheFullWidth) {
    constexpr float w = 400.0f;
    EXPECT_NEAR(FrequencyGrid::freqToX(FrequencyGrid::kMinFreq, w), 0.0f, 0.01f);
    EXPECT_NEAR(FrequencyGrid::freqToX(FrequencyGrid::kMaxFreq, w), w, 0.01f);
}

TEST(FrequencyGridTest, FreqToXIsLogarithmic) {
    constexpr float w = 400.0f;
    // Equal frequency ratios must map to equal pixel distances on a log axis.
    const float d1 = FrequencyGrid::freqToX(200.0f, w) - FrequencyGrid::freqToX(20.0f, w);
    const float d2 = FrequencyGrid::freqToX(2000.0f, w) - FrequencyGrid::freqToX(200.0f, w);
    const float d3 = FrequencyGrid::freqToX(20000.0f, w) - FrequencyGrid::freqToX(2000.0f, w);
    EXPECT_NEAR(d1, d2, 0.01f);
    EXPECT_NEAR(d2, d3, 0.01f);
}

TEST(FrequencyGridTest, FreqToXIsMonotonic) {
    constexpr float w = 512.0f;
    float previous = -1.0f;
    for (float freq = 20.0f; freq <= 20000.0f; freq *= 1.3f) {
        const float x = FrequencyGrid::freqToX(freq, w);
        EXPECT_GT(x, previous) << "at " << freq << " Hz";
        previous = x;
    }
}

TEST(FrequencyGridTest, XToFreqInvertsFreqToX) {
    constexpr float w = 400.0f;
    for (float freq : {20.0f, 100.0f, 440.0f, 1000.0f, 7500.0f, 20000.0f}) {
        const float roundTripped = FrequencyGrid::xToFreq(FrequencyGrid::freqToX(freq, w), w);
        EXPECT_NEAR(roundTripped, freq, freq * 0.001f) << "at " << freq << " Hz";
    }
}

TEST(FrequencyGridTest, XToFreqHandlesZeroWidth) {
    EXPECT_FLOAT_EQ(FrequencyGrid::xToFreq(10.0f, 0.0f), FrequencyGrid::kMinFreq);
    EXPECT_FLOAT_EQ(FrequencyGrid::xToFreq(10.0f, -5.0f), FrequencyGrid::kMinFreq);
}

TEST(FrequencyGridTest, IndexToFreqSpansTheAxisAndIsMonotonic) {
    constexpr int numPoints = 256;
    EXPECT_NEAR(FrequencyGrid::indexToFreq(0, numPoints), FrequencyGrid::kMinFreq, 0.01f);
    EXPECT_NEAR(FrequencyGrid::indexToFreq(numPoints - 1, numPoints), FrequencyGrid::kMaxFreq, 1.0f);

    float previous = 0.0f;
    for (int i = 0; i < numPoints; ++i) {
        const float freq = FrequencyGrid::indexToFreq(i, numPoints);
        EXPECT_GT(freq, previous) << "index " << i;
        previous = freq;
    }
}

TEST(FrequencyGridTest, IndexToFreqHandlesDegenerateCounts) {
    EXPECT_FLOAT_EQ(FrequencyGrid::indexToFreq(0, 1), FrequencyGrid::kMinFreq);
    EXPECT_FLOAT_EQ(FrequencyGrid::indexToFreq(0, 0), FrequencyGrid::kMinFreq);
}

// ---------------------------------------------------------------------------
// FrequencyGrid — dB axis
// ---------------------------------------------------------------------------

TEST(FrequencyGridTest, DbToYPutsMaxAtTopAndMinAtBottom) {
    constexpr float h = 120.0f;
    EXPECT_NEAR(FrequencyGrid::dbToY(30.0f, h, -30.0f, 30.0f), 0.0f, 0.01f);
    EXPECT_NEAR(FrequencyGrid::dbToY(-30.0f, h, -30.0f, 30.0f), h, 0.01f);
    // A symmetric window puts 0 dB exactly halfway down.
    EXPECT_NEAR(FrequencyGrid::dbToY(0.0f, h, -30.0f, 30.0f), h * 0.5f, 0.01f);
}

TEST(FrequencyGridTest, DbToYIsMonotonicDownwards) {
    constexpr float h = 200.0f;
    float previous = -1.0f;
    for (float db = 50.0f; db >= -40.0f; db -= 5.0f) {
        const float y = FrequencyGrid::dbToY(db, h, -40.0f, 50.0f);
        EXPECT_GT(y, previous) << "at " << db << " dB";
        previous = y;
    }
}

TEST(FrequencyGridTest, YToDbInvertsDbToY) {
    constexpr float h = 130.0f;
    for (float db : {-30.0f, -12.0f, 0.0f, 6.0f, 30.0f}) {
        const float roundTripped = FrequencyGrid::yToDb(FrequencyGrid::dbToY(db, h, -30.0f, 30.0f), h, -30.0f, 30.0f);
        EXPECT_NEAR(roundTripped, db, 0.01f) << "at " << db << " dB";
    }
}

TEST(FrequencyGridTest, YToDbHandlesZeroHeight) {
    EXPECT_FLOAT_EQ(FrequencyGrid::yToDb(5.0f, 0.0f, -30.0f, 30.0f), 30.0f);
}

// ---------------------------------------------------------------------------
// FrequencyGrid — labels and peak finding
// ---------------------------------------------------------------------------

TEST(FrequencyGridTest, FormatHzLabel) {
    EXPECT_EQ(FrequencyGrid::formatHzLabel(100.0f), "100Hz");
    EXPECT_EQ(FrequencyGrid::formatHzLabel(999.0f), "999Hz");
    EXPECT_EQ(FrequencyGrid::formatHzLabel(1000.0f), "1kHz");
    EXPECT_EQ(FrequencyGrid::formatHzLabel(1500.0f), "1.5kHz");
    EXPECT_EQ(FrequencyGrid::formatHzLabel(10000.0f), "10kHz");
}

TEST(FrequencyGridTest, FindPeakBin) {
    const float mags[] = {0.1f, 0.4f, 0.9f, 0.3f};
    EXPECT_EQ(FrequencyGrid::findPeakBin(mags, 4), 2);
    // Ties resolve to the first occurrence.
    const float tied[] = {0.5f, 0.5f};
    EXPECT_EQ(FrequencyGrid::findPeakBin(tied, 2), 0);
    EXPECT_EQ(FrequencyGrid::findPeakBin(mags, 0), -1);
    EXPECT_EQ(FrequencyGrid::findPeakBin(nullptr, 4), -1);
}

// ---------------------------------------------------------------------------
// The two visualisers agree with the shared grid
// ---------------------------------------------------------------------------

TEST(FrequencyGridTest, FilterViewStaticsDelegateToTheSharedGrid) {
    constexpr float w = 400.0f;
    constexpr float h = 200.0f;
    for (float freq : {20.0f, 440.0f, 5000.0f, 20000.0f})
        EXPECT_FLOAT_EQ(FrequencyResponseComponent::freqToXStatic(freq, w), FrequencyGrid::freqToX(freq, w));
    for (float db : {-40.0f, 0.0f, 20.0f, 50.0f})
        EXPECT_FLOAT_EQ(
            FrequencyResponseComponent::dbToYStatic(db, h),
            FrequencyGrid::dbToY(db, h, FrequencyResponseComponent::minDb, FrequencyResponseComponent::maxDb));
    EXPECT_EQ(FrequencyResponseComponent::formatHzLabel(1500.0f), FrequencyGrid::formatHzLabel(1500.0f));
}

TEST(EQCurveTest, StaticsUseASymmetricThirtyDbWindow) {
    constexpr float h = 130.0f;
    EXPECT_FLOAT_EQ(EQCurveComponent::minDb, -30.0f);
    EXPECT_FLOAT_EQ(EQCurveComponent::maxDb, 30.0f);
    // 0 dB is the visual centre — that is what makes boosts and cuts read symmetrically.
    EXPECT_NEAR(EQCurveComponent::dbToYStatic(0.0f, h), h * 0.5f, 0.01f);
    EXPECT_NEAR(EQCurveComponent::dbToYStatic(30.0f, h), 0.0f, 0.01f);
    EXPECT_NEAR(EQCurveComponent::dbToYStatic(-30.0f, h), h, 0.01f);
    EXPECT_FLOAT_EQ(EQCurveComponent::freqToXStatic(1000.0f, 400.0f), FrequencyGrid::freqToX(1000.0f, 400.0f));
}

// ---------------------------------------------------------------------------
// EQCurveComponent — paint smoke tests
// ---------------------------------------------------------------------------

TEST(EQCurveTest, PaintSmokeFlat) {
    ParametricEQModule eq;
    EQCurveComponent comp(eq);
    comp.setBounds(0, 0, 260, 130);

    juce::Image img(juce::Image::ARGB, 260, 130, true);
    juce::Graphics g(img);
    EXPECT_NO_THROW(comp.paint(g));
    EXPECT_TRUE(hasOpaquePixel(img)) << "painted image should have at least one opaque pixel";
}

TEST(EQCurveTest, PaintSmokeWithActiveBands) {
    ParametricEQModule eq;
    for (auto* p : eq.getParameters()) {
        auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(p);
        auto* asFloat = dynamic_cast<juce::AudioParameterFloat*>(p);
        if (withId == nullptr || asFloat == nullptr)
            continue;
        if (withId->paramID == "band1Gain")
            asFloat->setValueNotifyingHost(asFloat->convertTo0to1(18.0f));
        else if (withId->paramID == "band2Gain")
            asFloat->setValueNotifyingHost(asFloat->convertTo0to1(-18.0f));
        else if (withId->paramID == "lowGain")
            asFloat->setValueNotifyingHost(asFloat->convertTo0to1(12.0f));
    }
    eq.prepareToPlay(48000.0, 512);

    // Push a block through so the module publishes a non-flat snapshot to the view.
    juce::AudioBuffer<float> buffer(6, 512);
    buffer.clear();
    juce::MidiBuffer midi;
    eq.processBlock(buffer, midi);

    EQCurveComponent comp(eq);
    comp.setBounds(0, 0, 260, 130);
    juce::Image img(juce::Image::ARGB, 260, 130, true);
    juce::Graphics g(img);
    EXPECT_NO_THROW(comp.paint(g));
    EXPECT_TRUE(hasOpaquePixel(img));
}

TEST(EQCurveTest, PaintSmokeWithSpectrumOverlay) {
    ParametricEQModule eq;
    eq.prepareToPlay(48000.0, 512);

    // Feed the module real audio so its VisualBuffer has something for the FFT to chew on.
    juce::AudioBuffer<float> buffer(6, 512);
    juce::MidiBuffer midi;
    for (int b = 0; b < 4; ++b) {
        buffer.clear();
        for (int i = 0; i < 512; ++i) {
            const float s = 0.5f * std::sin(juce::MathConstants<float>::twoPi * 1000.0f *
                                            static_cast<float>(b * 512 + i) / 48000.0f);
            buffer.setSample(0, i, s);
            buffer.setSample(1, i, s);
        }
        eq.processBlock(buffer, midi);
    }

    EQCurveComponent comp(eq);
    comp.setBounds(0, 0, 260, 130);
    comp.setShowSpectrum(true);
    EXPECT_TRUE(comp.getShowSpectrum());
    // timerCallback drives the FFT; run it explicitly since no message loop is pumping here.
    EXPECT_NO_THROW(comp.timerCallback());

    juce::Image img(juce::Image::ARGB, 260, 130, true);
    juce::Graphics g(img);
    EXPECT_NO_THROW(comp.paint(g));
    EXPECT_TRUE(hasOpaquePixel(img));

    comp.setShowSpectrum(false);
    EXPECT_FALSE(comp.getShowSpectrum());
}

TEST(EQCurveTest, PaintAtDegenerateSizesDoesNotCrash) {
    ParametricEQModule eq;
    EQCurveComponent comp(eq);

    // Zero size: paint must bail out rather than divide by a zero width/height.
    comp.setBounds(0, 0, 0, 0);
    juce::Image tiny(juce::Image::ARGB, 1, 1, true);
    juce::Graphics gTiny(tiny);
    EXPECT_NO_THROW(comp.paint(gTiny));

    // Narrower than the 44 px callout box: labels are skipped, nothing overflows.
    comp.setBounds(0, 0, 20, 40);
    juce::Image narrow(juce::Image::ARGB, 20, 40, true);
    juce::Graphics gNarrow(narrow);
    EXPECT_NO_THROW(comp.paint(gNarrow));
}
