// ToolbarAiSparkTests.cpp -- the AI button's spark on a patch cable: the cable and plug draw in the
// group colour with the spark at rest (dark and Daylight), a pulse runs along the cable while the
// assistant works or the button is hovered and stops after, Reduce Motion keeps it still, and the
// keyframes of one cycle land where the design puts them (docs/layout/icons.md#the-ai-button).
#include "ToolbarButtonTestHelpers.h"

using namespace toolbartest;
using synth::ui::toolbarAiFrame;

namespace {
// Icon-grid points: the middle of the plug body, and the spark's centre.
constexpr juce::Point<float> kPlugBody{13.3f, 11.5f};
constexpr juce::Point<float> kSparkCentre{19.2f, 5.0f};

void enter(juce::Component& c) { c.mouseEnter(mouseEventOn(c)); }
void exitButton(juce::Component& c) { c.mouseExit(mouseEventOn(c)); }

bool sameImage(const juce::Image& a, const juce::Image& b) {
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
            if (a.getPixelAt(x, y) != b.getPixelAt(x, y))
                return false;
    return true;
}
} // namespace

TEST(ToolbarAiKeyframesTest, ThePulseRunsTheCableInTheFirstHalfAndFadesAtTheEnd) {
    EXPECT_FLOAT_EQ(toolbarAiFrame(0.0f).pulseAlong, 0.0f);
    EXPECT_FLOAT_EQ(toolbarAiFrame(0.0f).pulseAlpha, 0.0f);
    EXPECT_FLOAT_EQ(toolbarAiFrame(0.06f).pulseAlpha, 1.0f) << "faded in by 6 percent";
    EXPECT_FLOAT_EQ(toolbarAiFrame(0.25f).pulseAlong, 0.5f);
    EXPECT_FLOAT_EQ(toolbarAiFrame(0.5f).pulseAlong, 1.0f) << "at the plug at 50 percent";
    EXPECT_FLOAT_EQ(toolbarAiFrame(0.5f).pulseAlpha, 1.0f);
    EXPECT_FLOAT_EQ(toolbarAiFrame(0.58f).pulseAlpha, 0.0f);
    EXPECT_FLOAT_EQ(toolbarAiFrame(0.9f).pulseAlpha, 0.0f);

    const auto start = synth::ui::toolbarAiPulsePoint(0.0f);
    const auto end = synth::ui::toolbarAiPulsePoint(1.0f);
    EXPECT_NEAR(start.x, 3.0f, 0.05f);
    EXPECT_NEAR(start.y, 21.0f, 0.05f);
    EXPECT_NEAR(end.x, 12.0f, 0.05f);
    EXPECT_NEAR(end.y, 13.0f, 0.05f);
    EXPECT_GT(synth::ui::toolbarAiPulsePoint(0.5f).y, 13.0f);
    EXPECT_LT(synth::ui::toolbarAiPulsePoint(0.5f).y, 21.0f);
}

TEST(ToolbarAiKeyframesTest, TheGlowSwellsAtThePlugAndTheSparkBlooms) {
    EXPECT_FLOAT_EQ(toolbarAiFrame(0.45f).glowAlpha, 0.0f);
    EXPECT_FLOAT_EQ(toolbarAiFrame(0.58f).glowAlpha, 0.9f);
    EXPECT_FLOAT_EQ(toolbarAiFrame(1.0f).glowAlpha, 0.0f);

    EXPECT_FLOAT_EQ(toolbarAiFrame(0.48f).sparkAlpha, 0.0f);
    EXPECT_FLOAT_EQ(toolbarAiFrame(0.48f).sparkScale, 0.15f);
    const auto bloom = toolbarAiFrame(0.64f);
    EXPECT_NEAR(bloom.sparkScale, 1.12f, 1.0e-3f);
    EXPECT_NEAR(bloom.sparkDegrees, 30.0f, 1.0e-2f);
    EXPECT_NEAR(bloom.sparkAlpha, 1.0f, 1.0e-3f);
    EXPECT_NEAR(toolbarAiFrame(0.82f).sparkScale, 0.95f, 1.0e-3f);
    EXPECT_NEAR(toolbarAiFrame(0.82f).sparkDegrees, 45.0f, 1.0e-2f);
    EXPECT_NEAR(toolbarAiFrame(1.0f).sparkScale, 0.4f, 1.0e-3f);
    EXPECT_NEAR(toolbarAiFrame(1.0f).sparkDegrees, 70.0f, 1.0e-2f);
    EXPECT_NEAR(toolbarAiFrame(1.0f).sparkAlpha, 0.0f, 1.0e-3f);
    // The bounce: the scale overshoots its target partway through the bloom.
    float peak = 0.0f;
    for (float p = 0.48f; p <= 0.64f; p += 0.002f)
        peak = juce::jmax(peak, toolbarAiFrame(p).sparkScale);
    EXPECT_GT(peak, 1.12f);
}

TEST_F(ToolbarButtonTest, TheAiIconShowsItsCablePlugAndSparkAtRestInEveryTheme) {
    for (const auto& theme : {synth::theme::makeObsidian(), synth::theme::makeDaylight()}) {
        useTheme(theme);
        auto& b = make(Icon::ToggleAI, ToolbarGroup::AI, "Show AI");
        const auto img = render(b);
        const auto hue = this->theme().colors.hueRose;
        EXPECT_TRUE(pixelNear(img, iconPixel(b, kPlugBody), hue, 4)) << theme.name << " plug body";
        EXPECT_TRUE(areaContains(img, iconArea(b), hue, 4)) << theme.name << " cable";
        EXPECT_TRUE(
            pixelNear(img, iconPixel(b, kSparkCentre), this->theme().isDark ? juce::Colour(0xffff7ab6) : hue, 14))
            << theme.name << " spark";
        EXPECT_FALSE(b.isLoopActive());
        EXPECT_EQ(b.getLoopPhase(), 0.0f);
        EXPECT_EQ(b.getLoopFrame().pulseAlpha, 0.0f) << "no pulse at rest";
        EXPECT_GT(b.getLoopFrame().sparkAlpha, 0.9f);
        EXPECT_FALSE(areaContains(img, iconArea(b), this->theme().colors.hueAmber, 2)) << "no pulse drawn at rest";
    }
}

TEST_F(ToolbarButtonTest, WhileTheAssistantWorksThePulseLoopsAndWhenItStopsTheIconIsAtRestAgain) {
    ReducedMotionGuard guard;
    synth::ui::setReducedMotionForTest(false);
    auto& b = make(Icon::ToggleAI, ToolbarGroup::AI, "Show AI");
    b.setTitle("Show AI");
    const auto rest = render(b);

    b.setBusy(true);
    EXPECT_TRUE(b.isBusy());
    EXPECT_TRUE(b.isLoopActive());
    EXPECT_EQ(b.getDescription(), "Assistant is working");
    EXPECT_EQ(b.getTitle(), "Show AI") << "the name is untouched";

    b.setLoopPhaseForTest(0.10f);
    const auto early = b.getLoopFrame();
    const auto earlyImage = render(b);
    b.setLoopPhaseForTest(0.40f);
    const auto later = b.getLoopFrame();
    const auto laterImage = render(b);
    EXPECT_GT(later.pulseAlong, early.pulseAlong);
    const auto p0 = synth::ui::toolbarAiPulsePoint(early.pulseAlong);
    const auto p1 = synth::ui::toolbarAiPulsePoint(later.pulseAlong);
    EXPECT_GT(p0.getDistanceFrom(p1), 3.0f) << "the pulse moved along the cable";
    EXPECT_FALSE(sameImage(earlyImage, laterImage));
    EXPECT_FALSE(sameImage(earlyImage, rest));
    EXPECT_TRUE(areaContains(earlyImage, iconArea(b), theme().colors.hueAmber, 20)) << "the pulse is amber";

    b.setBusy(false);
    EXPECT_FALSE(b.isBusy());
    EXPECT_FALSE(b.isLoopActive());
    EXPECT_EQ(b.getLoopPhase(), 0.0f);
    EXPECT_TRUE(b.getDescription().isEmpty());
    EXPECT_TRUE(sameImage(render(b), rest)) << "back to the rest drawing";
}

TEST_F(ToolbarButtonTest, HoveringTheAiButtonPlaysTheLoopAndLeavingStopsIt) {
    ReducedMotionGuard guard;
    synth::ui::setReducedMotionForTest(false);
    auto& b = make(Icon::ToggleAI, ToolbarGroup::AI, "Show AI");
    enter(b);
    EXPECT_TRUE(b.isLoopActive());
    EXPECT_TRUE(b.getDescription().isEmpty()) << "hover is not a busy state";
    b.setLoopPhaseForTest(0.25f);
    EXPECT_GT(b.getLoopFrame().pulseAlpha, 0.9f);
    exitButton(b);
    EXPECT_FALSE(b.isLoopActive());
    EXPECT_EQ(b.getLoopFrame().pulseAlpha, 0.0f);

    // Busy and hovered at once, then the hover ends: it keeps playing until the assistant is done.
    b.setBusy(true);
    enter(b);
    exitButton(b);
    EXPECT_TRUE(b.isLoopActive());
    b.setBusy(false);
    EXPECT_FALSE(b.isLoopActive());
}

TEST_F(ToolbarButtonTest, UnderReduceMotionNeitherBusyNorHoverMovesThePulse) {
    ReducedMotionGuard guard;
    synth::ui::setReducedMotionForTest(true);
    auto& b = make(Icon::ToggleAI, ToolbarGroup::AI, "Show AI");
    const auto rest = render(b);
    b.setBusy(true);
    EXPECT_FALSE(b.isLoopActive());
    EXPECT_EQ(b.getDescription(), "Assistant is working") << "the screen reader still hears it";
    enter(b);
    EXPECT_FALSE(b.isLoopActive());
    b.setLoopPhaseForTest(0.4f);
    EXPECT_EQ(b.getLoopFrame().pulseAlpha, 0.0f);
    EXPECT_FLOAT_EQ(b.getLoopFrame().sparkScale, synth::ui::toolbarAiRestFrame().sparkScale);
    EXPECT_FALSE(areaContains(render(b), iconArea(b), theme().colors.hueAmber, 2)) << "no pulse";
    exitButton(b);
    b.setBusy(false);
    EXPECT_TRUE(sameImage(render(b), rest));
}

TEST_F(ToolbarButtonTest, OtherIconsNeverLoop) {
    ReducedMotionGuard guard;
    synth::ui::setReducedMotionForTest(false);
    auto& b = make(Icon::ActionSettings, ToolbarGroup::Housekeeping, "Settings");
    b.setBusy(true);
    enter(b);
    EXPECT_FALSE(b.isLoopActive());
}

TEST_F(ToolbarButtonTest, ALitAiButtonDrawsItsSparkInInk) {
    auto& b = make(Icon::ToggleAI, ToolbarGroup::AI, "Hide AI");
    b.setToggleState(true, juce::dontSendNotification);
    const auto img = render(b);
    EXPECT_TRUE(pixelNear(img, iconPixel(b, kSparkCentre), theme().colors.iconInk, 14));
}
