// DrawableButtonFocusRingTests.cpp -- every juce::DrawableButton (the card header's Bypass, Mute, Delete and
// Dual I/O, the status bar's master mute, the panel detach button) shows a keyboard-focus ring, and only while it
// holds focus. A headless window cannot take focus, so the ring is forced on for the focused render. Images use
// SoftwareImageType(): the default native image type reads back zeros on a Windows runner.

#include "UI/Layout/FocusRing.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace {

constexpr int kButtonWidth = 22;
constexpr int kButtonHeight = 20;

juce::Image render(juce::DrawableButton& button, bool ringShown) {
    synth::ui::setFocusRingsShownForTest(ringShown);
    juce::Image image(juce::Image::ARGB, kButtonWidth, kButtonHeight, true, juce::SoftwareImageType());
    juce::Graphics g(image);
    button.paintEntireComponent(g, false);
    synth::ui::setFocusRingsShownForTest(false);
    return image;
}

bool anyPixelDiffers(const juce::Image& a, const juce::Image& b) {
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
            if (a.getPixelAt(x, y) != b.getPixelAt(x, y))
                return true;
    return false;
}

} // namespace

// Regression test for FRO1815: the card header buttons (DrawableButtons with a themed icon) showed no focus ring.
TEST(DrawableButtonFocusRingTest, AFocusedHeaderButtonPaintsARingAndAnUnfocusedOneDoesNot) {
    synth::theme::AppLookAndFeel lf;
    juce::DrawableButton button("Bypass", juce::DrawableButton::ImageFitted);
    button.setLookAndFeel(&lf);
    button.setSize(kButtonWidth, kButtonHeight);
    if (auto icon = lf.getIcon(synth::theme::Icon::ModuleBypass))
        button.setImages(icon.get());

    const auto plain = render(button, false);
    const auto focused = render(button, true);
    EXPECT_TRUE(anyPixelDiffers(plain, focused)) << "the focused button must differ from the unfocused one";
    EXPECT_EQ(focused.getPixelAt(kButtonWidth / 2, 1).getARGB(), lf.getTheme().colors.accent.getARGB())
        << "the ring is the accent colour, on the top edge";
    EXPECT_NE(plain.getPixelAt(kButtonWidth / 2, 1).getARGB(), lf.getTheme().colors.accent.getARGB());
    button.setLookAndFeel(nullptr);
}
