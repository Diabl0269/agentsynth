// SectionDropHintLookTests.cpp -- the guard for drag hints that land off the drop point or blend into what they sit on.
// The placement function is enumerated over every drop-line position, strip width and window edge; the colours over
// every built-in theme, against the strip, the clip lane under the pill and every section palette colour. The last
// test proves the strip really paints the pill from these colours, under the strip, at the drop line.
#include "Timeline/TimelineDoc/SectionPalette.h"
#include "TimelineSectionStripFixture.h"
#include "TimelineSectionStripThreeBlocksFixture.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include "UI/Theme/ReadableText.h"
#include "UI/Timeline/TimelineSectionStrip/SectionDropHintLook.h"
#include <gtest/gtest.h>

using namespace sectionstrip_test;
using synth::theme::contrastBetween;
using synth::ui::sectionstrip::dropHintColours;
using synth::ui::sectionstrip::dropHintPillRect;

namespace {
constexpr double kStandsOut = 3.0; // a filled shape against its ground (WCAG non-text)
constexpr double kNotATwin = 1.5;  // a neutral section colour must still differ in lightness from the pill
constexpr float kNeutral = 0.15f;  // a fill with less channel spread than this has no hue of its own
constexpr int kStripHeight = 24;

juce::Colour opaqueOn(juce::Colour ground, juce::Colour c) { return ground.overlaidWith(c); }

// How far a colour is from grey: the spread of its channels, 0 to 1. HSV saturation calls a near-black grey with a
// faint cast saturated, so this reads hue the way the eye does.
float chroma(juce::Colour c) {
    const float r = c.getFloatRed(), g = c.getFloatGreen(), b = c.getFloatBlue();
    return std::max({r, g, b}) - std::min({r, g, b});
}
} // namespace

TEST(SectionDropHintLook, ThePillSitsAtTheDropLineOutsideTheStripWhereverTheLineIs) {
    for (const float stripW : {120.0f, 400.0f, 1200.0f}) {
        for (const float pillW : {90.0f, 160.0f, 260.0f}) {
            for (const bool roomBelow : {true, false}) {
                for (float lineX = -20.0f; lineX <= stripW + 20.0f; lineX += 7.0f) {
                    const auto pill = dropHintPillRect(lineX, pillW, stripW, kStripHeight, roomBelow);
                    const juce::String where = "strip " + juce::String(stripW) + " pill " + juce::String(pillW) +
                                               " line " + juce::String(lineX) + " below " +
                                               juce::String((int)roomBelow);
                    EXPECT_FALSE(pill.intersects(juce::Rectangle<float>(0.0f, 0.0f, stripW, (float)kStripHeight)))
                        << "never on the strip's row, so it covers no section name: " << where;
                    if (roomBelow)
                        EXPECT_GE(pill.getY(), (float)kStripHeight) << where;
                    else
                        EXPECT_LE(pill.getBottom(), 0.0f) << where;
                    EXPECT_LE(pill.getY() - (float)kStripHeight, 10.0f) << "just under the strip: " << where;
                    EXPECT_GE(pill.getX(), 0.0f) << where;
                    EXPECT_LE(pill.getRight(), stripW + 0.01f) << where;
                    const float line = std::clamp(lineX, 0.0f, stripW);
                    EXPECT_TRUE(line >= pill.getX() - 0.01f && line <= pill.getRight() + 0.01f)
                        << "the drop line runs through the pill: " << where;
                    if (pill.getX() > 0.0f && pill.getRight() < stripW)
                        EXPECT_NEAR(pill.getCentreX(), lineX, 0.01f) << "centred on the line: " << where;
                }
            }
        }
    }
}

TEST(SectionDropHintLook, ThePillStandsOutFromTheStripTheLaneAndEverySectionInEveryTheme) {
    for (const auto& theme : synth::theme::builtInThemes()) {
        const auto& c = theme.colors;
        const auto look = dropHintColours(c);
        const auto strip = opaqueOn(c.bg0, c.surface);
        const auto lane = opaqueOn(c.bg0, c.bg1);
        EXPECT_TRUE(look.fill.isOpaque()) << theme.id;
        EXPECT_GE(contrastBetween(look.text, look.fill), synth::theme::kReadableTextContrast) << theme.id;
        EXPECT_GE(contrastBetween(look.fill, strip), kStandsOut) << theme.id << ": against the Sections strip";
        EXPECT_GE(contrastBetween(look.fill, lane), kStandsOut) << theme.id << ": against the clip lane under it";
        EXPECT_LT(chroma(look.fill), kNeutral) << theme.id << ": no hue, so never tinted like a section";
        for (const auto argb : synth::kSectionColours) {
            const juce::Colour section(argb);
            if (chroma(section) < kNeutral)
                EXPECT_GE(contrastBetween(look.fill, section), kNotATwin)
                    << theme.id << ": a grey section " << section.toDisplayString(false);
        }
    }
}

// Regression test for FRO2141: the pill was filled with the raised surface colour, close to the strip and its blocks.
TEST(SectionDropHintLook, TheStripPaintsThePillInTheseColoursAtTheDropLine) {
    for (const auto& theme : synth::theme::builtInThemes()) {
        synth::theme::AppLookAndFeel lf;
        lf.applyTheme(theme);
        ThreeBlocks f;
        juce::Component window;
        window.setLookAndFeel(&lf);
        window.setSize(1300, 200);
        window.addAndMakeVisible(f.strip);
        f.strip.setBounds(0, 40, 1200, kStripHeight);
        const juce::Point<float> grab{f.x(2.0), 12.0f};
        f.strip.mouseDown(f.event(grab, grab, false));
        f.strip.mouseDrag(f.event({f.x(9.0), 12.0f}, grab, true));
        f.strip.getMotionForTest().stepForTest(0.0);
        f.strip.getMotionForTest().stepForTest(600.0); // the lift has run in: the pill is at full strength
        ASSERT_FALSE(f.strip.getDropHintTextForTest().isEmpty()) << theme.id;
        juce::Component* layer = nullptr;
        for (auto* child : window.getChildren())
            if (child != &f.strip && child->isVisible())
                layer = child;
        ASSERT_NE(layer, nullptr) << theme.id;
        juce::Image image(juce::Image::ARGB, layer->getWidth(), layer->getHeight(), true, juce::SoftwareImageType());
        {
            juce::Graphics g(image);
            layer->paintEntireComponent(g, false);
        }
        const auto pill = f.strip.getDropHintBoundsForTest();
        const auto inLayer = layer->getLocalArea(&f.strip, pill.toNearestInt());
        // A point inside the rounded body, clear of the words: just right of the left cap, near the bottom edge.
        const auto probe = juce::Point<int>(inLayer.getX() + (int)(pill.getHeight() * 0.6f), inLayer.getBottom() - 3);
        const auto painted = image.getPixelAt(probe.x, probe.y);
        const auto want = dropHintColours(theme.colors).fill;
        EXPECT_NEAR(painted.getRed(), want.getRed(), 4) << theme.id;
        EXPECT_NEAR(painted.getGreen(), want.getGreen(), 4) << theme.id;
        EXPECT_NEAR(painted.getBlue(), want.getBlue(), 4) << theme.id;
        EXPECT_GE(pill.getY(), (float)f.strip.getHeight()) << theme.id << ": under the strip";
        f.strip.mouseUp(f.event({f.x(9.0), 12.0f}, grab, true));
        window.removeChildComponent(&f.strip);
        window.setLookAndFeel(nullptr);
    }
}
