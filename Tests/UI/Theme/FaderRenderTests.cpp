// Render tests for the fader drawn by AppLookAndFeel for every linear juce::Slider: real sliders are
// painted through the look-and-feel into a software image and pixels are checked. Images use
// SoftwareImageType because the platform default reads back zeros on the Windows CI.

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeelFader.h"
#include "UI/Theme/BuiltInThemes.h"
#include <cmath>
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

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
        laf.setKnobAppearance({synth::theme::KnobStyle::Classic, true}); // the pixel checks below are Classic
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
    EXPECT_EQ(channelDistance(outline(false, false), rig.theme.colors.textDisabled), 0);
    EXPECT_EQ(channelDistance(outline(true, false), rig.theme.colors.textMuted), 0);
    EXPECT_EQ(channelDistance(outline(false, true), rig.theme.colors.accent), 0);
}

INSTANTIATE_TEST_SUITE_P(Themes, FaderRenderTest, ::testing::Values(0, 1));

// The rest outline must stand out from the cap gradient's two end colours on every built-in theme
// (border sat within ~20 of surfaceHi on Obsidian, Neon Lab and Warm Console).
TEST(FaderThemeTest, RestOutlineContrastsWithCapFillInEveryTheme) {
    for (const auto& t : {synth::theme::makeObsidian(), synth::theme::makeNeon(), synth::theme::makeWarm(),
                          synth::theme::makeDaylight()}) {
        EXPECT_GE(channelDistance(t.colors.textDisabled, t.colors.surfaceHi), 100) << t.id.toStdString();
        EXPECT_GE(channelDistance(t.colors.textDisabled, t.colors.knobBody), 100) << t.id.toStdString();
    }
}

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

// ---- Styles and colour: a fader follows the knob style and the knob family colour rule ----

using synth::theme::KnobAppearance;
using synth::theme::KnobStyle;

int differingPixels(const juce::Image& a, const juce::Image& b) {
    int count = 0;
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
            if (a.getPixelAt(x, y) != b.getPixelAt(x, y))
                ++count;
    return count;
}

KnobStyle styleAt(int i) { return (KnobStyle)i; }

TEST(FaderStyleTest, EveryStylePaintsADifferentFader) {
    for (const auto orientation : {juce::Slider::LinearVertical, juce::Slider::LinearHorizontal}) {
        const bool vertical = orientation == juce::Slider::LinearVertical;
        std::vector<juce::Image> images;
        for (int i = 0; i < synth::theme::kKnobStyleCount; ++i) {
            FaderRig rig(synth::theme::makeObsidian(), orientation, vertical ? 100 : 120, vertical ? 150 : 22, 0.6);
            rig.laf.setKnobAppearance({styleAt(i), true});
            images.push_back(rig.paint());
        }
        for (size_t a = 0; a < images.size(); ++a)
            for (size_t b = a + 1; b < images.size(); ++b)
                EXPECT_GT(differingPixels(images[a], images[b]), 0)
                    << (vertical ? "vertical " : "horizontal ") << a << " vs " << b;
    }
}

TEST(FaderStyleTest, ThumbRadiusAndTravelDoNotMoveWithTheStyle) {
    for (const auto orientation : {juce::Slider::LinearVertical, juce::Slider::LinearHorizontal}) {
        const bool vertical = orientation == juce::Slider::LinearVertical;
        for (int i = 0; i < synth::theme::kKnobStyleCount; ++i) {
            FaderRig rig(synth::theme::makeObsidian(), orientation, vertical ? 100 : 120, vertical ? 150 : 22, 0.5);
            rig.laf.setKnobAppearance({styleAt(i), true});
            EXPECT_EQ(rig.laf.getSliderThumbRadius(rig.slider), vertical ? 9 : 7) << i;
            const auto travel = rig.laf.getSliderLayout(rig.slider).sliderBounds;
            EXPECT_EQ(travel.getHeight(), vertical ? 150 - 18 : 22) << i;
            EXPECT_EQ(travel.getWidth(), vertical ? 100 : 120 - 14) << i;
        }
    }
}

// A fader inside a parent that may carry the family property, like a module card.
struct FamilyFaderRig {
    FamilyFaderRig(KnobAppearance appearance, int family) {
        laf.applyTheme(synth::theme::makeObsidian());
        laf.setKnobAppearance(appearance);
        slider.setSliderStyle(juce::Slider::LinearVertical);
        slider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        slider.setRange(0.0, 1.0, 0.0);
        slider.setValue(0.5, juce::dontSendNotification);
        parent.setLookAndFeel(&laf);
        if (family >= 0)
            parent.getProperties().set(AppLookAndFeel::kKnobFamilyProperty, family);
        parent.addAndMakeVisible(slider);
        parent.setBounds(0, 0, 100, 150);
        slider.setBounds(0, 0, 100, 150);
    }
    ~FamilyFaderRig() { parent.setLookAndFeel(nullptr); }

    // A fill pixel just under the cap (full strength in every style).
    juce::Colour fillPixel() {
        auto img = makeImage(100, 150);
        juce::Graphics g(img);
        slider.paintEntireComponent(g, false);
        const int pos = (int)std::lround((float)slider.getPositionOfValue(slider.getValue()));
        return img.getPixelAt(50, pos + 9);
    }

    AppLookAndFeel laf;
    juce::Component parent;
    juce::Slider slider;
};

TEST(FaderStyleTest, FillTakesTheFamilyColourLikeAKnob) {
    const auto colors = synth::theme::makeObsidian().colors;
    ASSERT_GT(channelDistance(colors.hueAmber, colors.accent), 120) << "the test needs distinct colours";
    for (int i = 0; i < synth::theme::kKnobStyleCount; ++i) {
        const auto style = styleAt(i);
        FamilyFaderRig sources({style, true}, 0); // ModuleCategory 0 = sources
        EXPECT_LE(channelDistance(sources.fillPixel(), colors.hueAmber), 40) << i << " family on";
        FamilyFaderRig off({style, false}, 0);
        EXPECT_LE(channelDistance(off.fillPixel(), colors.accent), 40) << i << " family off";
        FamilyFaderRig mixer({style, true}, -1); // no family property: the mixer, a controller surface
        EXPECT_LE(channelDistance(mixer.fillPixel(), colors.accent), 40) << i << " no family";
    }
}

} // namespace
