// IconButtonTests.cpp -- the one shared icon button: every style paints, the colour ladder is the
// single source of the glyph colour, the "when on" glyph follows the toggle state, and the focus ring
// is drawn on request. Images use SoftwareImageType(): the default native image type reads back
// zeros on a Windows runner.

#include "UI/Layout/IconButton.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace {

using synth::theme::Glyph;
using synth::ui::IconButton;
using Style = synth::ui::IconButton::Style;

constexpr int kSize = 24;

// A button the test can put into a hover or press state, standing in for the live mouse: setState is
// protected, and a headless component is never under the real pointer.
class HoverableIconButton : public IconButton {
public:
    using IconButton::IconButton;
    void hover() { setState(juce::Button::buttonOver); }
    void press() { setState(juce::Button::buttonDown); }
};

juce::Image render(juce::Component& component) {
    component.setSize(kSize, kSize);
    juce::Image image(juce::Image::ARGB, kSize, kSize, true, juce::SoftwareImageType());
    juce::Graphics g(image);
    component.paintEntireComponent(g, false);
    return image;
}

bool hasAnyPixel(const juce::Image& image) {
    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = 0; x < image.getWidth(); ++x)
            if (image.getPixelAt(x, y).getAlpha() != 0)
                return true;
    return false;
}

bool identical(const juce::Image& a, const juce::Image& b) {
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
            if (a.getPixelAt(x, y) != b.getPixelAt(x, y))
                return false;
    return true;
}

} // namespace

TEST(IconButtonTest, EveryStylePaintsSomething) {
    synth::theme::AppLookAndFeel lf;
    for (auto style : {Style::Framed, Style::Bare, Style::Round, Style::Danger}) {
        IconButton button("b", Glyph::Close, style);
        button.setLookAndFeel(&lf);
        EXPECT_TRUE(hasAnyPixel(render(button))) << "style " << (int)style;
        button.setLookAndFeel(nullptr);
    }
}

TEST(IconButtonTest, PaintsWithoutAnAppLookAndFeel) {
    IconButton button("b", Glyph::Play, Style::Framed);
    EXPECT_TRUE(hasAnyPixel(render(button)));
}

TEST(IconButtonTest, EveryGlyphPaintsSomething) {
    synth::theme::AppLookAndFeel lf;
    for (auto glyph : {Glyph::Play, Glyph::Stop, Glyph::RecordIdle, Glyph::RecordOn, Glyph::Loop, Glyph::ReturnToStart,
                       Glyph::Metronome, Glyph::Close, Glyph::Pin, Glyph::PinOn, Glyph::Delete, Glyph::MenuDots,
                       Glyph::EyeOpen, Glyph::EyeHidden}) {
        IconButton button("b", glyph);
        button.setLookAndFeel(&lf);
        EXPECT_TRUE(hasAnyPixel(render(button))) << "glyph " << (int)glyph;
        button.setLookAndFeel(nullptr);
    }
}

TEST(IconButtonTest, ColourLadderRestHoverOnDisabled) {
    synth::theme::AppLookAndFeel lf;
    const auto& c = lf.getTheme().colors;
    IconButton button("b", Glyph::Play);
    button.setLookAndFeel(&lf);

    EXPECT_EQ(button.glyphColour(false, false), c.textMuted) << "rest";
    EXPECT_EQ(button.glyphColour(true, false), c.textPrimary) << "hover";
    EXPECT_EQ(button.glyphColour(false, true), c.textPrimary) << "press";

    button.setToggleState(true, juce::dontSendNotification);
    EXPECT_EQ(button.glyphColour(false, false), c.accent) << "on";
    EXPECT_EQ(button.glyphColour(true, false), c.accent) << "on beats hover";

    button.setEnabled(false);
    EXPECT_EQ(button.glyphColour(true, false), c.textDisabled) << "disabled beats on and hover";
    button.setLookAndFeel(nullptr);
}

TEST(IconButtonTest, DangerStyleTurnsErrorColourWhenHot) {
    synth::theme::AppLookAndFeel lf;
    const auto& c = lf.getTheme().colors;
    IconButton button("b", Glyph::Delete, Style::Danger);
    button.setLookAndFeel(&lf);

    EXPECT_EQ(button.glyphColour(false, false), c.textMuted);
    EXPECT_EQ(button.glyphColour(true, false), c.error);
    EXPECT_EQ(button.glyphColour(false, true), c.error);
    button.setLookAndFeel(nullptr);
}

TEST(IconButtonTest, OnColourReplacesTheAccentWhenOn) {
    synth::theme::AppLookAndFeel lf;
    const juce::Colour red(0xFFE53935);
    IconButton button("b", Glyph::RecordIdle, Style::Framed);
    button.setLookAndFeel(&lf);
    button.setOnColour(red);

    EXPECT_NE(button.glyphColour(false, false), red) << "idle is not the on-colour";
    button.setToggleState(true, juce::dontSendNotification);
    EXPECT_EQ(button.glyphColour(false, false), red);

    button.setOnColour(std::nullopt);
    EXPECT_EQ(button.glyphColour(false, false), lf.getTheme().colors.accent);
    button.setLookAndFeel(nullptr);
}

TEST(IconButtonTest, GlyphWhenOnFollowsTheToggleState) {
    IconButton button("b", Glyph::Play);
    button.setGlyphWhenOn(Glyph::Stop);

    EXPECT_EQ(button.currentGlyph(), Glyph::Play);
    button.setToggleState(true, juce::dontSendNotification);
    EXPECT_EQ(button.currentGlyph(), Glyph::Stop);
    EXPECT_EQ(button.getGlyph(), Glyph::Play) << "the rest glyph is unchanged";
    button.setToggleState(false, juce::dontSendNotification);
    EXPECT_EQ(button.currentGlyph(), Glyph::Play);
}

TEST(IconButtonTest, WithoutAGlyphWhenOnTheGlyphStaysPut) {
    IconButton button("b", Glyph::Loop);
    button.setToggleState(true, juce::dontSendNotification);
    EXPECT_EQ(button.currentGlyph(), Glyph::Loop);
}

TEST(IconButtonTest, SetGlyphChangesWhatIsDrawn) {
    synth::theme::AppLookAndFeel lf;
    IconButton button("b", Glyph::Play);
    button.setLookAndFeel(&lf);
    const auto before = render(button);
    button.setGlyph(Glyph::Loop);
    EXPECT_EQ(button.getGlyph(), Glyph::Loop);
    EXPECT_FALSE(identical(before, render(button)));
    button.setLookAndFeel(nullptr);
}

TEST(IconButtonTest, TheButtonNeverSetsItsOwnAccessibilityText) {
    IconButton button("b", Glyph::Close);
    EXPECT_TRUE(button.getTitle().isEmpty());
    EXPECT_TRUE(button.getTooltip().isEmpty());
    EXPECT_TRUE(button.getDescription().isEmpty());
}

TEST(IconButtonTest, FocusRingAppearsWhenForced) {
    synth::theme::AppLookAndFeel lf;
    for (auto style : {Style::Framed, Style::Bare, Style::Round, Style::Danger}) {
        IconButton button("b", Glyph::Close, style);
        button.setLookAndFeel(&lf);
        const auto without = render(button);
        button.forceFocusRingForTest = true;
        const auto with = render(button);
        EXPECT_FALSE(identical(without, with)) << "style " << (int)style;
        // The ring hugs the edge: the leftmost pixel mid-way down is (nearly) opaque accent colour.
        const auto edge = with.getPixelAt(0, kSize / 2);
        const auto accent = lf.getTheme().colors.accent;
        EXPECT_GT(edge.getAlpha(), 200) << "style " << (int)style;
        EXPECT_NEAR(edge.getRed(), accent.getRed(), 4) << "style " << (int)style;
        EXPECT_NEAR(edge.getGreen(), accent.getGreen(), 4) << "style " << (int)style;
        EXPECT_NEAR(edge.getBlue(), accent.getBlue(), 4) << "style " << (int)style;
        button.setLookAndFeel(nullptr);
    }
}

TEST(IconButtonTest, HoverPaintsABackgroundOnABareButton) {
    synth::theme::AppLookAndFeel lf;
    HoverableIconButton button("b", Glyph::Close, Style::Bare);
    button.setLookAndFeel(&lf);
    const auto rest = render(button);

    button.hover();
    ASSERT_TRUE(button.isOver());
    const auto hovered = render(button);
    EXPECT_FALSE(identical(rest, hovered));
    // The wash reaches the corner region the glyph never touches.
    EXPECT_EQ(rest.getPixelAt(4, kSize / 2).getAlpha(), 0);
    EXPECT_GT(hovered.getPixelAt(4, kSize / 2).getAlpha(), 0);

    button.press();
    ASSERT_TRUE(button.isDown());
    EXPECT_GT(render(button).getPixelAt(4, kSize / 2).getAlpha(), hovered.getPixelAt(4, kSize / 2).getAlpha())
        << "press is a stronger wash than hover";
    button.setLookAndFeel(nullptr);
}

TEST(IconButtonTest, DisabledButtonShowsNoHoverWash) {
    synth::theme::AppLookAndFeel lf;
    HoverableIconButton button("b", Glyph::Close, Style::Bare);
    button.setLookAndFeel(&lf);
    button.setEnabled(false);
    button.hover();
    EXPECT_EQ(render(button).getPixelAt(4, kSize / 2).getAlpha(), 0);
    button.setLookAndFeel(nullptr);
}

TEST(IconButtonTest, FramedButtonDrawsItsBorderAndEngagedWash) {
    synth::theme::AppLookAndFeel lf;
    const juce::Colour red(0xFFE53935);
    IconButton button("b", Glyph::RecordIdle, Style::Framed);
    button.setLookAndFeel(&lf);
    button.setOnColour(red);

    // The border is stroked on the 1 px inset, so the top row outside the fill is its outer half.
    const auto idle = render(button).getPixelAt(kSize / 2, 0);
    EXPECT_GT(idle.getAlpha(), 0) << "idle border is drawn";
    EXPECT_LT(idle.getRed(), 0xA0) << "idle border is the neutral border colour, not red";

    button.setToggleState(true, juce::dontSendNotification);
    const auto engaged = render(button).getPixelAt(kSize / 2, 0);
    EXPECT_GT(engaged.getRed(), 0xC0) << "engaged border is the on-colour";
    EXPECT_LT(engaged.getBlue(), 0x80);
    button.setLookAndFeel(nullptr);
}

TEST(IconButtonTest, PlainOnToneReadsLikeHoverNotAccent) {
    synth::theme::AppLookAndFeel lf;
    const auto& c = lf.getTheme().colors;
    IconButton button("b", Glyph::EyeHidden);
    button.setLookAndFeel(&lf);
    button.setGlyphWhenOn(Glyph::EyeOpen);
    button.setOnTone(IconButton::OnTone::Plain);

    EXPECT_EQ(button.glyphColour(false, false), c.textMuted) << "off rests muted";
    button.setToggleState(true, juce::dontSendNotification);
    EXPECT_EQ(button.glyphColour(false, false), c.textPrimary) << "on is plain text, not the accent";
    button.setLookAndFeel(nullptr);
}

TEST(IconButtonTest, BareButtonToggledOnShowsTheLitAccentWash) {
    synth::theme::AppLookAndFeel lf;
    IconButton button("b", Glyph::Close, Style::Bare);
    button.setLookAndFeel(&lf);
    EXPECT_EQ(render(button).getPixelAt(4, kSize / 2).getAlpha(), 0) << "off and at rest: no background";
    button.setToggleState(true, juce::dontSendNotification);
    const auto lit = render(button).getPixelAt(4, kSize / 2);
    EXPECT_GT(lit.getAlpha(), 0);
    EXPECT_GT(lit.getBlue(), lit.getRed()) << "the wash is the accent, not a neutral grey";

    button.setOnTone(IconButton::OnTone::Plain);
    EXPECT_EQ(render(button).getPixelAt(4, kSize / 2).getAlpha(), 0) << "a plain on-tone stays unlit";
    button.setLookAndFeel(nullptr);
}
