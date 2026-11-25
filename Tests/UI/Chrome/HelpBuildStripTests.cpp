// The Welcome screen's "Help build" strip: a living synth waveform behind the message, and a custom-drawn button whose
// light, shimmer and heart move with one clock (docs/layout/chrome.md#welcome-screen-overlay).
#include "UI/Chrome/HelpBuildButton.h"
#include "UI/Chrome/HelpBuildStrip.h"
#include "UI/Chrome/WelcomeScreenComponent.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <cmath>
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::theme {
// Declared here, not included: BuiltInThemes.h is at its header-reach cap.
std::vector<Theme> builtInThemes();
} // namespace synth::theme

namespace {

using synth::ui::HelpBuildButton;
using synth::ui::HelpBuildStrip;

double luminance(juce::Colour c) {
    const auto channel = [](float v) { return v <= 0.03928f ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * channel(c.getFloatRed()) + 0.7152 * channel(c.getFloatGreen()) + 0.0722 * channel(c.getFloatBlue());
}

double contrast(juce::Colour a, juce::Colour b) {
    const auto hi = std::max(luminance(a), luminance(b));
    const auto lo = std::min(luminance(a), luminance(b));
    return (hi + 0.05) / (lo + 0.05);
}

juce::Image render(juce::Component& c) {
    juce::Image image(juce::Image::ARGB, c.getWidth(), c.getHeight(), true, juce::SoftwareImageType());
    juce::Graphics g(image);
    c.paintEntireComponent(g, false);
    return image;
}

bool identical(const juce::Image& a, const juce::Image& b) {
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
            if (a.getPixelAt(x, y) != b.getPixelAt(x, y))
                return false;
    return true;
}

} // namespace

TEST(HelpBuildStrip, TheWaveformMorphsSineTriangleSawSquareAndBackToSine) {
    EXPECT_NEAR(HelpBuildStrip::waveformSample(0.25f, 0.0f), 1.0f, 1e-4f);  // sine peak
    EXPECT_NEAR(HelpBuildStrip::waveformSample(0.25f, 1.0f), 1.0f, 1e-4f);  // triangle peak
    EXPECT_NEAR(HelpBuildStrip::waveformSample(0.125f, 1.0f), 0.5f, 1e-4f); // triangle is straight
    EXPECT_NEAR(HelpBuildStrip::waveformSample(0.25f, 2.0f), 0.5f, 1e-4f);  // saw ramps
    EXPECT_NEAR(HelpBuildStrip::waveformSample(0.25f, 3.0f), 1.0f, 1e-4f);  // square high
    EXPECT_NEAR(HelpBuildStrip::waveformSample(0.75f, 3.0f), -1.0f, 1e-4f); // square low
    for (const float x : {0.0f, 0.1f, 0.3f, 0.6f, 0.9f})
        EXPECT_NEAR(HelpBuildStrip::waveformSample(x, 4.0f), HelpBuildStrip::waveformSample(x, 0.0f), 1e-4f)
            << "the cycle ends on the sine it started on, so the loop has no jump";
    EXPECT_NEAR(HelpBuildStrip::waveformSample(0.25f, 0.5f), 1.0f, 1e-4f) << "halfway between two peaks is a peak";
    for (float shape = 0.0f; shape <= 4.0f; shape += 0.25f)
        for (float x = 0.0f; x < 1.0f; x += 0.05f)
            EXPECT_LE(std::abs(HelpBuildStrip::waveformSample(x, shape)), 1.0001f);
}

TEST(HelpBuildStrip, AStripThatIsNotOnScreenRestsAndPaintsTheSameWhateverThePhase) {
    HelpBuildStrip strip;
    strip.setSize(544, 76);
    EXPECT_FALSE(strip.isMovingForTest()) << "a strip nobody can see schedules no frames";
    const auto first = render(strip);
    const auto second = render(strip);
    EXPECT_TRUE(identical(first, second));
}

TEST(HelpBuildStrip, AMovingStripPaintsDifferentlyAtDifferentPointsOfTheCycle) {
    HelpBuildStrip strip;
    strip.setSize(544, 76);
    strip.setPhaseForTest(0.10f);
    const auto early = render(strip);
    strip.setPhaseForTest(0.40f);
    const auto later = render(strip);
    EXPECT_FALSE(identical(early, later));
    // The shimmer rests for the first 55 percent of its 4 s loop, the waveform and light keep moving: the button
    // alone changes between two phases that are a whole fill loop apart only in what is not looped.
    strip.setPhaseForTest(0.0f);
    const auto start = render(strip);
    strip.setPhaseForTest(1.0f);
    const auto end = render(strip);
    EXPECT_TRUE(identical(start, end)) << "phase 1 is phase 0: the loop restarts without a jump";
}

TEST(HelpBuildStrip, TheButtonTextLeavesRoomAroundItAndTheMessageKeepsMostOfTheRow) {
    HelpBuildStrip strip;
    strip.setSize(544, 76);
    auto& button = strip.button();
    EXPECT_EQ(button.getButtonText(), "Help build Agent Synth");
    EXPECT_EQ(button.getWidth(), HelpBuildButton::preferredWidth(button.getButtonText()));
    EXPECT_EQ(button.getHeight(), HelpBuildButton::kHeight);
    const int textWidth = juce::GlyphArrangement::getStringWidthInt(synth::theme::AppLookAndFeel::uiSemiBoldFont(14.0f),
                                                                    button.getButtonText());
    EXPECT_GE(button.getWidth(), textWidth + 14 + 8 + 14 + 14) << "text clear of both edges, heart after it";
    EXPECT_GE(button.getY(), 12) << "room above for the glow";
    EXPECT_GE(strip.getHeight() - button.getBottom(), 12);
    EXPECT_FALSE(button.getBounds().intersects(strip.label().getBounds()));
    EXPECT_GE(strip.label().getWidth(), 280);
}

TEST(HelpBuildStrip, TheButtonTextReadsOnTheFillInEveryBuiltInTheme) {
    for (const auto& theme : synth::theme::builtInThemes()) {
        const auto ink = HelpBuildButton::inkColour(theme.colors);
        EXPECT_GE(contrast(ink, theme.colors.accent), 3.0) << theme.name << " accent end";
        EXPECT_GE(contrast(ink, theme.colors.hueRose), 3.0) << theme.name << " rose end";
    }
}

TEST(HelpBuildStrip, TheButtonPaintsItsFillInEveryBuiltInTheme) {
    for (const auto& theme : synth::theme::builtInThemes()) {
        synth::theme::AppLookAndFeel laf;
        laf.applyTheme(theme);
        HelpBuildStrip strip;
        strip.setLookAndFeel(&laf);
        strip.setSize(544, 76);
        strip.setPhaseForTest(0.3f);
        const auto image = render(strip);
        const auto centre = strip.button().getBounds().getCentre();
        const auto fill = image.getPixelAt(strip.button().getX() + 6, centre.y);
        EXPECT_GT(fill.getAlpha(), 200) << theme.name;
        EXPECT_NE(fill, theme.colors.surface) << theme.name << ": the button is not the plain panel";
        strip.setLookAndFeel(nullptr);
    }
}

TEST(HelpBuildStrip, TheWelcomeScreenHoldsTheStripWithTheSameButtonAsBefore) {
    synth::ui::WelcomeScreenComponent ws;
    ws.setBounds(0, 0, 1600, 900);
    auto& button = ws.getContributeButtonForTest();
    EXPECT_EQ(&button, &ws.getHelpBuildStripForTest().button());
    EXPECT_EQ(button.getTitle(), "Help build Agent Synth");
    EXPECT_TRUE(button.getWantsKeyboardFocus());
    EXPECT_EQ(ws.getHelpBuildStripForTest().getHeight(), 76);
}

// Kind: harness-mode-divergence (the loop only broke with animations off while it was running, as test mode starts).
TEST(HelpBuildStrip, ALoopRunningWhenAnimationsGoOffRestsInsteadOfRestartingForever) {
    const auto before = synth::ui::animationMode();
    HelpBuildStrip strip;
    strip.setPhaseForTest(0.3f); // as if its loop were running
    ASSERT_TRUE(strip.isMovingForTest());
    synth::ui::setAnimationMode(synth::ui::AnimationMode::off);
    strip.startCycleForTest(); // the driver completes at once; before the fix each cycle started the next, until the
                               // stack ended
    EXPECT_FALSE(strip.isMovingForTest()) << "the strip rests once animations are off";
    EXPECT_EQ(strip.phaseForTest(), 0.0f);
    synth::ui::setAnimationMode(before);
}

// Kind: harness-mode-divergence (the loop must also survive with animations on, as the real app and a motion-mode test
// launch run it: starting a cycle never completes it inside the call, so cycles cannot nest).
TEST(HelpBuildStrip, StartingManyCyclesWithAnimationsOnNeverCompletesOneInsideTheCall) {
    const auto before = synth::ui::animationMode();
    synth::ui::setAnimationMode(synth::ui::AnimationMode::full);
    HelpBuildStrip strip;
    strip.setPhaseForTest(0.3f); // as if its loop were running
    ASSERT_TRUE(strip.isMovingForTest());
    for (int i = 0; i < 20000; ++i)
        strip.startCycleForTest();
    EXPECT_TRUE(strip.isMovingForTest()) << "the loop keeps running while animations are on";
    synth::ui::setAnimationMode(before);
}
