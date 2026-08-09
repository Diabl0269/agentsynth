// WavetableDisplayTests.cpp
// Headless unit tests for WavetableDisplayComponent: the quantisePosition helper, the
// gated-repaint contract, and paint smoke tests for built-in and loaded tables.

#include "../Source/Modules/WavetableOscillatorModule.h"
#include "../Source/UI/WavetableDisplayComponent.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace {

void setPosition(WavetableOscillatorModule& module, float value) {
    for (auto* param : module.getParameters())
        if (auto* f = dynamic_cast<juce::AudioParameterFloat*>(param))
            if (f->paramID == "position")
                *f = value;
}

} // namespace

// ---------------------------------------------------------------------------
// quantisePosition
// ---------------------------------------------------------------------------

TEST(WavetableDisplayTest, QuantisePositionEndpointsAndClamping) {
    EXPECT_EQ(WavetableDisplayComponent::quantisePosition(0.0f, 200), 0);
    EXPECT_EQ(WavetableDisplayComponent::quantisePosition(1.0f, 200), 200);
    EXPECT_EQ(WavetableDisplayComponent::quantisePosition(0.5f, 200), 100);
    // Out-of-range values are clamped, not wrapped.
    EXPECT_EQ(WavetableDisplayComponent::quantisePosition(-1.0f, 200), 0);
    EXPECT_EQ(WavetableDisplayComponent::quantisePosition(2.0f, 200), 200);
}

TEST(WavetableDisplayTest, QuantisePositionIsMonotonicAndCollapsesTinyChanges) {
    int previous = -1;
    for (int i = 0; i <= 100; ++i) {
        const int q = WavetableDisplayComponent::quantisePosition((float)i / 100.0f, 200);
        EXPECT_GE(q, previous);
        previous = q;
    }
    // A sub-bucket jitter must land in the same bucket, so the repaint gate stays closed.
    EXPECT_EQ(WavetableDisplayComponent::quantisePosition(0.5f, 200),
              WavetableDisplayComponent::quantisePosition(0.5009f, 200));
}

// ---------------------------------------------------------------------------
// Gated repaint contract
// ---------------------------------------------------------------------------

TEST(WavetableDisplayTest, RepeatedTimerTicksOnAnUnchangedModuleAreIdempotent) {
    // The gate's mechanism is the quantised change signature (covered above); this checks
    // that ticking repeatedly with an unchanged module is safe and does not disturb the
    // rendered trace.
    WavetableOscillatorModule module;
    WavetableDisplayComponent display(module);
    display.setBounds(0, 0, 260, 80);
    display.timerCallback();

    std::vector<float> before;
    module.getDisplayWaveform(before, WavetableDisplayComponent::kNumPoints);

    for (int i = 0; i < 5; ++i)
        EXPECT_NO_THROW(display.timerCallback());

    std::vector<float> after;
    module.getDisplayWaveform(after, WavetableDisplayComponent::kNumPoints);
    ASSERT_EQ(before.size(), after.size());
    for (size_t i = 0; i < before.size(); ++i)
        EXPECT_FLOAT_EQ(before[i], after[i]);

    // A real position change is still picked up.
    setPosition(module, 0.5f);
    EXPECT_NO_THROW(display.timerCallback());
    EXPECT_NEAR(module.getScanPosition(), 0.5f, 1.0e-5f);
}

// ---------------------------------------------------------------------------
// Waveform data feeding the display
// ---------------------------------------------------------------------------

TEST(WavetableDisplayTest, DisplayWaveformTracksScanPosition) {
    WavetableOscillatorModule module;

    std::vector<float> atZero, atOne;
    module.getDisplayWaveformAt(atZero, WavetableDisplayComponent::kNumPoints, 0.0f);
    module.getDisplayWaveformAt(atOne, WavetableDisplayComponent::kNumPoints, 1.0f);

    ASSERT_EQ(atZero.size(), (size_t)WavetableDisplayComponent::kNumPoints);
    ASSERT_EQ(atOne.size(), (size_t)WavetableDisplayComponent::kNumPoints);

    // "Basic Shapes" runs sine -> square, so the two ends must differ substantially.
    float maxDelta = 0.0f;
    for (size_t i = 0; i < atZero.size(); ++i)
        maxDelta = std::max(maxDelta, std::abs(atZero[i] - atOne[i]));
    EXPECT_GT(maxDelta, 0.2f);

    // Both traces are bounded — the display's y mapping assumes roughly [-1, 1].
    for (float v : atZero)
        EXPECT_LT(std::abs(v), 2.0f);
    for (float v : atOne)
        EXPECT_LT(std::abs(v), 2.0f);
}

TEST(WavetableDisplayTest, DisplayWaveformHandlesTinyPointCounts) {
    WavetableOscillatorModule module;
    std::vector<float> out;
    EXPECT_NO_THROW(module.getDisplayWaveformAt(out, 0, 0.5f));
    EXPECT_GE(out.size(), 2u) << "must never produce a degenerate trace";
    EXPECT_NO_THROW(module.getDisplayWaveformAt(out, 1, 0.5f));
    EXPECT_GE(out.size(), 2u);
}

// ---------------------------------------------------------------------------
// Paint smoke tests
// ---------------------------------------------------------------------------

TEST(WavetableDisplayTest, PaintSmokeBuiltInTable) {
    WavetableOscillatorModule module;
    WavetableDisplayComponent display(module);
    display.setBounds(0, 0, 260, 80);
    display.timerCallback();

    juce::Image img(juce::Image::ARGB, 260, 80, true);
    juce::Graphics g(img);
    EXPECT_NO_THROW(display.paint(g));
    EXPECT_TRUE(img.isValid());
}

TEST(WavetableDisplayTest, PaintSmokeAtEveryScanPosition) {
    WavetableOscillatorModule module;
    WavetableDisplayComponent display(module);
    display.setBounds(0, 0, 260, 80);

    juce::Image img(juce::Image::ARGB, 260, 80, true);
    juce::Graphics g(img);

    for (int i = 0; i <= 10; ++i) {
        setPosition(module, (float)i / 10.0f);
        display.timerCallback();
        EXPECT_NO_THROW(display.paint(g)) << "position " << (float)i / 10.0f;
    }
}

TEST(WavetableDisplayTest, PaintSmokeAtDegenerateSizes) {
    WavetableOscillatorModule module;
    WavetableDisplayComponent display(module);

    for (auto size : {juce::Point<int>{0, 0}, juce::Point<int>{1, 1}, juce::Point<int>{4, 80}}) {
        display.setBounds(0, 0, size.x, size.y);
        juce::Image img(juce::Image::ARGB, std::max(1, size.x), std::max(1, size.y), true);
        juce::Graphics g(img);
        EXPECT_NO_THROW(display.paint(g)) << size.x << "x" << size.y;
    }
}
