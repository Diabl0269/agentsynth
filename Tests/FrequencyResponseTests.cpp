// FrequencyResponseTests.cpp
// Headless unit tests for FrequencyResponseComponent static helpers.
// Paint smoke test constructs the component with a real FilterModule and
// renders into an off-screen image (no audio device or GUI window required).

#include "../Source/Modules/FilterModule.h"
#include "../Source/UI/FrequencyResponseComponent.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

// ---------------------------------------------------------------------------
// findPeakBin — finds index of maximum magnitude
// ---------------------------------------------------------------------------

TEST(FrequencyResponseTest, PeakBinFindsMaximum) {
    float mags[] = {1.0f, 5.0f, 3.0f, 9.0f, 2.0f};
    int peak = FrequencyResponseComponent::findPeakBin(mags, 5);
    EXPECT_EQ(peak, 3); // index 3 holds 9.0f (the maximum)
}

TEST(FrequencyResponseTest, PeakBinFindsFirstMaxWhenTied) {
    // Ties: first occurrence of the maximum wins (loop uses strict >).
    float mags[] = {4.0f, 7.0f, 7.0f, 2.0f};
    int peak = FrequencyResponseComponent::findPeakBin(mags, 4);
    EXPECT_EQ(peak, 1); // index 1 is the first occurrence of 7.0f
}

TEST(FrequencyResponseTest, PeakBinHandlesEmpty) {
    // Contract: numBins <= 0 -> return -1.
    EXPECT_EQ(FrequencyResponseComponent::findPeakBin(nullptr, 0), -1);
    EXPECT_EQ(FrequencyResponseComponent::findPeakBin(nullptr, -5), -1);

    float dummy[] = {1.0f};
    EXPECT_EQ(FrequencyResponseComponent::findPeakBin(dummy, 0), -1);
}

TEST(FrequencyResponseTest, PeakBinSingleElement) {
    float mags[] = {42.0f};
    EXPECT_EQ(FrequencyResponseComponent::findPeakBin(mags, 1), 0);
}

// ---------------------------------------------------------------------------
// formatHzLabel — canonical labels and general rule
// ---------------------------------------------------------------------------

TEST(FrequencyResponseTest, FormatHzLabel_100Hz) {
    EXPECT_EQ(FrequencyResponseComponent::formatHzLabel(100.0f), juce::String("100Hz"));
}

TEST(FrequencyResponseTest, FormatHzLabel_1kHz) {
    EXPECT_EQ(FrequencyResponseComponent::formatHzLabel(1000.0f), juce::String("1kHz"));
}

TEST(FrequencyResponseTest, FormatHzLabel_10kHz) {
    EXPECT_EQ(FrequencyResponseComponent::formatHzLabel(10000.0f), juce::String("10kHz"));
}

TEST(FrequencyResponseTest, FormatHzLabel_SubKiloHz) {
    // Values below 1000 Hz: integer, "Hz" suffix.
    EXPECT_EQ(FrequencyResponseComponent::formatHzLabel(440.0f), juce::String("440Hz"));
    EXPECT_EQ(FrequencyResponseComponent::formatHzLabel(20.0f), juce::String("20Hz"));
}

TEST(FrequencyResponseTest, FormatHzLabel_FractionalKiloHz) {
    // Non-integer kHz values get 1 decimal place.
    juce::String label = FrequencyResponseComponent::formatHzLabel(1500.0f);
    EXPECT_TRUE(label.contains("kHz")) << "label should have kHz suffix for 1500 Hz";
    EXPECT_TRUE(label.contains("1.5")) << "label should show 1.5 for 1500 Hz";
}

// ---------------------------------------------------------------------------
// freqToXStatic — monotonicity on log scale
// ---------------------------------------------------------------------------

TEST(FrequencyResponseTest, FreqMappingMonotonic) {
    constexpr float width = 600.0f;
    float x100 = FrequencyResponseComponent::freqToXStatic(100.0f, width);
    float x1k = FrequencyResponseComponent::freqToXStatic(1000.0f, width);
    float x10k = FrequencyResponseComponent::freqToXStatic(10000.0f, width);

    EXPECT_LT(x100, x1k) << "100 Hz should map to a smaller x than 1 kHz";
    EXPECT_LT(x1k, x10k) << "1 kHz should map to a smaller x than 10 kHz";

    // All values must be within [0, width]
    EXPECT_GE(x100, 0.0f);
    EXPECT_LE(x10k, width);
}

TEST(FrequencyResponseTest, FreqMappingEndpoints) {
    // 20 Hz (minFreq) -> x = 0; 20000 Hz (maxFreq) -> x = width.
    constexpr float width = 500.0f;
    EXPECT_NEAR(FrequencyResponseComponent::freqToXStatic(20.0f, width), 0.0f, 1e-3f);
    EXPECT_NEAR(FrequencyResponseComponent::freqToXStatic(20000.0f, width), width, 1e-3f);
}

// ---------------------------------------------------------------------------
// dbToYStatic — monotonicity (higher dB -> smaller y, i.e., top of screen)
// ---------------------------------------------------------------------------

TEST(FrequencyResponseTest, DbMappingMonotonic) {
    constexpr float height = 400.0f;
    float yPlus20 = FrequencyResponseComponent::dbToYStatic(20.0f, height);
    float yZero = FrequencyResponseComponent::dbToYStatic(0.0f, height);
    float yMinus20 = FrequencyResponseComponent::dbToYStatic(-20.0f, height);

    EXPECT_LT(yPlus20, yZero) << "+20 dB should be higher on screen (smaller y) than 0 dB";
    EXPECT_LT(yZero, yMinus20) << "0 dB should be higher on screen (smaller y) than -20 dB";
}

// ---------------------------------------------------------------------------
// PaintSmoke — construct the component, set bounds, paint into an image
// ---------------------------------------------------------------------------

TEST(FrequencyResponseTest, PaintSmoke) {
    FilterModule filter;
    FrequencyResponseComponent comp(filter);
    comp.setBounds(0, 0, 400, 200);

    juce::Image img(juce::Image::ARGB, 400, 200, true);
    juce::Graphics g(img);

    // Must not crash.
    EXPECT_NO_THROW(comp.paint(g));

    // At least one non-transparent pixel (background fill ensures this).
    bool hasOpaque = false;
    for (int y = 0; y < img.getHeight() && !hasOpaque; ++y)
        for (int x = 0; x < img.getWidth() && !hasOpaque; ++x)
            if (img.getPixelAt(x, y).getAlpha() > 0)
                hasOpaque = true;
    EXPECT_TRUE(hasOpaque) << "painted image should have at least one opaque pixel";
}

// ---------------------------------------------------------------------------
// plotDb — peaks capped, deep roll-off left unclamped (no floor stroke)
// ---------------------------------------------------------------------------

TEST(FrequencyResponseTest, PlotDbCapsPeaksButLeavesDeepCuts) {
    EXPECT_FLOAT_EQ(FrequencyResponseComponent::plotDb(60.0f), FrequencyResponseComponent::maxDb);
    EXPECT_LT(FrequencyResponseComponent::plotDb(-80.0f), FrequencyResponseComponent::minDb);
    EXPECT_FLOAT_EQ(FrequencyResponseComponent::plotDb(0.0f), 0.0f);
}

TEST(FrequencyResponseTest, DbBelowWindowMapsPastBottom) {
    // Contract paired with the unclamped path stroke: values below minDb must map to y > height
    // so the curve exits the view instead of pinning to the bottom edge.
    constexpr float height = 200.0f;
    EXPECT_GT(FrequencyResponseComponent::dbToYStatic(-80.0f, height), height);
}

TEST(FrequencyResponseTest, TimerRunsOnlyWhileVisible) {
    FilterModule filter;
    FrequencyResponseComponent comp(filter);
    // Components default to visible, but the timer is gated on visibilityChanged — force a
    // hidden→shown transition so the start path is exercised the same way the Filter card does.
    comp.setVisible(false);
    EXPECT_FALSE(comp.isTimerRunning()) << "hidden-by-default contract: no timer while hidden";

    comp.setVisible(true);
    EXPECT_TRUE(comp.isTimerRunning());

    comp.setVisible(false);
    EXPECT_FALSE(comp.isTimerRunning());
}

TEST(FrequencyResponseTest, LowpassRollOffDoesNotStrokeBottomEdge) {
    // Regression for the resonant-LPF "floor line": clamping the path to y=h left an opaque
    // accent stroke along the bottom-right after the roll-off. After the fix the path exits the
    // clip region, so the bottom row in the right quarter must not be a bright accent stroke.
    FilterModule filter;
    FrequencyResponseComponent comp(filter);
    constexpr int W = 400;
    constexpr int H = 200;
    comp.setBounds(0, 0, W, H);
    comp.timerCallback(); // recompute magnitudes from defaults (LPF24 @ 440 Hz)

    juce::Image img(juce::Image::ARGB, W, H, true);
    juce::Graphics g(img);
    comp.paint(g);

    const juce::Colour accent(0xff00b4d8);
    int brightBottom = 0;
    for (int x = W * 3 / 4; x < W; ++x) {
        const auto p = img.getPixelAt(x, H - 1);
        const int dr = std::abs((int)p.getRed() - (int)accent.getRed());
        const int dg = std::abs((int)p.getGreen() - (int)accent.getGreen());
        const int db = std::abs((int)p.getBlue() - (int)accent.getBlue());
        if (p.getAlpha() > 200 && (dr + dg + db) < 40)
            ++brightBottom;
    }
    EXPECT_LT(brightBottom, 8) << "bottom-right must not carry an opaque accent floor stroke";
}
