// BottomDockTabFadeTests.cpp (docs/layout/animation.md, "Fading things in and out"): switching the bottom dock's tab
// cross-fades the hosts with FadeVisibility. Which tab is active lands at once; the leaving host stays on screen at
// falling opacity until its fade ends, then its panel's own flag follows. Off screen a switch lands at once.
#include "../../App/MainComponent/MainComponentTestFixture.h"
#include "BottomDockActiveTabResetGuard.h"
#include "FadeVisibilityTestGuard.h"
#include "UI/Layout/BottomDockComponent.h"
#include <gtest/gtest.h>
#include <memory>
#include <optional>

namespace {

using synth::ui::BottomDockComponent;
using synth::ui::FadeVisibility;
using Tab = BottomDockComponent::Tab;

class BottomDockTabFadeTest : public MainComponentTest {
protected:
    void SetUp() override {
        MainComponentTest::SetUp();
        resetGuard_.emplace();
        mcOwner_ = std::make_unique<MainComponent>(std::make_unique<MockProvider>());
        mcOwner_->setSize(1400, 900);
        mcOwner_->newPatchForTest();
    }
    void TearDown() override {
        mcOwner_.reset();
        resetGuard_.reset();
        MainComponentTest::TearDown();
    }
    BottomDockComponent& dock() { return mcOwner_->getBottomDock(); }
    juce::Component& host(Tab tab) { return dock().getTabHostForTest(tab); }

    std::optional<BottomDockActiveTabResetGuardMDT> resetGuard_;
    std::unique_ptr<MainComponent> mcOwner_;
};

} // namespace

TEST_F(BottomDockTabFadeTest, OffScreenASwitchLandsAtOnce) {
    dock().setActiveTab(Tab::Mixer);
    EXPECT_EQ(dock().getActiveTab(), Tab::Mixer);
    EXPECT_TRUE(host(Tab::Mixer).isVisible());
    EXPECT_FALSE(host(Tab::Timeline).isVisible());
    EXPECT_EQ(host(Tab::Mixer).getAlpha(), 1.0f);
    EXPECT_EQ(host(Tab::Timeline).getAlpha(), 1.0f);
}

TEST_F(BottomDockTabFadeTest, ASwitchCrossFadesTheLeavingHostOutAndTheArrivingHostIn) {
    FadeAnimateGuard guard;
    ASSERT_TRUE(host(Tab::Timeline).isVisible());

    dock().setActiveTab(Tab::Mixer);
    EXPECT_EQ(dock().getActiveTab(), Tab::Mixer) << "the active tab lands before the fade";
    EXPECT_TRUE(host(Tab::Mixer).isVisible());
    EXPECT_EQ(host(Tab::Mixer).getAlpha(), 0.0f) << "the arriving host starts transparent";
    EXPECT_TRUE(host(Tab::Timeline).isVisible()) << "the leaving host stays until its fade ends";
    EXPECT_TRUE(dock().getTimelineHost().getPanelForTest().isVisible()) << "its panel still paints";

    FadeVisibility::stepAllForTest(0.5f);
    EXPECT_NEAR(host(Tab::Mixer).getAlpha(), 0.5f, 0.01f);
    EXPECT_NEAR(host(Tab::Timeline).getAlpha(), 0.5f, 0.01f);

    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FALSE(host(Tab::Timeline).isVisible());
    EXPECT_FALSE(dock().getTimelineHost().getPanelForTest().isVisible());
    EXPECT_EQ(host(Tab::Timeline).getAlpha(), 1.0f) << "alpha is restored for the next time it shows";
    EXPECT_EQ(host(Tab::Mixer).getAlpha(), 1.0f);

    dock().setActiveTab(Tab::MidiRemote);
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_TRUE(host(Tab::MidiRemote).isVisible());
    EXPECT_FALSE(host(Tab::Mixer).isVisible());
}

TEST_F(BottomDockTabFadeTest, AnimationsOffSwitchesAtOnce) {
    FadeAnimateGuard guard(synth::ui::AnimationMode::off);
    dock().setActiveTab(Tab::Mixer);
    EXPECT_FALSE(host(Tab::Timeline).isVisible());
    EXPECT_TRUE(host(Tab::Mixer).isVisible());
    EXPECT_EQ(host(Tab::Mixer).getAlpha(), 1.0f);
}
