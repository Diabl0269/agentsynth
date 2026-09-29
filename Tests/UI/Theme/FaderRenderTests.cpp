// Render tests for the fader drawn by AppLookAndFeel for every linear juce::Slider: real sliders are
// painted through the look-and-feel into a software image and pixels are checked. Images use
// SoftwareImageType because the platform default reads back zeros on the Windows CI.

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeelFader.h"
#include "UI/Theme/BuiltInThemes.h"
#include <cmath>
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace {

using synth::theme::AppLookAndFeel;
using synth::theme::Theme;

int channelDistance(juce::Colour a, juce::Colour b) {
    return std::abs((int)a.getRed() - (int)b.getRed()) + std::abs((int)a.getGreen() - (int)b.getGreen()) +
           std::abs((int)a.getBlue() - (int)b.getBlue());
}

juce::Image makeImage(int w, int h) { return juce::Image(juce::Image::ARGB, w, h, true, juce::SoftwareImageType()); }

// One themed slider plus the helpers every test needs.
struct FaderRig {
    explicit FaderRig(const Theme& t, juce::Slider::SliderStyle style, int w, int h, double value)
        : theme(t) {
        laf.applyTheme(theme);
        slider.setSliderStyle(style);
        slider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        slider.setRange(0.0, 1.0, 0.0);
        slider.setLookAndFeel(&laf);
        slider.setBounds(0, 0, w, h);
        slider.setValue(value, juce::dontSendNotification);
    }
    ~FaderRig() { slider.setLookAndFeel(nullptr); }

    juce::Image paint() {
        auto img = makeImage(slider.getWidth(), slider.getHeight());
        juce::Graphics g(img);
        slider.paintEntireComponent(g, false);
        return img;
    }
    float pos() { return (float)slider.getPositionOfValue(slider.getValue()); }
    int cx() const { return slider.getWidth() / 2; }
    int cy() const { return slider.getHeight() / 2; }

    Theme theme;
    AppLookAndFeel laf;
    juce::Slider slider;
};

// A pixel on the centre line is blended with the cap gradient; it must sit closer to the pointer
// colour than a pixel a few px along the travel does.
void expectCentreLineAt(juce::Image& img, int x, int y, int dx, int dy, juce::Colour pointer) {
    const int onLine = channelDistance(img.getPixelAt(x, y), pointer);
    const int offLine = channelDistance(img.getPixelAt(x + dx, y + dy), pointer);
    EXPECT_LT(onLine, offLine);
}

class FaderRenderTest : public ::testing::TestWithParam<int> {
protected:
    Theme theme() const { return GetParam() == 0 ? synth::theme::makeObsidian() : synth::theme::makeDaylight(); }
};

TEST_P(FaderRenderTest, VerticalCentreLineFillAndSlot) {
    FaderRig rig(theme(), juce::Slider::LinearVertical, 100, 150, 0.5);
    auto img = rig.paint();
    const int pos = (int)std::lround(rig.pos());
    const auto& c = rig.theme.colors;

    expectCentreLineAt(img, rig.cx(), pos, 0, -4, c.knobPointer);
    EXPECT_EQ(channelDistance(img.getPixelAt(rig.cx(), pos + 25), c.accent), 0) << "fill below the cap";
    EXPECT_EQ(channelDistance(img.getPixelAt(rig.cx(), pos - 25), c.bg0), 0) << "empty slot above the cap";
}

TEST_P(FaderRenderTest, HorizontalCentreLineFillAndSlot) {
    FaderRig rig(theme(), juce::Slider::LinearHorizontal, 120, 22, 0.5);
    auto img = rig.paint();
    const int pos = (int)std::lround(rig.pos());
    const auto& c = rig.theme.colors;

    expectCentreLineAt(img, pos, rig.cy(), -4, 0, c.knobPointer);
    EXPECT_EQ(channelDistance(img.getPixelAt(pos - 25, rig.cy()), c.accent), 0) << "fill left of the cap";
    EXPECT_EQ(channelDistance(img.getPixelAt(pos + 25, rig.cy()), c.bg0), 0) << "empty slot right of the cap";
}

TEST_P(FaderRenderTest, DisabledIsDimmedWithDisabledFill) {
    FaderRig rig(theme(), juce::Slider::LinearVertical, 100, 150, 0.5);
    const int y = (int)std::lround(rig.pos()) + 25;
    const auto enabled = rig.paint().getPixelAt(rig.cx(), y);
    rig.slider.setEnabled(false);
    const auto disabled = rig.paint().getPixelAt(rig.cx(), y);

    EXPECT_EQ(enabled.getAlpha(), 255);
    EXPECT_NEAR((float)disabled.getAlpha(), 0.45f * 255.0f, 6.0f);
    EXPECT_LE(channelDistance(disabled.withAlpha(1.0f), rig.theme.colors.textDisabled), 6);
}

TEST_P(FaderRenderTest, CapStaysInsideBoundsAtBothEnds) {
    for (const double value : {0.0, 1.0}) {
        FaderRig v(theme(), juce::Slider::LinearVertical, 100, 150, value);
        auto vi = v.paint();
        const int vp = (int)std::lround(v.pos());
        EXPECT_GE(vp - 7, 0);
        EXPECT_LE(vp + 7, v.slider.getHeight());
        EXPECT_EQ(vi.getPixelAt(v.cx(), vp - 6).getAlpha(), 255) << "cap top row, value " << value;
        EXPECT_EQ(vi.getPixelAt(v.cx(), vp + 6).getAlpha(), 255) << "cap bottom row, value " << value;

        FaderRig h(theme(), juce::Slider::LinearHorizontal, 120, 22, value);
        auto hi = h.paint();
        const int hp = (int)std::lround(h.pos());
        EXPECT_GE(hp - 5, 0);
        EXPECT_LE(hp + 5, h.slider.getWidth());
        EXPECT_EQ(hi.getPixelAt(hp - 4, h.cy()).getAlpha(), 255) << "cap left column, value " << value;
        EXPECT_EQ(hi.getPixelAt(hp + 4, h.cy()).getAlpha(), 255) << "cap right column, value " << value;
    }
}

TEST_P(FaderRenderTest, FocusRingIsAccentOutsideTheCapOnly) {
    FaderRig rig(theme(), juce::Slider::LinearVertical, 100, 150, 0.5);
    const auto travel = rig.slider.getLookAndFeel().getSliderLayout(rig.slider).sliderBounds;
    const juce::Point<int> size{rig.slider.getWidth(), rig.slider.getHeight()};
    const int pos = (int)std::lround(rig.pos());
    // 30 px cap, ring centred 3 px outside its left edge: x = centre - 18.
    const int ringX = rig.cx() - 18;

    auto render = [&](bool focused) {
        auto img = makeImage(100, 150);
        juce::Graphics g(img);
        synth::theme::fader::State state;
        state.focused = focused;
        synth::theme::fader::paint(g, rig.theme, travel, size, rig.pos(), true, state);
        return img;
    };
    EXPECT_EQ(channelDistance(render(true).getPixelAt(ringX, pos), rig.theme.colors.accent), 0);
    EXPECT_NE(channelDistance(render(false).getPixelAt(ringX, pos), rig.theme.colors.accent), 0);
}

TEST_P(FaderRenderTest, HoverAndDragOutlineColours) {
    FaderRig rig(theme(), juce::Slider::LinearVertical, 100, 150, 0.5);
    const auto travel = rig.slider.getLookAndFeel().getSliderLayout(rig.slider).sliderBounds;
    const juce::Point<int> size{rig.slider.getWidth(), rig.slider.getHeight()};
    const int pos = (int)std::lround(rig.pos());
    const int edgeX = rig.cx() - 15; // cap's left outline column

    auto outline = [&](bool hover, bool drag) {
        auto img = makeImage(100, 150);
        juce::Graphics g(img);
        synth::theme::fader::State state;
        state.hover = hover;
        state.dragging = drag;
        synth::theme::fader::paint(g, rig.theme, travel, size, rig.pos(), true, state);
        return img.getPixelAt(edgeX, pos);
    };
    EXPECT_EQ(channelDistance(outline(false, false), rig.theme.colors.border), 0);
    EXPECT_EQ(channelDistance(outline(true, false), rig.theme.colors.textMuted), 0);
    EXPECT_EQ(channelDistance(outline(false, true), rig.theme.colors.accent), 0);
}

INSTANTIATE_TEST_SUITE_P(Themes, FaderRenderTest, ::testing::Values(0, 1));

TEST(FaderMetricsTest, SizePicksFollowTheRealBounds) {
    using namespace synth::theme::fader;
    EXPECT_EQ(metricsFor(true, 100, 150).capW, 30.0f) << "mixer strip -> large";
    EXPECT_EQ(metricsFor(true, 56, 30).capW, 18.0f) << "controller-surface cell -> small";
    EXPECT_EQ(metricsFor(false, 120, 22).capH, 18.0f) << "horizontal -> medium";
    EXPECT_LE(metricsFor(false, 120, 14).capH, 12.0f) << "cap shrinks in short bounds";
    EXPECT_EQ(thumbRadiusFor(true, 100, 150), 9);
    EXPECT_EQ(thumbRadiusFor(false, 120, 22), 7);
}

TEST(FaderThemeTest, DaylightMidiMappedBadgeIsReadablePurple) {
    EXPECT_EQ(synth::theme::makeDaylight().colors.midiMapped, juce::Colour(0xff9333EA));
}

} // namespace
