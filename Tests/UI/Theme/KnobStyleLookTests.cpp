// KnobStyleLookTests.cpp -- what each control style looks like, rendered into software images (the
// platform default reads back zeros on the Windows CI): the five styles are clearly different at card
// size, Analog prints its scale numbers only above card size, Ring and Neon light up with the value, a
// bipolar knob draws from 12 o'clock, and a rotary slider whose range is symmetric round zero is drawn
// bipolar.

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/AppLookAndFeel/KnobPainter.h"
#include "UI/Theme/BuiltInThemes.h"
#include "UI/Theme/KnobStyle.h"
#include <cmath>
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace {

using namespace synth::theme;

constexpr int kCardSize = 54;

juce::Image render(const Theme& theme, KnobStyle style, int size, float pos, float origin = 0.0f,
                   juce::Colour colour = {}) {
    juce::Image img(juce::Image::ARGB, size, size, true, juce::SoftwareImageType());
    juce::Graphics g(img);
    paintKnob(g, theme, style, juce::Rectangle<float>(0, 0, (float)size, (float)size), pos,
              colour.isTransparent() ? theme.colors.accent : colour, origin);
    return img;
}

// Composited over the card surface, so a transparent pixel and a surface-coloured one compare equal.
juce::Colour onSurface(const Theme& theme, juce::Colour pixel) { return theme.colors.surface.overlaidWith(pixel); }

// Mean absolute RGB difference per pixel, 0..765.
double meanDifference(const Theme& theme, const juce::Image& a, const juce::Image& b) {
    double total = 0.0;
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x) {
            const auto p = onSurface(theme, a.getPixelAt(x, y));
            const auto q = onSurface(theme, b.getPixelAt(x, y));
            total += std::abs(p.getRed() - q.getRed()) + std::abs(p.getGreen() - q.getGreen()) +
                     std::abs(p.getBlue() - q.getBlue());
        }
    return total / (double)(a.getWidth() * a.getHeight());
}

double meanDifference(const juce::Image& a, const juce::Image& b) { return meanDifference(makeObsidian(), a, b); }

// How strongly the accent colour shows across the image: summed alpha of accent-ish pixels.
double accentEnergy(const juce::Image& img, juce::Colour accent) {
    double total = 0.0;
    for (int y = 0; y < img.getHeight(); ++y)
        for (int x = 0; x < img.getWidth(); ++x) {
            const auto p = img.getPixelAt(x, y);
            if (p.getAlpha() == 0)
                continue;
            const auto c = p.withAlpha(1.0f);
            const int d = std::abs(c.getRed() - accent.getRed()) + std::abs(c.getGreen() - accent.getGreen()) +
                          std::abs(c.getBlue() - accent.getBlue());
            if (d < 90)
                total += p.getFloatAlpha();
        }
    return total;
}

} // namespace

TEST(KnobStyleLookTest, EveryStyleIsClearlyDifferentAtCardSize) {
    const auto theme = makeObsidian();
    for (const float pos : {0.2f, 0.65f}) {
        juce::Image images[kKnobStyleCount];
        for (int i = 0; i < kKnobStyleCount; ++i)
            images[i] = render(theme, (KnobStyle)i, kCardSize, pos);
        for (int a = 0; a < kKnobStyleCount; ++a)
            for (int b = a + 1; b < kKnobStyleCount; ++b)
                EXPECT_GT(meanDifference(images[a], images[b]), 25.0)
                    << knobStyleId((KnobStyle)a) << " and " << knobStyleId((KnobStyle)b) << " look alike at " << pos;
    }
}

TEST(KnobStyleLookTest, AnalogPrintsItsNumbersOnlyAboveCardSize) {
    const auto theme = makeObsidian();
    // The numbers sit outside the ticks, in the corners and over the top: the band between the dial and
    // the bounds is empty at card size and inked when large.
    auto inkOutsideDial = [](const juce::Image& img, float dialFraction) {
        const float c = (float)img.getWidth() * 0.5f;
        int count = 0;
        for (int y = 0; y < img.getHeight(); ++y)
            for (int x = 0; x < img.getWidth(); ++x)
                if (std::hypot((float)x + 0.5f - c, (float)y + 0.5f - c) > c * dialFraction &&
                    img.getPixelAt(x, y).getAlpha() > 128)
                    ++count;
        return count;
    };
    EXPECT_EQ(inkOutsideDial(render(theme, KnobStyle::Analog, kCardSize, 0.5f), 1.0f), 0);
    const auto large = render(theme, KnobStyle::Analog, 112, 0.5f);
    EXPECT_GT(inkOutsideDial(large, 0.9f), 10) << "0, 5 and 10 are printed round a large knob";

    // At the threshold itself the numbers are still hidden; one pixel above, they show.
    EXPECT_LT(kAnalogNumbersMinSize, 56.0f);
    EXPECT_GT(kAnalogNumbersMinSize, (float)kCardSize);
}

TEST(KnobStyleLookTest, AnalogIgnoresTheValueColour) {
    const auto theme = makeObsidian();
    const auto amber = render(theme, KnobStyle::Analog, kCardSize, 0.4f, 0.0f, theme.colors.hueAmber);
    const auto rose = render(theme, KnobStyle::Analog, kCardSize, 0.4f, 0.0f, theme.colors.hueRose);
    EXPECT_EQ(meanDifference(amber, rose), 0.0);
}

TEST(KnobStyleLookTest, RingLightsMoreLedsAsTheValueRises) {
    const auto theme = makeObsidian();
    const auto accent = theme.colors.accent;
    const double low = accentEnergy(render(theme, KnobStyle::Ring, kCardSize, 0.2f), accent);
    const double mid = accentEnergy(render(theme, KnobStyle::Ring, kCardSize, 0.5f), accent);
    const double high = accentEnergy(render(theme, KnobStyle::Ring, kCardSize, 0.9f), accent);
    EXPECT_GT(low, 0.0);
    EXPECT_GT(mid, low * 1.5);
    EXPECT_GT(high, mid * 1.4);
}

TEST(KnobStyleLookTest, NeonGetsBrighterAsTheValueRises) {
    const auto theme = makeObsidian();
    const auto accent = theme.colors.accent;
    const double low = accentEnergy(render(theme, KnobStyle::Neon, kCardSize, 0.2f), accent);
    const double high = accentEnergy(render(theme, KnobStyle::Neon, kCardSize, 0.9f), accent);
    EXPECT_GT(high, low * 2.0);
}

TEST(KnobStyleLookTest, BipolarKnobAtTheCentreDrawsNoValueArc) {
    const auto theme = makeObsidian();
    for (const auto style : {KnobStyle::Classic, KnobStyle::Hardware, KnobStyle::Neon, KnobStyle::Ring}) {
        const auto centred = render(theme, style, kCardSize, 0.5f, 0.5f);
        const auto accent = theme.colors.accent;
        // Only the tip / glow at 12 o'clock may carry the value colour; the arc's sides stay unlit.
        const auto& img = centred;
        const double sides = [&] {
            double total = 0.0;
            const float c = (float)kCardSize * 0.5f;
            for (int y = 0; y < kCardSize; ++y)
                for (int x = 0; x < kCardSize; ++x) {
                    const float dx = (float)x + 0.5f - c;
                    const float dy = (float)y + 0.5f - c;
                    if (std::hypot(dx, dy) < c * 0.8f || std::abs(dx) < c * 0.45f)
                        continue;
                    const auto p = img.getPixelAt(x, y);
                    const auto q = p.withAlpha(1.0f);
                    if (p.getAlpha() > 60 && std::abs(q.getRed() - accent.getRed()) +
                                                     std::abs(q.getGreen() - accent.getGreen()) +
                                                     std::abs(q.getBlue() - accent.getBlue()) <
                                                 90)
                        total += 1.0;
                }
            return total;
        }();
        EXPECT_EQ(sides, 0.0) << knobStyleId(style);
    }
}

TEST(KnobStyleLookTest, BipolarKnobDrawsOnlyOnItsSideOfTwelveOClock) {
    const auto theme = makeObsidian();
    const auto accent = theme.colors.accent;
    // The value colour on each side, outside the body.
    auto sideEnergy = [&](const juce::Image& img, bool left) {
        const float c = (float)img.getWidth() * 0.5f;
        double total = 0.0;
        for (int y = 0; y < img.getHeight(); ++y)
            for (int x = 0; x < img.getWidth(); ++x) {
                const float dx = (float)x + 0.5f - c;
                if (std::abs(dx) < 4.0f || (left ? dx > 0.0f : dx < 0.0f) ||
                    std::hypot(dx, (float)y + 0.5f - c) < c * 0.78f)
                    continue;
                const auto p = img.getPixelAt(x, y);
                const auto q = p.withAlpha(1.0f);
                if (p.getAlpha() > 60 && std::abs(q.getRed() - accent.getRed()) +
                                                 std::abs(q.getGreen() - accent.getGreen()) +
                                                 std::abs(q.getBlue() - accent.getBlue()) <
                                             90)
                    total += 1.0;
            }
        return total;
    };
    for (const auto style : {KnobStyle::Classic, KnobStyle::Hardware, KnobStyle::Neon, KnobStyle::Ring}) {
        const auto right = render(theme, style, kCardSize, 0.75f, 0.5f);
        const auto left = render(theme, style, kCardSize, 0.25f, 0.5f);
        EXPECT_GT(sideEnergy(right, false), 10.0) << knobStyleId(style) << " +0.25 lights the right";
        EXPECT_EQ(sideEnergy(right, true), 0.0) << knobStyleId(style) << " +0.25 leaves the left dark";
        EXPECT_GT(sideEnergy(left, true), 10.0) << knobStyleId(style) << " -0.25 lights the left";
        EXPECT_EQ(sideEnergy(left, false), 0.0) << knobStyleId(style) << " -0.25 leaves the right dark";
    }
}

TEST(KnobStyleLookTest, UnipolarKnobStillDrawsFromTheStart) {
    const auto theme = makeObsidian();
    // At 0.25 an ordinary knob's arc runs up the left side from the start; a bipolar one's does not.
    const auto unipolar = render(theme, KnobStyle::Classic, kCardSize, 0.25f, 0.0f);
    const auto bipolar = render(theme, KnobStyle::Classic, kCardSize, 0.25f, 0.5f);
    EXPECT_GT(meanDifference(unipolar, bipolar), 1.0);
}

namespace {

// A rotary slider painted through a real AppLookAndFeel.
juce::Image paintSlider(KnobStyle style, double min, double max, double value) {
    AppLookAndFeel laf;
    laf.applyTheme(makeObsidian());
    laf.setKnobAppearance({style, false});
    juce::Slider slider(juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox);
    slider.setLookAndFeel(&laf);
    slider.setRange(min, max, 0.0);
    slider.setValue(value, juce::dontSendNotification);
    slider.setBounds(0, 0, kCardSize, kCardSize);
    juce::Image img(juce::Image::ARGB, kCardSize, kCardSize, true, juce::SoftwareImageType());
    juce::Graphics g(img);
    slider.paintEntireComponent(g, false);
    slider.setLookAndFeel(nullptr);
    return img;
}

} // namespace

TEST(KnobStyleLookTest, SymmetricRangeSliderIsDrawnBipolar) {
    const auto theme = makeObsidian();
    // Pan at centre (-1..1, value 0) matches a bipolar knob at 0.5, not an ordinary one at 0.5.
    const auto pan = paintSlider(KnobStyle::Classic, -1.0, 1.0, 0.0);
    const auto bipolar = render(theme, KnobStyle::Classic, kCardSize, 0.5f, 0.5f);
    const auto unipolar = render(theme, KnobStyle::Classic, kCardSize, 0.5f, 0.0f);
    EXPECT_LT(meanDifference(pan, bipolar), meanDifference(pan, unipolar));
    EXPECT_LT(meanDifference(pan, bipolar), 1.0);

    // An octave (-4..4) is bipolar; a detune amount (0..100) and a lopsided range (-12..24) are not.
    EXPECT_LT(meanDifference(paintSlider(KnobStyle::Classic, -4.0, 4.0, 0.0), bipolar), 1.0);
    EXPECT_LT(meanDifference(paintSlider(KnobStyle::Classic, 0.0, 100.0, 50.0), unipolar), 1.0);
    EXPECT_LT(meanDifference(paintSlider(KnobStyle::Classic, -12.0, 24.0, 6.0), unipolar), 1.0);
}
