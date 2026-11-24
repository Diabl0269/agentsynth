// FocusRingConsistencyTests.cpp -- the focus ring is one ring in every built-in theme: the same stroke
// thickness and gap whichever control paints it, and the icons on the active dock tab read against the
// tab's fill. Images use SoftwareImageType(): the default native image type reads back zeros on a
// Windows runner.

#include "../Layout/BottomDockMainAreaFixture.h"
#include "UI/Graph/CardBody/CardBodyMoreButton.h"
#include "UI/Layout/BottomDockTabModel.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Layout/IconButton.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeelFader.h"
#include "UI/Theme/BuiltInThemes.h"
#include "UI/Theme/ReadableText.h"
#include <cmath>
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace {

using synth::theme::AppLookAndFeel;
using synth::theme::Theme;

juce::Image ringBlank(int w, int h) { return juce::Image(juce::Image::ARGB, w, h, true, juce::SoftwareImageType()); }

// The ring's stroke thickness as painted: the opacity summed down the middle column of `image`, over its two
// horizontal edges, halved.
float ringPaintedThickness(const juce::Image& image) {
    float total = 0.0f;
    for (int y = 0; y < image.getHeight(); ++y)
        total += image.getPixelAt(image.getWidth() / 2, y).getFloatAlpha();
    return total * 0.5f;
}

} // namespace

TEST(FocusRingConsistency, EveryThemesRingIsOneThicknessAndLightThemesAreThicker) {
    for (const auto& theme : synth::theme::builtInThemes()) {
        AppLookAndFeel lf;
        lf.applyTheme(theme);
        juce::Component comp;
        comp.setLookAndFeel(&lf);
        comp.setSize(60, 40);

        for (const float radius : {0.0f, 6.0f}) {
            auto image = ringBlank(60, 40);
            {
                juce::Graphics g(image);
                synth::ui::paintFocusRingAlways(g, comp.getLocalBounds().toFloat(), comp, radius);
            }
            EXPECT_NEAR(ringPaintedThickness(image), synth::ui::focusRingThickness(theme), 0.1f)
                << theme.id << " radius " << radius;
        }
        EXPECT_GT(synth::ui::focusRingThickness(theme), theme.metrics.borderWidth) << theme.id;
        comp.setLookAndFeel(nullptr);
    }
}

// The fader used to stroke its own 2 px ring; it now paints the shared one, so its stroke is the theme's.
TEST(FocusRingConsistency, TheFadersFocusRingIsTheSharedRingAtTheSharedGap) {
    using synth::theme::fader::State;
    for (const auto& theme : synth::theme::builtInThemes()) {
        const juce::Rectangle<int> travel(20, 10, 24, 120);
        const juce::Point<int> size(64, 140);
        constexpr float kPos = 70.0f;
        State focused;
        focused.focused = true;

        auto with = ringBlank(size.x, size.y);
        auto without = ringBlank(size.x, size.y);
        {
            juce::Graphics g(with);
            synth::theme::fader::paint(g, theme, travel, size, kPos, true, focused);
        }
        {
            juce::Graphics g(without);
            synth::theme::fader::paint(g, theme, travel, size, kPos, true, State{});
        }

        // Across the cap's centre row the ring crosses twice; each crossing is one shared thickness at most (the
        // cap's own pixels are untouched, so only ring pixels differ).
        const auto m = synth::theme::fader::metricsFor(true, size.x, size.y);
        const auto cap = synth::theme::fader::capBounds(travel.toFloat(), kPos, true, m);
        const int row = (int)cap.getCentreY();
        float ringPixels = 0.0f;
        for (int x = 0; x < size.x; ++x)
            ringPixels += with.getPixelAt(x, row) != without.getPixelAt(x, row) ? 1.0f : 0.0f;
        EXPECT_GT(ringPixels, 0.0f) << theme.id << ": a focused fader shows a ring";
        EXPECT_LE(ringPixels, 2.0f * std::ceil(synth::ui::focusRingThickness(theme)) + 2.0f)
            << theme.id << ": the ring is no thicker than the shared one";
        EXPECT_GE(ringPixels, 2.0f * std::floor(synth::ui::focusRingThickness(theme)))
            << theme.id << ": the ring is no thinner than the shared one";

        // The pixel just outside the cap's left edge by the shared gap is empty; the next band is the ring.
        const int gapX = (int)std::floor(cap.getX() - synth::ui::kFocusRingGap * 0.5f);
        EXPECT_EQ(with.getPixelAt(gapX, row), without.getPixelAt(gapX, row)) << theme.id << ": the gap is clear";
    }
}

// Regression test for FRO1917: the icons on the active dock tab (grey on the light theme's light-blue fill) read
// at 4.5:1 or better against the tab's fill, at rest and under the pointer, in every built-in theme.
TEST(FocusRingConsistency, TheActiveTabsIconsReadAgainstItsFillInEveryTheme) {
    for (const auto& theme : synth::theme::builtInThemes()) {
        AppLookAndFeel lf;
        lf.applyTheme(theme);
        juce::TextButton tab("Mixer");
        tab.setLookAndFeel(&lf);
        tab.setSize(120, 19);
        tab.setToggleState(true, juce::dontSendNotification);
        auto* icon = new synth::ui::IconButton("swapMainView", synth::theme::Glyph::Swap);
        tab.addAndMakeVisible(icon);
        icon->setLookAndFeel(&lf);

        const auto fill = tab.findColour(juce::TextButton::buttonOnColourId);
        for (const bool hot : {false, true}) {
            const auto glyph = synth::theme::iconButtonGlyphColour(theme, *icon, hot, false);
            EXPECT_GE(synth::theme::contrastBetween(glyph, fill), synth::theme::kReadableTextContrast)
                << theme.id << (hot ? " hovered" : " at rest");
        }
        icon->setLookAndFeel(nullptr);
        tab.setLookAndFeel(nullptr);
    }
}

// Regression test for FRO1917: the active tab's ring stood on the tab's own accent fill and vanished. A band of the
// strip's background colour now separates them: ring, then the gap, then the fill.
TEST_F(BottomDockMainAreaTest, TheActiveTabsFocusRingStandsOffItsFillByAGap) {
    using synth::ui::BottomDockComponent;
    dock().setBounds(0, 0, 900, 200);
    dock().setActiveTab(BottomDockComponent::Tab::Timeline);
    const auto& theme = synth::theme::themeOf(dock());
    juce::Rectangle<int> tab;
    for (auto* button : dock().getTabButtons())
        if (button->isVisible() && dynamic_cast<juce::TextButton*>(button)->getToggleState())
            tab = button->getBounds();
    ASSERT_FALSE(tab.isEmpty()) << "an active tab shows in the strip";

    auto render = [&](bool ring) {
        synth::ui::setFocusRingsShownForTest(ring);
        auto image = ringBlank(dock().getWidth(), dock().getHeight());
        juce::Graphics g(image);
        dock().paintEntireComponent(g, false);
        synth::ui::setFocusRingsShownForTest(false);
        return image;
    };
    const auto shown = render(true);
    const int y = tab.getCentreY();
    const int x = tab.getX();
    EXPECT_EQ(shown.getPixelAt(x, y), theme.colors.accent) << "the ring is on the tab's outer edge";
    const int gapX = x + (int)std::ceil(synth::ui::focusRingThickness(theme)) + 1;
    EXPECT_EQ(shown.getPixelAt(gapX, y), theme.colors.bg0) << "then a clear gap";
    EXPECT_GE(synth::ui::kTabFocusRingGap, 2.0f);
}

// A focused More button paints the look and feel's one ring and nothing more: its edge band is pixel-identical to a
// plain button's. Regression test for FRO2258: the button painted a second ring at another radius, which showed as
// a doubled outline.
TEST(FocusRingConsistency, AFocusedMoreButtonPaintsOneFocusRing) {
    for (const auto& theme : synth::theme::builtInThemes()) {
        synth::theme::AppLookAndFeel lf;
        lf.applyTheme(theme);
        synth::CardMoreButton more(7);
        juce::TextButton plain(more.getButtonText());
        for (auto* b : {static_cast<juce::Component*>(&more), static_cast<juce::Component*>(&plain)}) {
            b->setLookAndFeel(&lf);
            b->setSize(180, 20);
        }
        synth::ui::setFocusRingsShownForTest(true);
        auto paint = [](juce::Component& c) {
            juce::Image image(juce::Image::ARGB, c.getWidth(), c.getHeight(), true, juce::SoftwareImageType());
            juce::Graphics g(image);
            c.paintEntireComponent(g, false);
            return image;
        };
        const auto a = paint(more);
        const auto b = paint(plain);
        synth::ui::setFocusRingsShownForTest(false);
        int different = 0;
        for (int y = 0; y < a.getHeight(); ++y)
            for (int x = 0; x < a.getWidth(); ++x) {
                const bool inBand = x < 5 || y < 5 || x >= a.getWidth() - 5 || y >= a.getHeight() - 5;
                if (inBand && a.getPixelAt(x, y) != b.getPixelAt(x, y))
                    ++different;
            }
        EXPECT_EQ(different, 0) << theme.id;
        more.setLookAndFeel(nullptr);
        plain.setLookAndFeel(nullptr);
    }
}
