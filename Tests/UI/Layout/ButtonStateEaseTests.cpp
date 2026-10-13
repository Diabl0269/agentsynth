#include "MotionPolish/MotionStep.h"
#include "UI/Layout/IconButton.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <cmath>
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

// Topic: the shared button drawing eases hover, press and toggled-on (docs/layout/theming.md#themed-widgets).
// Headless: the animated path is forced with FadeAnimateGuard and stepped by hand; images use SoftwareImageType().

namespace {

using synth::ui::AnimationMode;
using synth::ui::ButtonStateEase;

constexpr int kW = 80;
constexpr int kH = 28;

class HoverableTextButton : public juce::TextButton {
public:
    using juce::TextButton::TextButton;
    void hover() { setState(juce::Button::buttonOver); }
    void press() { setState(juce::Button::buttonDown); }
    void leave() { setState(juce::Button::buttonNormal); }
};

class HoverableIconButton : public synth::ui::IconButton {
public:
    using synth::ui::IconButton::IconButton;
    void hover() { setState(juce::Button::buttonOver); }
    void leave() { setState(juce::Button::buttonNormal); }
};

// The pixel at the middle of the button, where a text button with no text shows its fill.
juce::Colour centre(juce::Component& component) {
    component.setSize(kW, kH);
    juce::Image image(juce::Image::ARGB, kW, kH, true, juce::SoftwareImageType());
    juce::Graphics g(image);
    component.paintEntireComponent(g, false);
    return image.getPixelAt(kW / 2, kH / 2);
}

float distance(juce::Colour a, juce::Colour b) {
    return std::abs((int)a.getRed() - (int)b.getRed()) + std::abs((int)a.getGreen() - (int)b.getGreen()) +
           std::abs((int)a.getBlue() - (int)b.getBlue()) + std::abs((int)a.getAlpha() - (int)b.getAlpha());
}

struct Fixture {
    synth::theme::AppLookAndFeel lf;
    HoverableTextButton button{"b"};
    Fixture() { button.setLookAndFeel(&lf); }
    ~Fixture() { button.setLookAndFeel(nullptr); }
};

} // namespace

TEST(ButtonStateEase, HoverStepsThroughIntermediateColoursThenLandsOnTheFinalOne) {
    Fixture f;
    const auto rest = centre(f.button);
    {
        // The settled hover colour: drawn off screen without the guard, so it lands at once.
        Fixture reference;
        reference.button.hover();
        centre(reference.button);
        const auto hovered = centre(reference.button);
        ASSERT_GT(distance(rest, hovered), 8.0f)
            << "the hover fill must differ from rest for this test to mean anything";

        FadeAnimateGuard guard;
        f.button.hover();
        EXPECT_NEAR(distance(centre(f.button), rest), 0.0f, 1.0f) << "frame 0 is still the rest colour";
        EXPECT_TRUE(ButtonStateEase::isEasing(f.button));

        stepMotion(0.4f);
        const auto mid = centre(f.button);
        EXPECT_GT(distance(mid, rest), 1.0f);
        EXPECT_GT(distance(mid, hovered), 1.0f) << "mid-ease is strictly between rest and hover";
        stepMotion(0.7f);
        const auto later = centre(f.button);
        EXPECT_LT(distance(later, hovered), distance(mid, hovered)) << "it keeps approaching the hover colour";

        stepMotion(1.0f);
        EXPECT_EQ(centre(f.button), hovered);
        EXPECT_FALSE(ButtonStateEase::isEasing(f.button));
    }
}

TEST(ButtonStateEase, ReversalStartsFromWhereTheColourIs) {
    Fixture f;
    const auto rest = centre(f.button);
    FadeAnimateGuard guard;
    f.button.hover();
    centre(f.button);
    stepMotion(0.5f);
    const auto mid = centre(f.button);
    f.button.leave();
    EXPECT_EQ(centre(f.button), mid) << "leaving mid-ease must not jump to an extreme first";
    stepMotion(1.0f);
    EXPECT_EQ(centre(f.button), rest);
}

TEST(ButtonStateEase, PressAndToggledOnEaseToo) {
    Fixture f;
    f.button.setClickingTogglesState(true);
    f.button.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff203040));
    f.button.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff40a0e0));
    centre(f.button);
    FadeAnimateGuard guard;
    const auto rest = centre(f.button);
    f.button.setToggleState(true, juce::dontSendNotification);
    centre(f.button); // the first draw after the change starts the ease
    stepMotion(0.5f);
    const auto half = centre(f.button);
    stepMotion(1.0f);
    const auto on = centre(f.button);
    EXPECT_GT(distance(half, rest), 1.0f);
    EXPECT_GT(distance(on, half), 1.0f);
    EXPECT_GT(distance(on, rest), distance(half, rest));
}

TEST(ButtonStateEase, LandsAtOnceOffScreenAndUnderAnimationsOff) {
    {
        Fixture f; // not showing, not forced: a pixel test with no VBlank reads the final state
        const auto rest = centre(f.button);
        f.button.hover();
        EXPECT_FALSE(ButtonStateEase::isEasing(f.button));
        EXPECT_GT(distance(centre(f.button), rest), 8.0f);
    }
    {
        Fixture f;
        FadeAnimateGuard off(AnimationMode::off);
        const auto rest = centre(f.button);
        f.button.hover();
        const auto hovered = centre(f.button);
        EXPECT_FALSE(ButtonStateEase::isEasing(f.button));
        EXPECT_GT(distance(hovered, rest), 8.0f);
    }
}

TEST(ButtonStateEase, ReduceMotionIsAShortFadeNotAJump) {
    Fixture f;
    FadeAnimateGuard reduced(AnimationMode::reduced);
    const auto rest = centre(f.button);
    f.button.hover();
    centre(f.button);
    EXPECT_TRUE(ButtonStateEase::isEasing(f.button));
    EXPECT_EQ(centre(f.button), rest);
    stepMotion(1.0f);
    EXPECT_GT(distance(centre(f.button), rest), 8.0f);
}

TEST(ButtonStateEase, IconButtonGlyphAndWashEaseAndLandOnTheLadderColour) {
    synth::theme::AppLookAndFeel lf;
    HoverableIconButton button("i", synth::theme::Glyph::Close, synth::ui::IconButton::Style::Bare);
    button.setLookAndFeel(&lf);
    const auto rest = centre(button);
    const auto finalGlyph = button.glyphColour(true, false);
    EXPECT_NE(finalGlyph, button.glyphColour(false, false));
    {
        FadeAnimateGuard guard;
        button.hover();
        centre(button);
        stepMotion(0.5f);
        const auto mid = centre(button);
        EXPECT_NE(mid, rest);
        stepMotion(1.0f);
        const auto hovered = centre(button);
        EXPECT_NE(hovered, mid);
        EXPECT_FALSE(ButtonStateEase::isEasing(button));
        EXPECT_EQ(ButtonStateEase::current(button).hover, 1.0f);
    }
    button.setLookAndFeel(nullptr);
}

TEST(ButtonStateEase, ToggleButtonFillEasesWithItsState) {
    synth::theme::AppLookAndFeel lf;
    juce::ToggleButton toggle("t");
    toggle.setLookAndFeel(&lf);
    toggle.setSize(kW, kH);
    centre(toggle);
    FadeAnimateGuard guard;
    toggle.setToggleState(true, juce::dontSendNotification);
    centre(toggle);
    EXPECT_TRUE(ButtonStateEase::isEasing(toggle));
    stepMotion(1.0f);
    EXPECT_FLOAT_EQ(ButtonStateEase::current(toggle).on, 1.0f);
    toggle.setLookAndFeel(nullptr);
}

TEST(ButtonStateEase, DestroyingAButtonMidEaseIsSafe) {
    synth::theme::AppLookAndFeel lf;
    FadeAnimateGuard guard;
    auto button = std::make_unique<HoverableTextButton>("b");
    button->setLookAndFeel(&lf);
    centre(*button);
    button->hover();
    centre(*button);
    ASSERT_TRUE(ButtonStateEase::isEasing(*button));
    button->setLookAndFeel(nullptr);
    button.reset();
    stepMotion(0.5f); // no ease is left to step
    stepMotion(1.0f);
}

TEST(ButtonStateEase, BlendIsExactAtRestAndKeepsAWashTheSameColour) {
    const auto c = [](bool on, bool hot, bool down) {
        return down ? juce::Colours::red
                    : (hot ? juce::Colours::green : (on ? juce::Colours::blue : juce::Colours::transparentBlack));
    };
    EXPECT_EQ(ButtonStateEase::blend({0, 0, 0}, c), juce::Colours::transparentBlack);
    EXPECT_EQ(ButtonStateEase::blend({1, 0, 0}, c), juce::Colours::green);
    EXPECT_EQ(ButtonStateEase::blend({1, 1, 0}, c), juce::Colours::red);
    EXPECT_EQ(ButtonStateEase::blend({0, 0, 1}, c), juce::Colours::blue);
    const auto half = ButtonStateEase::mix(juce::Colours::transparentBlack, juce::Colours::white, 0.5f);
    EXPECT_EQ(half.getRed(), 255) << "a wash fades in as its own colour, not through grey";
    EXPECT_GT(half.getAlpha(), 100);
}
