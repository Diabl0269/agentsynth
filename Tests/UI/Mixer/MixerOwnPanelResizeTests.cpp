// MixerOwnPanelResizeTests.cpp -- FRO231: the Mixer's "Own panel" strip has a persisted user height
// ("mixerOwnPanelHeight") and its own top-edge PanelResizeHandle, and shares the window's 3/4 budget
// with the bottom dock. Real off-screen MainComponent, synthesized mouse events.

#include "MixerOwnPanelTestFixture.h"
#include "UI/Layout/PanelResizeHandle.h"

namespace {
using Handle = synth::ui::PanelResizeHandle;
} // namespace

// ---- The clamp, as a pure function ----

TEST(MixerOwnPanelClampTest, FloorIsTheDefaultHeightAndCeilingIsThreeQuartersOfTheWindowMinusTheDock) {
    using C = synth::ui::MixerPlacementController;
    EXPECT_EQ(C::clampHeight(5000, 900, 0), 675) << "75% of the 900 px window";
    EXPECT_EQ(C::clampHeight(5000, 900, 220), 455) << "the open dock keeps its minimum";
    EXPECT_EQ(C::clampHeight(10, 900, 0), C::kOwnPanelMinHeight);
    EXPECT_EQ(C::clampHeight(400, 900, 220), 400);
    EXPECT_EQ(C::clampHeight(5000, 100, 0), C::kOwnPanelMinHeight) << "on a tiny window the floor wins";
    EXPECT_EQ(C::clampHeight(5000, 900, 800), C::kOwnPanelMinHeight)
        << "an oversized reservation cannot go below the floor";
    EXPECT_EQ(C::clampHeight(900, 0, 0), 900) << "before the first layout only the floor applies";
    EXPECT_EQ(C::kOwnPanelMinHeight, 220);
}

// ---- Handle geometry and hit test ----

TEST_F(MixerOwnPanelTest, HandleSitsOnTheStripsTopEdgeAndTheHostStartsBelowIt) {
    useOwnPanelPlacement();
    MainComponent mc(std::make_unique<MockProviderTL>());
    showOwnPanel(mc);
    auto& own = mc.getMixerPlacementControllerForTest();
    auto& handle = own.getResizeHandle();

    EXPECT_EQ(handle.getComponentID(), "ownPanelResizeHandle");
    EXPECT_EQ(handle.getBounds(), juce::Rectangle<int>(0, 0, own.getWidth(), Handle::kHeight));
    EXPECT_TRUE(handle.isVisible());
    EXPECT_TRUE(handle.getMouseCursor() == juce::MouseCursor::UpDownResizeCursor);
    EXPECT_EQ(mc.getBottomDock().getMixerHost().getY(), Handle::kHeight)
        << "the hosted panel's own header never sits under the grab strip";
    EXPECT_EQ(own.getComponentAt(10, 2), &handle) << "the handle wins the hit test over the host";
    auto* below = own.getComponentAt(10, Handle::kHeight + 2);
    EXPECT_TRUE(below == &mc.getBottomDock().getMixerHost() || mc.getBottomDock().getMixerHost().isParentOf(below))
        << "just under the strip is the hosted panel";
}

TEST_F(MixerOwnPanelTest, TabAndWindowPlacementsCarveNothingAndShowNoHandle) {
    for (const char* placement : {"tab", "window"}) {
        writeSetting("mixerPlacement", placement);
        MainComponent mc(std::make_unique<MockProviderTL>());
        mc.setSize(1600, 900);
        auto& own = mc.getMixerPlacementControllerForTest();
        EXPECT_EQ(own.getCarveHeight(), 0) << placement;
        EXPECT_FALSE(own.isOwnPanelShowing()) << placement;
        EXPECT_FALSE(own.getResizeHandle().isVisible()) << placement;
    }
}

// ---- The drag ----

TEST_F(MixerOwnPanelTest, DefaultsToTheMinimumHeightWhenNothingIsPersisted) {
    useOwnPanelPlacement();
    MainComponent mc(std::make_unique<MockProviderTL>());
    showOwnPanel(mc);
    auto& own = mc.getMixerPlacementControllerForTest();

    EXPECT_EQ(own.getOwnPanelHeight(), 220);
    EXPECT_EQ(own.getCarveHeight(), 220);
    EXPECT_EQ(own.getHeight(), 220);
    EXPECT_EQ(own.getBottom(), mc.getStatusBar().getBounds().getY()) << "pinned to the window's bottom edge";
    EXPECT_EQ(readPersistedOwnHeight(mc), -1) << "showing the strip writes no height";
}

TEST_F(MixerOwnPanelTest, DraggingResizesLiveAndPersistsOnlyOnMouseUp) {
    useOwnPanelPlacement();
    MainComponent mc(std::make_unique<MockProviderTL>());
    showOwnPanel(mc);
    auto& own = mc.getMixerPlacementControllerForTest();
    auto& handle = own.getResizeHandle();

    handle.mouseDown(makeClickEvent(handle, {10.0f, 2.0f}));
    // 142 px above the grab point against the pinned bottom edge: 220 + 142.
    handle.mouseDrag(makeDragEvent(handle, {10.0f, -140.0f}, {10.0f, 2.0f}));

    EXPECT_EQ(own.getOwnPanelHeight(), 362);
    EXPECT_EQ(own.getHeight(), 362) << "LIVE: MainComponent already re-laid out";
    EXPECT_EQ(own.getBottom(), mc.getStatusBar().getBounds().getY());
    EXPECT_EQ(handle.getBounds(), juce::Rectangle<int>(0, 0, own.getWidth(), Handle::kHeight))
        << "the handle rides the strip's top edge";
    EXPECT_EQ(readPersistedOwnHeight(mc), -1) << "not persisted per pixel";

    handle.mouseUp(makeClickEvent(handle, {10.0f, -140.0f}));
    EXPECT_EQ(readPersistedOwnHeight(mc), 362);
}

TEST_F(MixerOwnPanelTest, AStrayClickOnTheHandleNeverPersists) {
    useOwnPanelPlacement();
    MainComponent mc(std::make_unique<MockProviderTL>());
    showOwnPanel(mc);
    auto& own = mc.getMixerPlacementControllerForTest();
    auto& handle = own.getResizeHandle();

    handle.mouseDown(makeClickEvent(handle, {10.0f, 2.0f}));
    handle.mouseUp(makeClickEvent(handle, {10.0f, 2.0f}));
    EXPECT_EQ(own.getOwnPanelHeight(), 220);
    EXPECT_EQ(readPersistedOwnHeight(mc), -1);
}

TEST_F(MixerOwnPanelTest, HeightIsClampedToTheFloorAndToThreeQuartersOfTheWindow) {
    useOwnPanelPlacement();
    MainComponent mc(std::make_unique<MockProviderTL>());
    showOwnPanel(mc);
    auto& own = mc.getMixerPlacementControllerForTest();

    own.setOwnPanelHeight(5000, false);
    EXPECT_EQ(own.getOwnPanelHeight(), 675) << "75% of the 900 px window";
    EXPECT_EQ(own.getHeight(), 675);
    EXPECT_GT(mc.getGraphEditor().getBounds().getHeight(), 0);

    own.setOwnPanelHeight(10, false);
    EXPECT_EQ(own.getOwnPanelHeight(), 220) << "the default is the floor";
    EXPECT_EQ(readPersistedOwnHeight(mc), -1) << "only the commit persists";

    own.setOwnPanelHeight(5000, true);
    EXPECT_EQ(readPersistedOwnHeight(mc), 675) << "the committed value is the clamped one";
}

TEST_F(MixerOwnPanelTest, AnOpenDockReservesItsMinimumOutOfTheOwnPanelsBudget) {
    useOwnPanelPlacement();
    MainComponent mc(std::make_unique<MockProviderTL>());
    showOwnPanel(mc);
    auto& own = mc.getMixerPlacementControllerForTest();

    mc.simulateToggleTimelineClick(); // open the dock (220 min)
    ASSERT_TRUE(mc.getBottomDock().isVisible());

    own.setOwnPanelHeight(5000, false);
    EXPECT_EQ(own.getOwnPanelHeight(), 455) << "675 - the dock's 220 minimum";
    EXPECT_EQ(mc.getBottomDock().getHeight(), 220) << "the dock gives way to its minimum, no further";

    // Closing the dock hands its reservation back: the stored wish (5000 -> clamped when stored)
    // does not silently shrink, but only what the window allows is laid out.
    mc.simulateToggleTimelineClick();
    EXPECT_LE(own.getHeight(), 675);
    EXPECT_EQ(own.getHeight(), own.getOwnPanelHeight());
}

// ---- Persistence ----

TEST_F(MixerOwnPanelTest, PersistedHeightIsHonouredAtConstructionAndReclampedAfterAShrink) {
    useOwnPanelPlacement();
    writeSetting(Controller::kOwnPanelHeightKey, 400);
    MainComponent mc(std::make_unique<MockProviderTL>());
    showOwnPanel(mc);
    auto& own = mc.getMixerPlacementControllerForTest();
    EXPECT_EQ(own.getOwnPanelHeight(), 400);
    EXPECT_EQ(own.getHeight(), 400);

    mc.setSize(1000, 400);
    EXPECT_EQ(own.getOwnPanelHeight(), 300) << "75% of the 400 px window";
    EXPECT_EQ(own.getHeight(), 300);
    EXPECT_GT(mc.getGraphEditor().getBounds().getHeight(), 0);

    mc.setSize(1600, 900);
    EXPECT_EQ(own.getHeight(), 400) << "a layout pass never rewrites the user's stored height";
}

TEST_F(MixerOwnPanelTest, ADragCommitIsReadBackByTheNextWindow) {
    useOwnPanelPlacement();
    {
        MainComponent mc(std::make_unique<MockProviderTL>());
        showOwnPanel(mc);
        auto& handle = mc.getMixerPlacementControllerForTest().getResizeHandle();
        handle.mouseDown(makeClickEvent(handle, {10.0f, 2.0f}));
        handle.mouseDrag(makeDragEvent(handle, {10.0f, -100.0f}, {10.0f, 2.0f}));
        handle.mouseUp(makeClickEvent(handle, {10.0f, -100.0f}));
    }
    MainComponent mc2(std::make_unique<MockProviderTL>());
    showOwnPanel(mc2);
    EXPECT_EQ(mc2.getMixerPlacementControllerForTest().getOwnPanelHeight(), 322);
}

// ---- Sharing the window with the dock ----

TEST_F(MixerOwnPanelTest, DockAndOwnPanelTogetherNeverExceedThreeQuartersOfTheWindow) {
    useOwnPanelPlacement();
    MainComponent mc(std::make_unique<MockProviderTL>());
    showOwnPanel(mc);
    auto& own = mc.getMixerPlacementControllerForTest();
    auto& dock = mc.getBottomDock();
    mc.simulateToggleTimelineClick();
    ASSERT_TRUE(dock.isVisible());

    for (const int windowHeight : {900, 700, 640}) {
        mc.setSize(1600, windowHeight);
        for (const int ownWish : {220, 400, 5000}) {
            for (const int dockWish : {220, 500, 5000}) {
                own.setOwnPanelHeight(ownWish, false);
                dock.onResizeHeight(dockWish);
                const int carves = own.getHeight() + dock.getHeight();
                EXPECT_LE(carves, std::max(440, windowHeight * 3 / 4))
                    << "window " << windowHeight << " own " << ownWish << " dock " << dockWish;
                EXPECT_GE(dock.getHeight(), 220);
                EXPECT_GE(own.getHeight(), 220);
                EXPECT_GE(windowHeight - carves, windowHeight / 4) << "the canvas keeps its quarter";
                EXPECT_GT(mc.getGraphEditor().getBounds().getHeight(), 0);
            }
        }
    }
}

TEST_F(MixerOwnPanelTest, TheOwnPanelSitsAtTheWindowsBottomEdgeUnderTheDock) {
    useOwnPanelPlacement();
    MainComponent mc(std::make_unique<MockProviderTL>());
    showOwnPanel(mc);
    mc.simulateToggleTimelineClick();
    auto& own = mc.getMixerPlacementControllerForTest();
    auto& dock = mc.getBottomDock();

    EXPECT_EQ(own.getBottom(), mc.getStatusBar().getBounds().getY());
    EXPECT_EQ(dock.getBottom(), own.getY()) << "the dock stacks directly above the Own panel";
    EXPECT_EQ(mc.getGraphEditor().getBounds().getBottom(), dock.getY());
}

TEST_F(MixerOwnPanelTest, TheDocksStoredHeightSurvivesGivingWayToTheOwnPanel) {
    useOwnPanelPlacement();
    writeSetting(MainComponent::kTimelinePanelHeightKey, 500);
    MainComponent mc(std::make_unique<MockProviderTL>());
    showOwnPanel(mc);
    mc.simulateToggleTimelineClick();
    ASSERT_EQ(mc.getBottomDock().getHeight(), 455) << "500 wished, 675 - the Own panel's 220";

    EXPECT_EQ(mc.getTimelinePanelHeight(), 500) << "the stored/persisted dock height is untouched";
    mc.performToggleMixerPanel(); // close the Own panel (headless: lands at once)
    EXPECT_EQ(mc.getBottomDock().getHeight(), 500) << "and comes back once the room does";
}
