// MixerOwnPanelSlideTests.cpp -- FRO231: the Mixer's "Own panel" opens and closes through its own
// PanelSlide (inside MixerPlacementController, separate from MainComponent's three fractions).
// Headless there is no VBlank, so the slide's frames are stood in for by the controller's test seams.

#include "MixerOwnPanelTestFixture.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"

TEST_F(MixerOwnPanelTest, HeadlessToggleLandsSynchronouslyBothWays) {
    useOwnPanelPlacement();
    MainComponent mc(std::make_unique<MockProviderTL>());
    showOwnPanel(mc);
    auto& own = mc.getMixerPlacementControllerForTest();
    ASSERT_EQ(own.getSlideProgressForTest(), 1.0f);

    mc.performToggleMixerPanel(); // close
    EXPECT_EQ(own.getSlideProgressForTest(), 0.0f);
    EXPECT_FALSE(own.isVisible());
    EXPECT_FALSE(own.isOwnPanelShowing());
    EXPECT_FALSE(own.isSlideAnimatingForTest()) << "no VBlank off-screen, so nothing is left running";
    EXPECT_EQ(own.getCarveHeight(), 0);
    EXPECT_EQ(mc.getGraphEditor().getBounds().getBottom(), mc.getStatusBar().getBounds().getY())
        << "a closed strip carves nothing off the canvas";

    mc.performToggleMixerPanel(); // open
    EXPECT_EQ(own.getSlideProgressForTest(), 1.0f);
    EXPECT_TRUE(own.isOwnPanelShowing());
    EXPECT_EQ(own.getCarveHeight(), 220);
    EXPECT_EQ(own.getHeight(), 220);
    EXPECT_EQ(own.getBottom(), mc.getStatusBar().getBounds().getY());
}

TEST_F(MixerOwnPanelTest, MidSlideCarveFollowsTheFraction) {
    useOwnPanelPlacement();
    writeSetting(Controller::kOwnPanelHeightKey, 400);
    MainComponent mc(std::make_unique<MockProviderTL>());
    showOwnPanel(mc);
    auto& own = mc.getMixerPlacementControllerForTest();

    for (const float progress : {0.25f, 0.5f, 0.9f}) {
        own.setSlideProgressForTest(progress);
        mc.resized();
        const int expected = (int)std::lround(progress * 400.0);
        EXPECT_EQ(own.getCarveHeight(), expected) << progress;
        EXPECT_EQ(own.getHeight(), expected) << progress;
        EXPECT_EQ(own.getBottom(), mc.getStatusBar().getBounds().getY()) << "pinned bottom edge, " << progress;
        EXPECT_EQ(mc.getGraphEditor().getBounds().getBottom(), own.getY()) << progress;
    }
}

TEST_F(MixerOwnPanelTest, OpeningIsVisibleBeforeTheFirstFrame) {
    useOwnPanelPlacement();
    MainComponent mc(std::make_unique<MockProviderTL>());
    showOwnPanel(mc);
    auto& own = mc.getMixerPlacementControllerForTest();
    mc.performToggleMixerPanel(); // close (synchronous)
    ASSERT_FALSE(own.isVisible());

    own.forceSlideAnimationForTest(true);
    mc.performToggleMixerPanel(); // open, as a real tween
    EXPECT_TRUE(own.isSlideAnimatingForTest());
    EXPECT_TRUE(own.isVisible()) << "visible before the first frame, or the opening would be a pop";
    EXPECT_TRUE(own.isOwnPanelShowing());
    EXPECT_EQ(own.getSlideProgressForTest(), 0.0f) << "frame 0 still measures nothing";
    EXPECT_EQ(own.getHeight(), 0);

    own.applySlideFrameForTest(0.5f);
    EXPECT_GT(own.getHeight(), 0);
    EXPECT_LT(own.getHeight(), 220);

    own.finishSlideForTest();
    EXPECT_FALSE(own.isSlideAnimatingForTest());
    EXPECT_EQ(own.getSlideProgressForTest(), 1.0f);
    EXPECT_EQ(own.getHeight(), 220);
    EXPECT_TRUE(own.isVisible());
}

TEST_F(MixerOwnPanelTest, ClosingStaysVisibleUntilTheSlideFinishes) {
    useOwnPanelPlacement();
    MainComponent mc(std::make_unique<MockProviderTL>());
    showOwnPanel(mc);
    auto& own = mc.getMixerPlacementControllerForTest();

    own.forceSlideAnimationForTest(true);
    mc.performToggleMixerPanel(); // close, as a real tween
    EXPECT_TRUE(own.isSlideAnimatingForTest());
    EXPECT_TRUE(own.isVisible()) << "a closing strip animates down instead of vanishing";
    EXPECT_TRUE(own.isOwnPanelShowing()) << "meters and the focus region treat the whole slide as showing";
    EXPECT_EQ(own.getSlideProgressForTest(), 1.0f);

    own.applySlideFrameForTest(0.5f);
    EXPECT_TRUE(own.isVisible());
    EXPECT_EQ(own.getHeight(), 110) << "half-way down";

    own.finishSlideForTest();
    EXPECT_FALSE(own.isVisible());
    EXPECT_EQ(own.getSlideProgressForTest(), 0.0f);
    EXPECT_EQ(own.getCarveHeight(), 0);
    EXPECT_FALSE(own.isSlideAnimatingForTest());
}

TEST_F(MixerOwnPanelTest, AMidFlightReversalStartsFromTheCurrentFraction) {
    useOwnPanelPlacement();
    MainComponent mc(std::make_unique<MockProviderTL>());
    showOwnPanel(mc);
    auto& own = mc.getMixerPlacementControllerForTest();

    own.forceSlideAnimationForTest(true);
    mc.performToggleMixerPanel(); // closing...
    own.applySlideFrameForTest(0.4f);
    const float mid = own.getSlideProgressForTest();
    ASSERT_GT(mid, 0.0f);
    ASSERT_LT(mid, 1.0f);

    mc.performToggleMixerPanel(); // ...reversed mid-flight
    EXPECT_FLOAT_EQ(own.getSlideTweenStartForTest(), mid) << "from where it is, never a jump to an extreme";
    EXPECT_FLOAT_EQ(own.getSlideProgressForTest(), mid);
    EXPECT_TRUE(own.isVisible());

    own.finishSlideForTest();
    EXPECT_EQ(own.getSlideProgressForTest(), 1.0f);
    EXPECT_TRUE(own.isVisible());
}

TEST_F(MixerOwnPanelTest, SwitchingPlacementSnapsWithNoAnimation) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    mc.setSize(1600, 900);
    auto& own = mc.getMixerPlacementControllerForTest();
    ASSERT_EQ(own.getCarveHeight(), 0) << "Tab placement";

    own.forceSlideAnimationForTest(true); // even with animation available, a placement change never tweens
    mc.getAppPropertiesForTest().getUserSettings()->setValue("mixerPlacement", "ownPanel");
    mc.getAppPropertiesForTest().getUserSettings()->saveIfNeeded();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

    EXPECT_EQ(own.getPlacement(), Controller::Placement::OwnPanel);
    EXPECT_EQ(own.getSlideProgressForTest(), 1.0f);
    EXPECT_FALSE(own.isSlideAnimatingForTest());
    EXPECT_TRUE(own.isOwnPanelShowing());
    EXPECT_EQ(own.getCarveHeight(), 220);
    EXPECT_EQ(own.getHeight(), 220) << "the live change re-lays out at once";
    EXPECT_TRUE(own.getResizeHandle().isVisible());

    mc.getAppPropertiesForTest().getUserSettings()->setValue("mixerPlacement", "tab");
    mc.getAppPropertiesForTest().getUserSettings()->saveIfNeeded();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

    EXPECT_EQ(own.getSlideProgressForTest(), 0.0f);
    EXPECT_FALSE(own.isOwnPanelShowing());
    EXPECT_EQ(own.getCarveHeight(), 0);
    EXPECT_FALSE(own.getResizeHandle().isVisible());
}
