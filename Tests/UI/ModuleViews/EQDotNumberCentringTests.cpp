// EQDotNumberCentringTests.cpp -- the band number inside an EQ dot has its ink centred on the dot's ring, at fractional
// dot positions, for one- and two-digit numbers.
#include "Modules/FX/ParametricEQModule.h"
#include "UI/Layout/CentredLabel.h"
#include "UI/ModuleViews/EQCurveComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/Theme.h"
#include <cmath>
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace {

struct InkBox {
    bool any = false;
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    void add(int x, int y) {
        any = true;
        x0 = std::min(x0, (float)x);
        y0 = std::min(y0, (float)y);
        x1 = std::max(x1, (float)x + 1.0f);
        y1 = std::max(y1, (float)y + 1.0f);
    }
    juce::Point<float> centre() const { return {(x0 + x1) * 0.5f, (y0 + y1) * 0.5f}; }
};

} // namespace

TEST(EQDotNumberCentring, TheHelperCentresTheInkOnAFloatPoint) {
    const juce::Point<float> centres[] = {{20.3f, 20.7f}, {31.6f, 18.2f}, {15.5f, 22.45f}};
    for (const int number : {1, 4, 10, 24}) {
        for (const float size : {8.0f, 9.0f}) {
            for (const auto centre : centres) {
                juce::Image img(juce::Image::ARGB, 48, 40, true, juce::SoftwareImageType());
                {
                    juce::Graphics g(img);
                    g.setColour(juce::Colours::white);
                    synth::ui::drawLabelCentredOn(g, juce::String(number), juce::Font(size), centre);
                }
                InkBox box;
                for (int y = 0; y < img.getHeight(); ++y)
                    for (int x = 0; x < img.getWidth(); ++x)
                        if (img.getPixelAt(x, y).getFloatAlpha() > 0.1f)
                            box.add(x, y);
                ASSERT_TRUE(box.any) << number;
                EXPECT_NEAR(box.centre().x, centre.x, 0.5f) << "number " << number << " size " << size;
                EXPECT_NEAR(box.centre().y, centre.y, 0.5f) << "number " << number << " size " << size;
            }
        }
    }
}

// Regression test for FRO1780: the number in a dot was drawn in an int box (shifted up to a pixel, differently per dot)
// centred by line box, so the digits rode high and left of the ring's middle.
TEST(EQDotNumberCentring, EveryNumberOnTheCurveSitsInTheMiddleOfItsRing) {
    ParametricEQModule eq;
    for (int b = 0; b < 12; ++b) {
        eq.setBandFreq(b, 90.0f * std::pow(1.45f, (float)b));
        eq.setBandGain(b, b % 2 == 0 ? 4.0f : -5.0f);
        eq.setBandEnabled(b, true);
    }
    EQCurveComponent curve(eq);
    curve.setBounds(0, 0, 520, 200);
    curve.timerCallback();

    juce::Image img(juce::Image::ARGB, 520, 200, true, juce::SoftwareImageType());
    {
        juce::Graphics g(img);
        curve.paintEntireComponent(g, false);
    }
    const auto fill = synth::theme::themeOf(curve).colors.bg1;
    int checked = 0;
    for (int b = 0; b < 12; ++b) {
        const auto ring = curve.getDrawnBandPosition(b);
        // Only the inside of the ring (radius 7, 2 px stroke): what differs from the dot's fill there is the number.
        InkBox box;
        for (int y = (int)ring.y - 6; y <= (int)ring.y + 6; ++y)
            for (int x = (int)ring.x - 6; x <= (int)ring.x + 6; ++x) {
                const float dx = (float)x + 0.5f - ring.x, dy = (float)y + 0.5f - ring.y;
                if (dx * dx + dy * dy > 5.0f * 5.0f)
                    continue;
                const auto c = img.getPixelAt(x, y);
                const int diff = std::abs(c.getRed() - fill.getRed()) + std::abs(c.getGreen() - fill.getGreen()) +
                                 std::abs(c.getBlue() - fill.getBlue());
                if (diff > 120)
                    box.add(x, y);
            }
        ASSERT_TRUE(box.any) << "band " << b + 1;
        EXPECT_NEAR(box.centre().x, ring.x, 0.75f) << "band " << b + 1;
        EXPECT_NEAR(box.centre().y, ring.y, 0.75f) << "band " << b + 1;
        ++checked;
    }
    EXPECT_EQ(checked, 12);
}
