// AppTooltipWindowTests.cpp (docs/layout/animation.md#tooltips): the one tooltip window fades a tip in and out (the
// popups' plain 80 ms fade under Reduce Motion), and hides info tooltips (but never helper tips) when "Show info
// tooltips" is off. The window is a child of a plain parent component (no native window), so no VBlank frame ever
// arrives: the fade is driven by hand through the test seams, the way a VBlank would.
#include "UI/Layout/AppTooltipWindow.h"
#include "UI/Layout/PopupMotion.h"
#include "UI/Layout/ReducedMotion.h"
#include <gtest/gtest.h>

namespace {

using synth::ui::AppTooltipWindow;

struct ReducedMotionGuard {
    explicit ReducedMotionGuard(bool value) { synth::ui::setReducedMotionForTest(value); }
    ~ReducedMotionGuard() { synth::ui::setReducedMotionForTest(std::nullopt); }
};

// A parent hosting a tooltip window, over an in-memory settings file.
struct Hosted {
    Hosted() {
        juce::PropertiesFile::Options options;
        options.applicationName = "AppTooltipWindowTest";
        options.filenameSuffix = "test";
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        properties.setStorageParameters(options);
        properties.getUserSettings()->clear();
        parent.setSize(400, 300);
        window = std::make_unique<AppTooltipWindow>(&parent, &properties);
        window->setAnimateOffScreenForTest(true);
    }
    ~Hosted() {
        window.reset();
        properties.getUserSettings()->clear();
    }

    int childCount() const { return parent.getNumChildComponents(); }

    juce::ApplicationProperties properties;
    juce::Component parent;
    std::unique_ptr<AppTooltipWindow> window;
};

} // namespace

TEST(AppTooltipWindowTest, ATipFadesInFromTransparentAndLandsOnFullOpacity) {
    ReducedMotionGuard motion(false);
    Hosted h;
    h.window->displayTip({100, 100}, "Send level: drag to change");

    ASSERT_TRUE(h.window->isVisible());
    EXPECT_LT(h.window->getAlpha(), 1.0f) << "it starts transparent, so it never pops in whole";
    EXPECT_TRUE(h.window->isFading());

    h.window->applyFadeInFrameForTest(0.5f);
    EXPECT_GT(h.window->getAlpha(), 0.0f);
    EXPECT_LT(h.window->getAlpha(), 1.0f);

    h.window->applyFadeInFrameForTest(1.0f);
    EXPECT_EQ(h.window->getAlpha(), 1.0f);
}

TEST(AppTooltipWindowTest, ATipThatGoesFadesOutOnAGhostThenIsGone) {
    ReducedMotionGuard motion(false);
    Hosted h;
    h.window->displayTip({100, 100}, "Remove this send");
    h.window->applyFadeInFrameForTest(1.0f);
    const int before = h.childCount();

    h.window->setLastTipForTest("Remove this send"); // what getTipFor returned for it, as the mouse timer does
    h.window->hideTip();
    EXPECT_FALSE(h.window->isVisible()) << "the real window is hidden at once, as juce does";
    EXPECT_EQ(h.window->getAlpha(), 1.0f) << "and is ready for its next frame 0";
    EXPECT_TRUE(h.window->hasLeavingGhostForTest()) << "a copy fades out where it was";
    EXPECT_EQ(h.childCount(), before + 1);

    // A new tip while the old one is still leaving replaces the ghost.
    h.window->displayTip({100, 100}, "Add a send to a bus");
    EXPECT_FALSE(h.window->hasLeavingGhostForTest());
    EXPECT_EQ(h.childCount(), before);
}

TEST(AppTooltipWindowTest, ReduceMotionKeepsThePopupsPlainShortFade) {
    ReducedMotionGuard motion(true);
    Hosted h;
    h.window->displayTip({100, 100}, "Send level: drag to change");
    EXPECT_LT(h.window->getAlpha(), 1.0f) << "Reduce Motion still fades, like every popup (80 ms, no slide)";
    EXPECT_TRUE(h.window->isFading());
    EXPECT_EQ(synth::ui::popup_motion::durationMs(synth::ui::popup_motion::Phase::In, true), 80.0);

    h.window->applyFadeInFrameForTest(1.0f);
    h.window->setLastTipForTest("Send level: drag to change");
    h.window->hideTip();
    EXPECT_TRUE(h.window->hasLeavingGhostForTest()) << "and it fades out too";
}

TEST(AppTooltipWindowTest, InfoTooltipsAreSuppressedWhenThePreferenceIsOffButHelperTipsStillShow) {
    Hosted h;
    juce::TextButton ordinary("ordinary");
    ordinary.setTooltip("Mute or unmute this send");
    juce::TextButton helper("helper");
    helper.setTooltip("Plays into this channel: Lead");
    synth::ui::markHelperTooltip(helper);

    EXPECT_TRUE(h.window->areInfoTooltipsOn()) << "on by default";
    EXPECT_FALSE(h.window->suppresses(ordinary));
    EXPECT_FALSE(h.window->suppresses(helper));
    const auto ordinaryWhenOn = h.window->getTipFor(ordinary);
    const auto helperWhenOn = h.window->getTipFor(helper);

    h.properties.getUserSettings()->setValue(synth::ui::kShowInfoTooltipsKey, false);
    EXPECT_FALSE(h.window->areInfoTooltipsOn()) << "read live: no restart, no push";
    EXPECT_TRUE(h.window->suppresses(ordinary));
    EXPECT_FALSE(h.window->suppresses(helper));
    EXPECT_TRUE(h.window->getTipFor(ordinary).isEmpty());
    EXPECT_EQ(h.window->getTipFor(helper), helperWhenOn) << "a helper tip is untouched by the preference";
    (void)ordinaryWhenOn;

    h.properties.getUserSettings()->setValue(synth::ui::kShowInfoTooltipsKey, true);
    EXPECT_EQ(h.window->getTipFor(ordinary), ordinaryWhenOn);
}

TEST(AppTooltipWindowTest, AWindowWithNoSettingsKeepsInfoTooltipsOn) {
    juce::Component parent;
    AppTooltipWindow window(&parent);
    juce::TextButton button("b");
    EXPECT_TRUE(window.areInfoTooltipsOn());
    EXPECT_FALSE(window.suppresses(button));
}

TEST(AppTooltipWindowTest, AComponentIsAHelperTipOnlyOnceMarked) {
    juce::TextButton button("b");
    EXPECT_FALSE(synth::ui::isHelperTooltip(button));
    synth::ui::markHelperTooltip(button);
    EXPECT_TRUE(synth::ui::isHelperTooltip(button));
}
