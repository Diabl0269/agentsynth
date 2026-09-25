// TimelinePanelTests.cpp
//
// Bottom-docked timeline panel shell + toolbar toggle + rebindable shortcut + slide animation:
// synth::ui::TimelinePanelComponent in isolation (pure layout/paint) and MainComponent
// integration (toggle, persistence, carve geometry). Shared MockProviderTL and the
// TimelinePanelIntegrationTest fixture live in TimelinePanelTestFixture.h.

#include "AI/AIProvider.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AppUndoManager.h"
#include "MainComponent/MainComponent.h"
#include "ProjectBundle.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "TimelinePanelTestFixture.h"
#include "Transport/TransportService.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include "UI/Timeline/EdgeAutoScroll.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include "UI/Timeline/TrackColour.h"
#include "UserSettings.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

// ============================================================================
// 1. synth::ui::TimelinePanelComponent — pure layout/paint smoke tests.
// ============================================================================

TEST(TimelinePanelComponentTest, PanelRegionsTile) {
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1200, 220);

    const auto transport = panel.getTransportBarBounds();
    const auto trackHeader = panel.getTrackHeaderBounds();
    const auto lanes = panel.getLanesBounds();
    const auto full = panel.getLocalBounds();

    // Union (bounding box) covers the whole panel with no gaps at the edges.
    EXPECT_EQ(transport.getUnion(trackHeader).getUnion(lanes), full);

    // No overlap: the three regions' areas sum to exactly the panel's total area. Combined with
    // the union check above, this proves an exact tiling (three disjoint rects derived from
    // sequential removeFromTop/removeFromLeft calls, per resized()).
    const juce::int64 sumAreas = (juce::int64)transport.getWidth() * transport.getHeight() +
                                 (juce::int64)trackHeader.getWidth() * trackHeader.getHeight() +
                                 (juce::int64)lanes.getWidth() * lanes.getHeight();
    EXPECT_EQ(sumAreas, (juce::int64)full.getWidth() * full.getHeight());

    // Sanity on placement: transport is the top strip, trackHeader the left column of the
    // remainder, lanes the rest.
    EXPECT_EQ(transport.getY(), 0);
    EXPECT_EQ(trackHeader.getX(), 0);
    EXPECT_EQ(trackHeader.getY(), transport.getBottom());
    EXPECT_EQ(lanes.getX(), trackHeader.getRight());
    EXPECT_EQ(lanes.getY(), transport.getBottom());
    EXPECT_EQ(lanes.getRight(), full.getRight());
    EXPECT_EQ(lanes.getBottom(), full.getBottom());
}

TEST(TimelinePanelComponentTest, SnapshotSmoke) {
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1200, 220);

    const juce::Image img = panel.createComponentSnapshot(panel.getLocalBounds());
    EXPECT_FALSE(img.isNull());
    EXPECT_EQ(img.getWidth(), 1200);
    EXPECT_EQ(img.getHeight(), 220);
}

// ============================================================================
// 2. MainComponent integration.
// ============================================================================

TEST_F(TimelinePanelIntegrationTest, HiddenByDefaultAndCarvesNothing) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    mc.setSize(1600, 900);

    EXPECT_FALSE(mc.isBottomDockConfiguredVisible());
    // FRO11 (P9-5): timelinePanelIsOpen(), not isVisible() -- the panel now lives inside
    // BottomDockComponent (see its own comment), and its own visibility flag reflects only "the
    // Timeline tab is selected" (true by default), not "the dock is open".
    EXPECT_FALSE(timelinePanelIsOpen(mc));
    // No carve: the graph editor still reaches all the way down to the status bar.
    EXPECT_EQ(mc.getGraphEditor().getBounds().getBottom(), mc.getStatusBar().getBounds().getY());
}

TEST_F(TimelinePanelIntegrationTest, ToggleCarvesFullWidthAboveStatusBar) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    mc.setSize(1600, 900);

    const int libraryX = mc.getGraphEditor().getBounds().getX();
    const int graphRight = mc.getGraphEditor().getBounds().getRight();

    mc.simulateToggleTimelineClick();
    ASSERT_TRUE(mc.isBottomDockConfiguredVisible());
    ASSERT_TRUE(timelinePanelIsOpen(mc));

    // FRO11 (P9-5): MainComponent-relative bounds -- see timelinePanelBoundsInMainComponent's own
    // comment for why a raw mc.getTimelinePanel().getBounds() can no longer be compared directly
    // against the status bar / graph editor (different coordinate origins now that the panel is
    // nested inside BottomDockComponent).
    const auto panelBounds = timelinePanelBoundsInMainComponent(mc);
    EXPECT_EQ(panelBounds.getX(), 0);
    EXPECT_EQ(panelBounds.getWidth(), 1600);
    // Metrics::timelinePanelHeight literal default (220) minus BottomDockComponent's own 22px tab
    // strip -- the dock's TOTAL carve is still 220, but the tab strip now eats part of it.
    EXPECT_EQ(panelBounds.getHeight(), 198);
    // Sits directly above the status bar.
    EXPECT_EQ(panelBounds.getBottom(), mc.getStatusBar().getBounds().getY());

    // Graph editor shrunk by exactly the dock's total carve (220), NOT just the panel's own
    // (smaller) content height -- the graph editor sits above the WHOLE dock, tab strip included.
    EXPECT_EQ(mc.getGraphEditor().getBounds().getBottom(), panelBounds.getY() - 22);

    // Library/AI panels unaffected horizontally (default: library visible at 200px, AI hidden).
    EXPECT_EQ(mc.getGraphEditor().getBounds().getX(), libraryX);
    EXPECT_EQ(mc.getGraphEditor().getBounds().getRight(), graphRight);
}

TEST_F(TimelinePanelIntegrationTest, ToggleBackRestores) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    mc.setSize(1600, 900);
    const auto initialBounds = mc.getGraphEditor().getBounds();

    mc.simulateToggleTimelineClick();
    ASSERT_TRUE(mc.isBottomDockConfiguredVisible());

    mc.simulateToggleTimelineClick();
    EXPECT_FALSE(mc.isBottomDockConfiguredVisible());
    EXPECT_FALSE(timelinePanelIsOpen(mc)); // see HiddenByDefaultAndCarvesNothing's comment
    EXPECT_EQ(mc.getGraphEditor().getBounds(), initialBounds);
}

TEST_F(TimelinePanelIntegrationTest, VisibilityPersists) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    ASSERT_FALSE(mc.isBottomDockConfiguredVisible());

    mc.simulateToggleTimelineClick();
    ASSERT_TRUE(mc.isBottomDockConfiguredVisible());
    EXPECT_TRUE(mc.getAppPropertiesForTest().getUserSettings()->getBoolValue("bottomDockVisible", false));

    // A second MainComponent reads the same on-disk properties file — visible from startup.
    MainComponent mc2(std::make_unique<MockProviderTL>());
    EXPECT_TRUE(mc2.isBottomDockConfiguredVisible());
    EXPECT_TRUE(mc2.getTimelinePanel().isVisible());
}

// ----------------------------------------------------------------------------
// The timeline is GA (the old Preferences "Show timeline (experimental)" kill switch and its
// MainComponent::applyTimelineFeatureEnabled() plumbing are gone): the toolbar button, Cmd+T and
// Space must never be gated off again.
// ----------------------------------------------------------------------------

TEST_F(TimelinePanelIntegrationTest, ToggleTimelinePanelAndPlaybackCommandsAreAlwaysActive) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    mc.setSize(1600, 900);

    auto& cm = mc.getCommandManager();
    juce::ApplicationCommandInfo info(AppCommands::toggleTimelinePanel);
    mc.getCommandInfo(AppCommands::toggleTimelinePanel, info);
    EXPECT_EQ(info.flags & juce::ApplicationCommandInfo::isDisabled, 0) << "toggleTimelinePanel must always be active";
    // invokeDirectly() only proves perform() ran (returns true) — its actual effect is
    // toggleTimelineButton.triggerClick(), which POSTS a message and never dispatches in a
    // headless test (see PreferencesSettingsTabTests.cpp's ClickingTheToggleReachesTheEditorAnd
    // NewModules comment), so it is not asserted here. simulateToggleTimelineClick() (used by the
    // tests above) is the synchronous path for observing the panel's actual visibility.
    EXPECT_TRUE(cm.invokeDirectly(AppCommands::toggleTimelinePanel, false));

    juce::ApplicationCommandInfo playbackInfo(AppCommands::togglePlayback);
    mc.getCommandInfo(AppCommands::togglePlayback, playbackInfo);
    EXPECT_EQ(playbackInfo.flags & juce::ApplicationCommandInfo::isDisabled, 0)
        << "togglePlayback (Space) must always be active";
}
