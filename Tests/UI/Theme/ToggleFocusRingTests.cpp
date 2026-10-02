// ToggleFocusRingTests.cpp -- the keyboard-focus ring of a check box stands off the box by a gap, so
// it stays visible when the box is ticked (a ticked box is filled with the same accent colour).
//
// A headless component cannot hold real keyboard focus, so the tests paint through
// AppLookAndFeel::paintToggleButton with the focus state given. Images use SoftwareImageType(): the
// default native image type reads back zeros on a Windows runner.

#include "UI/Layout/FocusRing.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace {

constexpr float kRingGap = 1.0f; // mirrors AppLookAndFeel::paintToggleButton: the bg0 separator width

struct Painted {
    juce::Image image;
    juce::Rectangle<int> box;
    float ringExtent = 0.0f;
};

Painted paintToggle(synth::theme::AppLookAndFeel& lf, int height, bool ticked, bool focused) {
    juce::ToggleButton toggle("Option");
    toggle.setLookAndFeel(&lf);
    toggle.setSize(240, height);
    toggle.setToggleState(ticked, juce::dontSendNotification);

    Painted out;
    out.image = juce::Image(juce::Image::ARGB, 240, height, true, juce::SoftwareImageType());
    {
        juce::Graphics g(out.image);
        g.fillAll(juce::Colours::magenta); // a colour no theme token uses
        lf.paintToggleButton(g, toggle, false, focused);
    }
    out.ringExtent = kRingGap + lf.getTheme().metrics.borderWidth * 1.5f;
    const float boxSize = juce::jmin(18.0f, (float)height - 2.0f * out.ringExtent);
    out.box = juce::Rectangle<float>(4.0f, ((float)height - boxSize) * 0.5f, boxSize, boxSize).toNearestInt();
    toggle.setLookAndFeel(nullptr);
    return out;
}

// Pixel centre-left of the box: in the gap (just outside the box edge) and on the ring (beyond it).
juce::Colour gapPixel(const Painted& p) { return p.image.getPixelAt(p.box.getX() - 1, p.box.getCentreY()); }
juce::Colour ringPixel(const Painted& p) { return p.image.getPixelAt(p.box.getX() - 2, p.box.getCentreY()); }

} // namespace

TEST(ToggleFocusRingTest, RingIsSeparatedFromATickedBoxByAGap) {
    synth::theme::AppLookAndFeel lf;
    const auto accent = lf.getTheme().colors.accent;
    const auto painted = paintToggle(lf, 26, /*ticked=*/true, /*focused=*/true);

    EXPECT_EQ(ringPixel(painted), accent) << "the ring is the accent colour";
    EXPECT_NE(gapPixel(painted), accent) << "the pixel between the ring and a ticked (accent-filled) box is not accent";
    EXPECT_EQ(gapPixel(painted), lf.getTheme().colors.bg0) << "the gap is the separator colour";
}

TEST(ToggleFocusRingTest, RingIsSeparatedFromAnUntickedBoxToo) {
    synth::theme::AppLookAndFeel lf;
    const auto painted = paintToggle(lf, 26, /*ticked=*/false, /*focused=*/true);

    EXPECT_EQ(ringPixel(painted), lf.getTheme().colors.accent);
    EXPECT_EQ(gapPixel(painted), lf.getTheme().colors.bg0);
}

TEST(ToggleFocusRingTest, NoRingWithoutKeyboardFocus) {
    synth::theme::AppLookAndFeel lf;
    const auto painted = paintToggle(lf, 26, /*ticked=*/true, /*focused=*/false);

    EXPECT_EQ(ringPixel(painted), juce::Colours::magenta);
    EXPECT_EQ(painted.image.getPixelAt(painted.box.getX() - 3, painted.box.getCentreY()), juce::Colours::magenta);
}

// The ring is part of the component, not clipped by its edge, even at the shortest row used: the box
// gives up size rather than the ring giving up its gap.
TEST(ToggleFocusRingTest, RingStaysInsideAShortRow) {
    synth::theme::AppLookAndFeel lf;
    const auto accent = lf.getTheme().colors.accent;
    const auto painted = paintToggle(lf, 20, /*ticked=*/true, /*focused=*/true);

    const int centreX = painted.box.getCentreX();
    EXPECT_EQ(painted.image.getPixelAt(centreX, 0), accent) << "top of the ring is on the top row, not cut off";
    EXPECT_EQ(painted.image.getPixelAt(centreX, painted.image.getHeight() - 1), accent)
        << "bottom of the ring is on the bottom row, not cut off";
}

TEST(ToggleFocusRingTest, LightThemeRingIsThickerThanTheDarkOne) {
    synth::theme::Theme dark;
    dark.isDark = true;
    synth::theme::Theme light;
    light.isDark = false;
    EXPECT_FLOAT_EQ(synth::ui::focusRingThickness(dark), dark.metrics.borderWidth * 1.5f);
    EXPECT_GT(synth::ui::focusRingThickness(light), synth::ui::focusRingThickness(dark));
}
