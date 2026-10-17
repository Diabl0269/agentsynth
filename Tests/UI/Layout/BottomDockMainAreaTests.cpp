// BottomDockMainAreaTests.cpp -- the Timeline or the MIDI Remote panel in the main area: its host moved into
// MainComponent's holder at the canvas's rect, on screen with the bottom panel closed (edit commands, the
// playhead gate, focus regions), and back in the dock when the canvas returns (docs/layout/chrome.md#the-main-view).
// Moved up through MainComponent::setMainViewForTest; the swap icon's own path is BottomDockSwapIconTests.cpp.
#include "../../TestSettingsHelpers.h"
#include "BottomDockMainAreaFixture.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Layout/BottomDockComponent/BottomDockComponent.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <vector>

namespace {

using synth::ui::BottomDockComponent;
using Tab = BottomDockComponent::Tab;

} // namespace

TEST_F(BottomDockMainAreaTest, TheTimelineHostFillsTheRectTheCanvasHad) {
    // The main area is the canvas plus its toolbar row.
    const auto mainRect = mc_->getGraphEditor().getBounds().getUnion(mc_->getCanvasToolbar().getBounds());
    const auto library = regionBounds("library");
    const auto aiPanel = regionBounds("aiPanel");

    ASSERT_TRUE(mc_->setMainViewForTest(Tab::Timeline));
    auto& host = dock().getTimelineHost();
    EXPECT_EQ(host.getParentComponent(), &holder());
    EXPECT_TRUE(holder().isVisible());
    EXPECT_EQ(holder().getBounds(), mainRect);
    EXPECT_EQ(host.getBounds(), holder().getLocalBounds());
    EXPECT_TRUE(host.isVisible());
    EXPECT_TRUE(timeline().isVisible());
    EXPECT_TRUE(dock().isInMainArea(Tab::Timeline));
    EXPECT_FALSE(mc_->getGraphEditor().isVisible()) << "the canvas waits in the closed panel";
    EXPECT_EQ(regionBounds("library"), library) << "the library stays beside the main area";
    EXPECT_EQ(regionBounds("aiPanel"), aiPanel) << "and so does the AI panel";
}

TEST_F(BottomDockMainAreaTest, TheTimelineInMainIsShowingWithThePanelClosed) {
    ASSERT_TRUE(mc_->setMainViewForTest(Tab::Timeline));
    ASSERT_FALSE(mc_->isBottomDockConfiguredVisible());

    EXPECT_TRUE(dock().isTimelineShowing()) << "the playhead poll runs";
    EXPECT_TRUE(regionOpen("timeline"));
    EXPECT_FALSE(regionOpen("dockTabs"));
    EXPECT_TRUE(commandActive(AppCommands::snapSetQuarter)) << "the snap grid keys work";
    EXPECT_TRUE(commandActive(AppCommands::selectNextTrack));

    mc_->setFocusedComponentOverrideForTest(&timeline().getPianoRoll());
    EXPECT_EQ(mc_->resolveEditSurface(), MainComponent::EditSurface::PianoRoll);
    mc_->setFocusedComponentOverrideForTest(&timeline().getClipLaneArea());
    EXPECT_EQ(mc_->resolveEditSurface(), MainComponent::EditSurface::TimelineClips);
}

TEST_F(BottomDockMainAreaTest, TheTimelineInTheDockStillNeedsTheDockOpen) {
    ASSERT_FALSE(mc_->isBottomDockConfiguredVisible());
    EXPECT_FALSE(dock().isTimelineShowing());
    EXPECT_FALSE(regionOpen("timeline"));
    mc_->setFocusedComponentOverrideForTest(&timeline().getClipLaneArea());
    EXPECT_EQ(mc_->resolveEditSurface(), MainComponent::EditSurface::Graph) << "a hidden panel owns no verbs";
}

TEST_F(BottomDockMainAreaTest, OpeningOrClosingThePanelLeavesTheMainViewAlone) {
    ASSERT_TRUE(mc_->setMainViewForTest(Tab::Timeline));
    mc_->ensureCanvasShowing();
    ASSERT_TRUE(mc_->isBottomDockConfiguredVisible());
    dock().setActiveTab(Tab::Mixer);
    EXPECT_TRUE(timeline().isVisible()) << "a dock tab switch never hides the view in main";
    EXPECT_TRUE(dock().getTimelineHost().isVisible());

    mc_->simulateToggleBottomPanelClick();
    ASSERT_FALSE(mc_->isBottomDockConfiguredVisible());
    EXPECT_TRUE(dock().isTimelineShowing());
    EXPECT_EQ(dock().getTimelineHost().getParentComponent(), &holder());
}

TEST_F(BottomDockMainAreaTest, RevealingTheViewInMainNeverOpensThePanel) {
    ASSERT_TRUE(mc_->setMainViewForTest(Tab::Timeline));
    const auto tab = dock().getActiveTab();
    std::vector<juce::String> focused;
    mc_->getFocusRegionsForTest().onRegionFocused = [&](const juce::String& id) { focused.push_back(id); };

    mc_->showBottomDockTabFromShortcut(Tab::Timeline);
    EXPECT_EQ(focused, std::vector<juce::String>{"timeline"}) << "Cmd+1 focuses the Timeline in main";
    mc_->showBottomDockTab(Tab::Timeline);
    EXPECT_TRUE(mc_->getFocusRegionsForTest().focusRegionById("timeline"));
    mc_->ensureDockOpenFor(Tab::Timeline);
    dock().setActiveTab(Tab::Timeline);

    EXPECT_FALSE(mc_->isBottomDockConfiguredVisible());
    EXPECT_EQ(dock().getActiveTab(), tab) << "the main view is never the active tab";
    mc_->ensureDockOpenFor(Tab::Mixer);
    EXPECT_TRUE(mc_->isBottomDockConfiguredVisible()) << "a view in the dock still opens it";
}

TEST_F(BottomDockMainAreaTest, TheCanvasComingBackReturnsTheHostToTheDock) {
    ASSERT_TRUE(mc_->setMainViewForTest(Tab::Timeline));
    ASSERT_TRUE(mc_->setMainViewForTest(Tab::Canvas));

    auto& host = dock().getTimelineHost();
    EXPECT_EQ(host.getParentComponent(), &dock());
    EXPECT_FALSE(dock().isInMainArea(Tab::Timeline));
    EXPECT_FALSE(holder().isVisible());
    EXPECT_TRUE(mc_->getGraphEditor().isVisible());

    mc_->showBottomDockTab(Tab::Timeline);
    EXPECT_EQ(dock().getActiveTab(), Tab::Timeline);
    EXPECT_TRUE(host.isVisible());
    EXPECT_TRUE(timeline().isVisible());
    EXPECT_EQ(host.getBounds(), dock().getLocalBounds().withTrimmedTop(BottomDockComponent::kTabStripHeight));
}

TEST_F(BottomDockMainAreaTest, AnotherViewTakingMainSendsTheTimelineBackToTheDock) {
    ASSERT_TRUE(mc_->setMainViewForTest(Tab::Timeline));
    ASSERT_TRUE(mc_->setMainViewForTest(Tab::MidiRemote));
    EXPECT_EQ(dock().getTimelineHost().getParentComponent(), &dock());
    EXPECT_EQ(dock().getMidiRemoteHost().getParentComponent(), &holder());
    EXPECT_FALSE(timeline().isVisible()) << "back in the dock as a tab that is not active";
}

TEST_F(BottomDockMainAreaTest, TheMidiRemotePanelInMainIsShowingWithThePanelClosed) {
    ASSERT_TRUE(mc_->setMainViewForTest(Tab::MidiRemote));
    ASSERT_FALSE(mc_->isBottomDockConfiguredVisible());

    EXPECT_EQ(dock().getMidiRemoteHost().getParentComponent(), &holder());
    EXPECT_TRUE(dock().getMidiRemotePanel().isVisible());
    EXPECT_TRUE(dock().isMidiRemoteShowing()) << "its activity tick runs";
    EXPECT_TRUE(regionOpen("midiRemote"));
    EXPECT_EQ(mc_->getMainViewFocusRegionId(), "midiRemote");

    ASSERT_TRUE(mc_->setMainViewForTest(Tab::Canvas));
    EXPECT_EQ(dock().getMidiRemoteHost().getParentComponent(), &dock());
    EXPECT_FALSE(dock().isMidiRemoteShowing());
}

TEST_F(BottomDockMainAreaTest, AViewInItsOwnWindowIsRefused) {
    dock().getTimelineHost().setDetached(true);
    EXPECT_FALSE(mc_->setMainViewForTest(Tab::Timeline));
    EXPECT_EQ(dock().getMainView(), Tab::Canvas);
    EXPECT_FALSE(holder().isVisible());
    dock().getTimelineHost().setDetached(false);
    EXPECT_TRUE(mc_->setMainViewForTest(Tab::Timeline));
}

// A saved Timeline main view opens with the Timeline in the main area and the canvas in the dock.
TEST_F(BottomDockMainAreaTest, ASavedTimelineMainViewOpensWithTheTimelineInMain) {
    mc_.reset();
    {
        juce::ApplicationProperties props;
        props.setStorageParameters(synth::test::userSettingsTestOptions());
        props.getUserSettings()->setValue("mainView", "timeline");
        props.getUserSettings()->saveIfNeeded();
    }
    build();

    EXPECT_EQ(dock().getMainView(), Tab::Timeline);
    EXPECT_EQ(dock().getTimelineHost().getParentComponent(), &holder());
    EXPECT_TRUE(holder().isVisible());
    EXPECT_FALSE(holder().getBounds().isEmpty());
    EXPECT_EQ(dock().getTimelineHost().getBounds(), holder().getLocalBounds());
}
