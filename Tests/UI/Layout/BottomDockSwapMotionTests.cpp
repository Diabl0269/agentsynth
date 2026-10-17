// BottomDockSwapMotionTests.cpp -- the motion of a main-view swap: the leaving view's picture moves down into the
// panel body while the arriving one grows up to the main area (240 ms), a plain 80 ms fade under Reduce Motion, at
// once when off screen, with Animations Off or with the panel closed; a second swap lands the first
// (docs/layout/animation.md "What moves, and how").
#include "BottomDockMainAreaFixture.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/MainViewSwapMotion.h"
#include "UI/Layout/ReducedMotion.h"
#include <gtest/gtest.h>
#include <vector>

namespace {

using synth::ui::BottomDockComponent;
using synth::ui::MainViewSwapMotion;
using Tab = BottomDockComponent::Tab;

class BottomDockSwapMotionTest : public BottomDockMainAreaTest {
protected:
    void SetUp() override {
        BottomDockMainAreaTest::SetUp();
        synth::ui::setAnimationMode(synth::ui::AnimationMode::full);
        synth::ui::FadeVisibility::setAnimateOffScreenForTest(true);
        mc_->ensureBottomDockOpen();
        mc_->resized();
        const auto wired = motion().onRunningChanged;
        motion().onRunningChanged = [this, wired](bool running) {
            events_.push_back(running);
            wired(running);
        };
    }
    void TearDown() override {
        synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
        synth::ui::setReducedMotionForTest(std::nullopt);
        synth::ui::setAnimationMode(synth::ui::AnimationMode::followSystem);
        BottomDockMainAreaTest::TearDown();
    }
    MainViewSwapMotion& motion() { return mc_->getMainViewSwapMotionForTest(); }
    juce::Rectangle<int> panelBody() { return dock().getPanelBodyBounds() + dock().getPosition(); }
    juce::Rectangle<int> mainRect() {
        return mc_->getGraphEditor().getBounds().getUnion(mc_->getCanvasToolbar().getBounds());
    }
    std::vector<bool> events_;
};

} // namespace

TEST_F(BottomDockSwapMotionTest, MidMoveBothPicturesAreOverTheFinishedLayout) {
    const auto main = mainRect();
    const auto body = panelBody();
    ASSERT_TRUE(dock().swapTabToMain(Tab::Timeline));

    ASSERT_TRUE(motion().isRunning());
    EXPECT_FALSE(motion().isFade());
    EXPECT_EQ(motion().durationMs(), 240.0);
    EXPECT_TRUE(motion().mainLayer().isVisible());
    EXPECT_TRUE(motion().bodyLayer().isVisible());
    EXPECT_TRUE(motion().mainLayer().isOpaque());
    EXPECT_EQ(motion().mainLayer().getBounds(), main);
    EXPECT_EQ(motion().bodyLayer().getBounds(), body);
    EXPECT_EQ(motion().leavingRect(), main.toFloat()) << "the leaving view starts where it was";
    EXPECT_EQ(motion().arrivingRect(), body.toFloat()) << "the arriving view starts in the panel body";

    EXPECT_EQ(dock().getTimelineHost().getParentComponent(), &holder()) << "the real components are already placed";
    EXPECT_EQ(holder().getBounds(), main);
    EXPECT_EQ(dock().getActiveTab(), Tab::Canvas) << "the view that went up is what the panel shows";

    motion().stepForTest(0.5f);
    EXPECT_TRUE(motion().isRunning());
    EXPECT_FLOAT_EQ(motion().leavingRect().getCentreY(),
                    (main.toFloat().getCentreY() + body.toFloat().getCentreY()) / 2);
    EXPECT_LT(motion().leavingRect().getHeight(), (float)main.getHeight());
    EXPECT_GT(motion().arrivingRect().getHeight(), (float)body.getHeight());
    EXPECT_TRUE(motion().mainLayer().isVisible());
}

TEST_F(BottomDockSwapMotionTest, TheLayersTakeNoClicksAndAreNotAccessibilityNodes) {
    ASSERT_TRUE(dock().swapTabToMain(Tab::Timeline));
    ASSERT_TRUE(motion().isRunning());
    for (auto* layer : {&motion().mainLayer(), &motion().bodyLayer()}) {
        bool clicks = true, childClicks = true;
        layer->getInterceptsMouseClicks(clicks, childClicks);
        EXPECT_FALSE(clicks);
        EXPECT_FALSE(childClicks);
        EXPECT_FALSE(layer->isAccessible());
        EXPECT_FALSE(layer->getWantsKeyboardFocus());
    }
    bool handleClicks = true, handleChildClicks = true;
    dock().getResizeHandle().getInterceptsMouseClicks(handleClicks, handleChildClicks);
    EXPECT_FALSE(handleClicks) << "the panel cannot be resized mid-move";

    motion().stepForTest(1.0f);
    dock().getResizeHandle().getInterceptsMouseClicks(handleClicks, handleChildClicks);
    EXPECT_TRUE(handleClicks);
    EXPECT_NE(mc_->getComponentAt(mainRect().getCentre()), &motion().mainLayer());
}

TEST_F(BottomDockSwapMotionTest, TheEndStateIsTheInstantSwapsLayout) {
    synth::ui::setAnimationMode(synth::ui::AnimationMode::off);
    ASSERT_TRUE(dock().swapTabToMain(Tab::Timeline));
    ASSERT_FALSE(motion().isRunning());
    const auto holderBounds = holder().getBounds();
    const auto hostBounds = dock().getTimelineHost().getBounds();
    const auto dockBounds = dock().getBounds();
    const auto activeTab = dock().getActiveTab();
    const bool graphShowing = mc_->getGraphEditor().isVisible();
    const auto canvasBounds = mc_->getGraphEditor().getBounds();
    ASSERT_TRUE(dock().swapTabToMain(Tab::Canvas));

    synth::ui::setAnimationMode(synth::ui::AnimationMode::full);
    events_.clear();
    ASSERT_TRUE(dock().swapTabToMain(Tab::Timeline));
    ASSERT_TRUE(motion().isRunning());
    motion().stepForTest(1.0f);

    EXPECT_FALSE(motion().isRunning());
    EXPECT_FALSE(motion().mainLayer().isVisible());
    EXPECT_FALSE(motion().bodyLayer().isVisible());
    EXPECT_EQ(events_, (std::vector<bool>{true, false}));
    EXPECT_EQ(holder().getBounds(), holderBounds);
    EXPECT_EQ(dock().getTimelineHost().getBounds(), hostBounds);
    EXPECT_EQ(dock().getBounds(), dockBounds);
    EXPECT_EQ(dock().getActiveTab(), activeTab);
    EXPECT_EQ(mc_->getGraphEditor().isVisible(), graphShowing);
    EXPECT_EQ(mc_->getGraphEditor().getBounds(), canvasBounds);
}

TEST_F(BottomDockSwapMotionTest, ReduceMotionIsAnEightyMillisecondFadeOverTheMainAreaOnly) {
    synth::ui::setAnimationMode(synth::ui::AnimationMode::followSystem);
    synth::ui::setReducedMotionForTest(true);
    const auto main = mainRect();
    ASSERT_TRUE(dock().swapTabToMain(Tab::Timeline));

    ASSERT_TRUE(motion().isRunning());
    EXPECT_TRUE(motion().isFade());
    EXPECT_EQ(motion().durationMs(), 80.0);
    EXPECT_TRUE(motion().mainLayer().isVisible());
    EXPECT_FALSE(motion().mainLayer().isOpaque()) << "the real arriving view shows through the fading picture";
    EXPECT_FALSE(motion().bodyLayer().isVisible());
    EXPECT_EQ(motion().mainLayer().getBounds(), main);
    motion().stepForTest(0.5f);
    EXPECT_EQ(motion().leavingRect(), main.toFloat()) << "a fade does not move";
    EXPECT_TRUE(motion().arrivingRect().isEmpty());
    motion().stepForTest(1.0f);
    EXPECT_FALSE(motion().isRunning());
}

TEST_F(BottomDockSwapMotionTest, OffScreenAnimationsOffAndAClosedPanelSwapAtOnce) {
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
    ASSERT_TRUE(dock().swapTabToMain(Tab::Timeline));
    EXPECT_FALSE(motion().isRunning()) << "off screen";
    EXPECT_FALSE(motion().mainLayer().isVisible());
    ASSERT_TRUE(dock().swapTabToMain(Tab::Canvas));

    synth::ui::FadeVisibility::setAnimateOffScreenForTest(true);
    synth::ui::setAnimationMode(synth::ui::AnimationMode::off);
    ASSERT_TRUE(dock().swapTabToMain(Tab::Timeline));
    EXPECT_FALSE(motion().isRunning()) << "Animations Off";
    ASSERT_TRUE(dock().swapTabToMain(Tab::Canvas));
    synth::ui::setAnimationMode(synth::ui::AnimationMode::full);

    mc_->simulateToggleBottomPanelClick();
    mc_->resized();
    ASSERT_FALSE(mc_->isBottomDockConfiguredVisible());
    ASSERT_TRUE(dock().setMainView(Tab::Timeline));
    EXPECT_FALSE(motion().isRunning()) << "no panel body to move into";
    EXPECT_TRUE(events_.empty());
}

TEST_F(BottomDockSwapMotionTest, ASecondSwapMidMoveLandsTheFirstThenStartsItsOwn) {
    ASSERT_TRUE(dock().swapTabToMain(Tab::Timeline));
    motion().stepForTest(0.3f);
    ASSERT_TRUE(dock().swapTabToMain(Tab::Mixer));

    EXPECT_EQ(events_, (std::vector<bool>{true, false, true})) << "the first ended before the next began";
    EXPECT_TRUE(motion().isRunning());
    EXPECT_FLOAT_EQ(motion().progress(), 0.0f);
    EXPECT_EQ(motion().leavingRect(), motion().mainLayer().getBounds().toFloat());
    EXPECT_EQ(dock().getMainView(), Tab::Mixer);
    motion().stepForTest(1.0f);
    EXPECT_EQ(events_, (std::vector<bool>{true, false, true, false}));
}

TEST_F(BottomDockSwapMotionTest, ALayoutPassMidMoveEndsItOnTheFinalFrame) {
    ASSERT_TRUE(dock().swapTabToMain(Tab::Timeline));
    ASSERT_TRUE(motion().isRunning());
    mc_->resized();
    EXPECT_FALSE(motion().isRunning());
    EXPECT_FALSE(motion().mainLayer().isVisible());
}
