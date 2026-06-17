// ScopeTests.cpp
// Headless unit tests for ScopeComponent static helpers and paint smoke tests.

#include "../Source/Modules/VisualBuffer.h"
#include "../Source/UI/ScopeComponent.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

// ---------------------------------------------------------------------------
// isNoSignal — threshold logic
// ---------------------------------------------------------------------------

TEST(ScopeTest, NoSignalThreshold_Zero) { EXPECT_TRUE(ScopeComponent::isNoSignal(0.0f)); }

TEST(ScopeTest, NoSignalThreshold_BoundaryInclusive) {
    // 0.02 is the boundary — inclusive, so must return true
    EXPECT_TRUE(ScopeComponent::isNoSignal(0.02f));
}

TEST(ScopeTest, NoSignalThreshold_JustAbove) {
    // 0.021 is just above the threshold — must return false
    EXPECT_FALSE(ScopeComponent::isNoSignal(0.021f));
}

TEST(ScopeTest, NoSignalThreshold_FullAmplitude) { EXPECT_FALSE(ScopeComponent::isNoSignal(0.5f)); }

// ---------------------------------------------------------------------------
// amplitudeToY — mapping sanity checks
// ---------------------------------------------------------------------------

TEST(ScopeTest, AmplitudeMapping_TopNearBoundsTop) {
    juce::Rectangle<float> bounds(0.0f, 0.0f, 400.0f, 200.0f);
    float y = ScopeComponent::amplitudeToY(1.0f, bounds);
    // amp=+1 should be above centre (lower y value)
    EXPECT_LT(y, bounds.getCentreY());
    // And still within bounds
    EXPECT_GE(y, bounds.getY());
}

TEST(ScopeTest, AmplitudeMapping_BottomNearBoundsBottom) {
    juce::Rectangle<float> bounds(0.0f, 0.0f, 400.0f, 200.0f);
    float y = ScopeComponent::amplitudeToY(-1.0f, bounds);
    // amp=-1 should be below centre (higher y value)
    EXPECT_GT(y, bounds.getCentreY());
    // And still within bounds
    EXPECT_LE(y, bounds.getBottom());
}

TEST(ScopeTest, AmplitudeMapping_ZeroIsVerticalCentre) {
    juce::Rectangle<float> bounds(0.0f, 0.0f, 400.0f, 200.0f);
    float y = ScopeComponent::amplitudeToY(0.0f, bounds);
    EXPECT_FLOAT_EQ(y, bounds.getCentreY());
}

TEST(ScopeTest, AmplitudeMapping_Symmetry) {
    juce::Rectangle<float> bounds(0.0f, 0.0f, 400.0f, 200.0f);
    float yPos = ScopeComponent::amplitudeToY(0.5f, bounds);
    float yNeg = ScopeComponent::amplitudeToY(-0.5f, bounds);
    float centre = bounds.getCentreY();
    // Positive and negative amplitudes should be symmetric around the centre
    EXPECT_NEAR(centre - yPos, yNeg - centre, 0.001f);
}

// ---------------------------------------------------------------------------
// Paint smoke test — no-signal state (silent buffer)
// ---------------------------------------------------------------------------

TEST(ScopeTest, PaintSmokeNoSignal) {
    VisualBuffer buf(256);
    // Leave buffer at zero (all samples are 0.0) — should trigger no-signal path
    ScopeComponent scope(buf);
    scope.setBounds(0, 0, 400, 200);

    juce::Image img(juce::Image::ARGB, 400, 200, true);
    juce::Graphics g(img);

    EXPECT_NO_THROW(scope.paint(g));
    EXPECT_TRUE(img.isValid());
}

// ---------------------------------------------------------------------------
// Paint smoke test — with signal
// ---------------------------------------------------------------------------

TEST(ScopeTest, PaintSmokeWithSignal) {
    VisualBuffer buf(256);
    // Push a sine-like signal well above the no-signal threshold
    for (int i = 0; i < 256; ++i) {
        float sample = 0.8f * std::sin(static_cast<float>(i) * juce::MathConstants<float>::twoPi / 64.0f);
        buf.pushSample(sample);
    }

    ScopeComponent scope(buf);
    scope.setBounds(0, 0, 400, 200);

    // Trigger a timer tick so the component copies from the buffer
    scope.timerCallback();

    juce::Image img(juce::Image::ARGB, 400, 200, true);
    juce::Graphics g(img);

    EXPECT_NO_THROW(scope.paint(g));
    EXPECT_TRUE(img.isValid());
}
