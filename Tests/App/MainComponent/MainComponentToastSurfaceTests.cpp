// Concern: a toast sits where the action that caused it happened -- a timeline hotkey's toast over the timeline, a
// canvas action's toast over the canvas. Sweeps every toast source MainComponent wires, so a new source cannot ship
// without naming its surface.
#include "MainComponentTestFixture.h"
#include "UI/Chrome/Toast/ToastComponent.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/PolyChain/PolyChainController.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"

namespace {

synth::ui::ToastComponent* findToast(juce::Component& root) {
    for (auto* child : root.getChildren())
        if (auto* toast = dynamic_cast<synth::ui::ToastComponent*>(child))
            return toast;
    return nullptr;
}

} // namespace

TEST_F(MainComponentTest, EveryToastSourceSitsOverTheSurfaceWhereItsActionHappened) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1400, 900);
    mc.toggleBottomPanelFromShortcut(); // the fixture starts with the timeline closed
    ASSERT_TRUE(mc.isBottomDockConfiguredVisible());
    mc.resized();
    auto* toast = findToast(mc);
    ASSERT_NE(toast, nullptr);
    const auto timelineArea = mc.getLocalArea(&mc.getTimelinePanel(), mc.getTimelinePanel().getLocalBounds());
    ASSERT_FALSE(timelineArea.isEmpty());
    ASSERT_TRUE(mc.getTimelinePanel().isShowing() || !timelineArea.isEmpty());

    struct Source {
        const char* name;
        std::function<void()> fire;
        bool onTimeline;
    };
    auto& poly = mc.getGraphEditor().getPolyChain();
    const std::vector<Source> sources = {
        {"timeline hotkey", [&] { mc.getTimelinePanel().onShowMessage("Snap on"); }, true},
        {"poly chain", [&] { poly.showToast("Made 4 modules poly", "Undo", [] {}, "Undo this change"); }, false},
        {"cable refused", [&] { mc.getGraphEditor().onDropRefused("That jack cannot take it"); }, false},
    };
    for (const auto& source : sources) {
        toast->dismissNow();
        source.fire();
        ASSERT_TRUE(toast->isToastShown()) << source.name;
        toast->finishMotionForTest();
        const auto centre = toast->getBounds().getCentre();
        EXPECT_EQ(timelineArea.contains(centre), source.onTimeline) << source.name;
        // Over the timeline the toast stays inside its panel; elsewhere it clears the dock below the canvas.
        if (source.onTimeline)
            EXPECT_TRUE(timelineArea.contains(toast->getBounds())) << source.name;
        else
            EXPECT_LE(toast->getBottom(), timelineArea.getY()) << source.name;
    }
}

TEST_F(MainComponentTest, ATimelineToastFallsBackToTheCanvasWhenTheTimelineIsClosed) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1400, 900);
    ASSERT_FALSE(mc.isBottomDockConfiguredVisible());
    auto* toast = findToast(mc);
    ASSERT_NE(toast, nullptr);
    mc.getTimelinePanel().onShowMessage("Snap off");
    toast->finishMotionForTest();
    EXPECT_TRUE(toast->isToastShown());
    EXPECT_TRUE(mc.getLocalBounds().contains(toast->getBounds()));
}
