// DepthSettingIntegrationTests.cpp -- the Depth look through a real MainComponent: a level chosen on the ThemeManager
// reaches the shared AppLookAndFeel, cross-fades the window for 200 ms (80 ms under Reduce Motion, not at all with
// Animations Off), leaves no picture behind, and a plain theme switch fades nothing.
#include "../../TestWait.h"
#include "../../UI/Layout/FadeVisibilityTestGuard.h"
#include "MainComponentTestFixture.h"
#include "UI/Layout/FadeOutPicture.h"
#include "UI/Theme/DepthLevel.h"
#include "UI/Theme/ThemeManager.h"
#include "UserSettings.h"
#include <optional>

namespace {

using synth::theme::DepthLevel;

// The depth key lives in the real settings file every MainComponent reads: put it back as it was.
class PersistedDepthKeyGuard {
public:
    PersistedDepthKeyGuard() {
        juce::ApplicationProperties props;
        props.setStorageParameters(synth::userSettingsOptions());
        if (auto* s = props.getUserSettings())
            if (s->containsKey(synth::theme::depthLevelKey()))
                saved_ = s->getValue(synth::theme::depthLevelKey());
    }
    ~PersistedDepthKeyGuard() {
        juce::ApplicationProperties props;
        props.setStorageParameters(synth::userSettingsOptions());
        if (auto* s = props.getUserSettings()) {
            saved_ ? s->setValue(synth::theme::depthLevelKey(), *saved_)
                   : s->removeValue(synth::theme::depthLevelKey());
            s->saveIfNeeded();
        }
    }

private:
    std::optional<juce::String> saved_;
};

struct DepthRig {
    DepthRig()
        : mc(themeManager, lookAndFeel, std::make_unique<MockProvider>()) {
        mc.setSize(900, 600);
        mc.setVisible(true); // a component is hidden until shown; the picture is only taken of one on show
        themeManager.setDepthLevel(DepthLevel::Soft); // a known start whatever the settings file held
        synth::test::waitUntil([&] { return lookAndFeel.getDepthLevel() == DepthLevel::Soft; });
    }

    synth::ui::FadeOutPicture* fade() {
        return dynamic_cast<synth::ui::FadeOutPicture*>(mc.findChildWithID("depthFadePicture"));
    }
    bool fadeShowing() {
        auto* f = fade();
        return f != nullptr && f->isPlaying();
    }
    void settle() { synth::test::flushMessageQueue(); }

    PersistedDepthKeyGuard guard;
    synth::theme::ThemeManager themeManager;
    synth::theme::AppLookAndFeel lookAndFeel;
    MainComponent mc;
};

} // namespace

TEST_F(MainComponentTest, ADepthChoiceReachesTheSharedLookAndFeel) {
    DepthRig rig;
    EXPECT_EQ(rig.lookAndFeel.getDepthLevel(), DepthLevel::Soft);
    rig.themeManager.setDepthLevel(DepthLevel::Rich);
    synth::test::waitUntil([&] { return rig.lookAndFeel.getDepthLevel() == DepthLevel::Rich; });
    EXPECT_EQ(rig.lookAndFeel.getDepthLevel(), DepthLevel::Rich);
    EXPECT_EQ(rig.lookAndFeel.getTheme().id, rig.themeManager.getActiveThemeId());
}

TEST_F(MainComponentTest, ADepthChangeCrossFadesTheWindowThenLeavesNothingBehind) {
    DepthRig rig;
    // after the rig: MainComponent applies the saved Animations mode when it is built
    FadeAnimateGuard animating;
    rig.themeManager.setDepthLevel(DepthLevel::Flat);
    synth::test::waitUntil([&] { return rig.lookAndFeel.getDepthLevel() == DepthLevel::Flat; });

    ASSERT_NE(rig.fade(), nullptr);
    EXPECT_TRUE(rig.fadeShowing()) << "the old look is pictured over the new one";
    EXPECT_FALSE(interceptsClicks(*rig.fade())) << "mouse-transparent";
    EXPECT_FALSE(rig.fade()->isAccessible());
    EXPECT_EQ(rig.fade()->getBounds(), rig.mc.getLocalBounds());
    EXPECT_NEAR(rig.fade()->fadeMsNowForTest(), 200.0, 0.01);

    synth::ui::FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FALSE(rig.fadeShowing());
    EXPECT_FALSE(rig.fade()->isVisible()) << "static at idle";
}

TEST_F(MainComponentTest, TheDepthFadeIsThePlainShortFadeUnderReduceMotion) {
    DepthRig rig;
    // after the rig: MainComponent applies the saved Animations mode when it is built
    FadeAnimateGuard animating(synth::ui::AnimationMode::reduced);
    rig.themeManager.setDepthLevel(DepthLevel::Rich);
    synth::test::waitUntil([&] { return rig.lookAndFeel.getDepthLevel() == DepthLevel::Rich; });
    ASSERT_NE(rig.fade(), nullptr);
    EXPECT_TRUE(rig.fadeShowing());
    EXPECT_NEAR(rig.fade()->fadeMsNowForTest(), synth::ui::FadeVisibility::kReducedMs, 0.01);
    synth::ui::FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FALSE(rig.fadeShowing());
}

TEST_F(MainComponentTest, ADepthChangeWithAnimationsOffShowsNoFadeAtAll) {
    DepthRig rig;
    // after the rig: MainComponent applies the saved Animations mode when it is built
    FadeAnimateGuard animating(synth::ui::AnimationMode::off);
    rig.themeManager.setDepthLevel(DepthLevel::Flat);
    synth::test::waitUntil([&] { return rig.lookAndFeel.getDepthLevel() == DepthLevel::Flat; });
    EXPECT_FALSE(rig.fadeShowing());
}

TEST_F(MainComponentTest, APlainThemeSwitchShowsNoDepthFade) {
    DepthRig rig;
    // after the rig: MainComponent applies the saved Animations mode when it is built
    FadeAnimateGuard animating;
    ASSERT_TRUE(rig.themeManager.setActiveTheme("daylight")); // broadcasts synchronously
    rig.settle();
    EXPECT_EQ(rig.lookAndFeel.getTheme().id, "daylight");
    EXPECT_FALSE(rig.fadeShowing());
    EXPECT_EQ(rig.fade(), nullptr) << "never even built for a switch that leaves the depth alone";
}
