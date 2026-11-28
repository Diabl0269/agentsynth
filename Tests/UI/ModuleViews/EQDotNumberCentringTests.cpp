// EQDotNumberCentringTests.cpp -- the helper that draws a number inside a dot balances its ink (the centroid of the
// painted area) on a float point, for every number 1-24. The numbers as drawn on the real curve, in every theme, are
// measured by EQDotSweepTests.cpp.
#include "Modules/FX/ParametricEQModule.h"
#include "UI/Layout/CentredLabel.h"
#include "UI/ModuleViews/EQCurveComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/Theme.h"
#include <cmath>
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace {

struct InkMass {
    double mass = 0.0, mx = 0.0, my = 0.0;
    void add(int x, int y, double weight) {
        mass += weight;
        mx += weight * (x + 0.5);
        my += weight * (y + 0.5);
    }
    bool any() const { return mass > 0.0; }
    juce::Point<float> centroid() const { return {(float)(mx / mass), (float)(my / mass)}; }
};

} // namespace

TEST(EQDotNumberCentring, TheHelperBalancesTheInkOnAFloatPoint) {
    const juce::Point<float> centres[] = {{20.3f, 20.7f}, {31.6f, 18.2f}, {15.5f, 22.45f}};
    for (int number = 1; number <= 24; ++number) {
        for (const float size : {9.0f, 10.0f}) {
            for (const auto centre : centres) {
                juce::Image img(juce::Image::ARGB, 48, 40, true, juce::SoftwareImageType());
                {
                    juce::Graphics g(img);
                    g.setColour(juce::Colours::white);
                    synth::ui::drawLabelCentredOn(g, juce::String(number), juce::Font(size), centre);
                }
                InkMass ink;
                for (int y = 0; y < img.getHeight(); ++y)
                    for (int x = 0; x < img.getWidth(); ++x)
                        ink.add(x, y, img.getPixelAt(x, y).getFloatAlpha());
                ASSERT_TRUE(ink.any()) << number;
                EXPECT_NEAR(ink.centroid().x, centre.x, 0.3f) << "number " << number << " size " << size;
                EXPECT_NEAR(ink.centroid().y, centre.y, 0.3f) << "number " << number << " size " << size;
            }
        }
    }
}

// Regression test for FRO1921: the centroid cache once ignored the typeface, so a "1" measured in one font put the
// same string in another font off-centre, depending on which test ran first.
TEST(EQDotNumberCentring, TheCentroidCacheKeepsEachTypefaceApart) {
    const juce::Point<float> centre{20.3f, 20.7f};
    for (const char* name : {"Courier New", "Times New Roman", "Arial"}) {
        juce::Image img(juce::Image::ARGB, 48, 40, true, juce::SoftwareImageType());
        {
            juce::Graphics g(img);
            g.setColour(juce::Colours::white);
            const juce::Font font(juce::FontOptions(name, 10.0f, juce::Font::plain));
            synth::ui::drawLabelCentredOn(g, "1", font, centre);
        }
        InkMass ink;
        for (int y = 0; y < img.getHeight(); ++y)
            for (int x = 0; x < img.getWidth(); ++x)
                ink.add(x, y, img.getPixelAt(x, y).getFloatAlpha());
        ASSERT_TRUE(ink.any()) << name;
        EXPECT_NEAR(ink.centroid().x, centre.x, 0.3f) << name;
        EXPECT_NEAR(ink.centroid().y, centre.y, 0.3f) << name;
    }
}
