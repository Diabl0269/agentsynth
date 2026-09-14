// TimelinePanelResizeTests.cpp
//
// The resizable panel height: the panel's top-edge grab strip (ungated) and MainComponent's
// ownership of the value — default from the theme metric, clamp, live relayout, persistence.

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
// 5. Resizable panel height — the panel's top-edge grab strip (ungated) and MainComponent's
//    ownership of the value (gated, like the rest of group 2).
// ============================================================================

TEST(TimelinePanelComponentTest, AddTrackButtonCarriesATooltip) {
    synth::ui::TimelinePanelComponent panel;
    EXPECT_EQ(panel.getAddTrackButton().getTooltip(), "Add a MIDI or Audio track");
}

TEST(TimelinePanelResizeTest, GrabStripCoversTheTopEdgeAndKeepsTheTransportControlsClear) {
    using Panel = synth::ui::TimelinePanelComponent;
    Panel panel;
    panel.setSize(1200, 220);

    auto& handle = panel.getResizeHandle();
    EXPECT_EQ(handle.getBounds(), juce::Rectangle<int>(0, 0, 1200, Panel::kResizeHandleHeight));
    EXPECT_TRUE(handle.getMouseCursor() == juce::MouseCursor::UpDownResizeCursor);

    // The strip is chrome ON the transport strip, not a fourth region: transportBarBounds_ still
    // starts at y == 0 (PanelRegionsTile's tiling holds), but the controls inside start below it,
    // so a resize grab can never land on a transport button.
    EXPECT_EQ(panel.getTransportBarBounds().getY(), 0);
    EXPECT_GE(panel.getTransportBar().getY(), Panel::kResizeHandleHeight);
    EXPECT_GE(panel.getSnapCombo().getY(), Panel::kResizeHandleHeight);
}

TEST(TimelinePanelResizeTest, HoverStateFlipsOnlyOnEnterAndExit) {
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1200, 220);
    auto& handle = panel.getResizeHandle();

    EXPECT_FALSE(panel.isResizeHandleHovered());
    handle.mouseEnter(makeClickEvent(handle, {10.0f, 2.0f}));
    EXPECT_TRUE(panel.isResizeHandleHovered());
    // A second enter is not a change — the strip repaints only when the state moves.
    handle.mouseEnter(makeClickEvent(handle, {40.0f, 3.0f}));
    EXPECT_TRUE(panel.isResizeHandleHovered());
    handle.mouseExit(makeClickEvent(handle, {40.0f, 3.0f}));
    EXPECT_FALSE(panel.isResizeHandleHovered());
}

// The panel reports a DESIRED height measured from its fixed bottom edge, unclamped — clamping and
// layout belong to the owner. Persistence is signalled once, on mouse-up.
TEST(TimelinePanelResizeTest, DraggingReportsTheHeightMeasuredFromTheFixedBottomEdge) {
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1200, 220);

    std::vector<int> live, committed;
    panel.onResizeHeight = [&live](int h) { live.push_back(h); };
    panel.onResizeHeightCommitted = [&committed](int h) { committed.push_back(h); };

    auto& handle = panel.getResizeHandle();
    // Grabbed 2 px into the strip, dragged 62 px UP: bottom edge pinned, so 220 + 62.
    handle.mouseDown(makeClickEvent(handle, {10.0f, 2.0f}));
    handle.mouseDrag(makeDragEvent(handle, {10.0f, -60.0f}, {10.0f, 2.0f}));
    ASSERT_EQ(live.size(), 1u);
    EXPECT_EQ(live.front(), 282);
    EXPECT_TRUE(committed.empty()) << "nothing is committed mid-drag";

    handle.mouseUp(makeClickEvent(handle, {10.0f, -60.0f}));
    ASSERT_EQ(committed.size(), 1u);
    EXPECT_EQ(committed.front(), 282);
    EXPECT_EQ(live.size(), 1u) << "mouse-up adds no extra layout step";

    // Downward drag shrinks it, and the reported value is NOT clamped to the panel's minimum.
    live.clear();
    committed.clear();
    handle.mouseDown(makeClickEvent(handle, {10.0f, 2.0f}));
    handle.mouseDrag(makeDragEvent(handle, {10.0f, 90.0f}, {10.0f, 2.0f}));
    ASSERT_EQ(live.size(), 1u);
    EXPECT_EQ(live.front(), 132); // 220 - 88
    handle.mouseUp(makeClickEvent(handle, {10.0f, 90.0f}));

    // A drag that never began on the strip reports nothing.
    live.clear();
    committed.clear();
    handle.mouseDrag(makeDragEvent(handle, {10.0f, -300.0f}, {10.0f, 2.0f}));
    handle.mouseUp(makeClickEvent(handle, {10.0f, -300.0f}));
    EXPECT_TRUE(live.empty());
    EXPECT_TRUE(committed.empty());

    // A click that never dragged commits nothing either — no settings write on a stray click.
    handle.mouseDown(makeClickEvent(handle, {10.0f, 2.0f}));
    handle.mouseUp(makeClickEvent(handle, {10.0f, 2.0f}));
    EXPECT_TRUE(live.empty());
    EXPECT_TRUE(committed.empty());
}

// Nothing inside the panel assumes the default height: at 2x, every extra pixel goes to the lanes.
TEST(TimelinePanelResizeTest, InternalLayoutHoldsAtDoubleHeight) {
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1200, 220);
    const int lanesAtDefault = panel.getLanesBounds().getHeight();

    panel.setSize(1200, 440);
    const auto transport = panel.getTransportBarBounds();
    const auto trackHeader = panel.getTrackHeaderBounds();
    const auto lanes = panel.getLanesBounds();
    EXPECT_EQ(transport.getUnion(trackHeader).getUnion(lanes), panel.getLocalBounds());
    EXPECT_EQ(transport.getHeight(), 34) << "the transport strip keeps its metric height";
    EXPECT_EQ(lanes.getHeight(), lanesAtDefault + 220) << "the extra height all goes to the lanes";
    EXPECT_EQ(panel.getResizeHandle().getWidth(), 1200);

    // The clip lanes still fill the lanes region below the 30 px ruler — the rect the grid is
    // painted into, so clips stay aligned with it at any height. (30, up from 24: the strip now
    // tiles a numbers row and a marker band — see Theme::Metrics::timelineRulerHeight.)
    EXPECT_EQ(panel.getClipLaneArea().getBounds(), lanes.withTrimmedTop(30));
    // The ROLL shares the same rect, except for how much of the ruler band it claims: it owns the
    // vertical run above the ruler when its chip toolbar is showing, so that top edge belongs to
    // PianoRollComponent's own layout and is asserted in its own tests. What must hold HERE is that
    // the roll tracks the lanes REGION at any panel height — full width, flush bottom, top inside.
    const auto roll = panel.getPianoRoll().getBounds();
    EXPECT_EQ(roll.getX(), lanes.getX());
    EXPECT_EQ(roll.getWidth(), lanes.getWidth());
    EXPECT_EQ(roll.getBottom(), lanes.getBottom());
    EXPECT_GE(roll.getY(), lanes.getY());
    EXPECT_LE(roll.getY(), lanes.getY() + 30);
    // The header list fills the taller viewport even with no tracks (no gap under the last row).
    ASSERT_NE(panel.getTrackHeaderViewport().getViewedComponent(), nullptr);
    EXPECT_GE(panel.getTrackHeaderViewport().getViewedComponent()->getHeight(),
              panel.getTrackHeaderViewport().getMaximumVisibleHeight());

    const juce::Image img = panel.createComponentSnapshot(panel.getLocalBounds());
    EXPECT_FALSE(img.isNull());
    EXPECT_EQ(img.getHeight(), 440);
}

namespace {
// Leaves a height in the shared settings file the way an earlier session would have — the same file
// TimelinePanelIntegrationTest::resetPanelKeys() clears the key from.
void persistTimelinePanelHeight(int height) {
    juce::ApplicationProperties props;
    props.setStorageParameters(synth::userSettingsOptions());
    if (auto* s = props.getUserSettings()) {
        s->setValue(MainComponent::kTimelinePanelHeightKey, height);
        s->saveIfNeeded();
    }
}

int readPersistedTimelinePanelHeight(MainComponent& mc) {
    return mc.getAppPropertiesForTest().getUserSettings()->getIntValue(MainComponent::kTimelinePanelHeightKey, -1);
}
} // namespace

TEST_F(TimelinePanelIntegrationTest, AbsentSettingFallsBackToTheThemeMetric) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    mc.setSize(1600, 900);
    mc.simulateToggleTimelineClick();

    EXPECT_EQ(mc.getTimelinePanelHeight(), 220); // Metrics::timelinePanelHeight literal default
    EXPECT_EQ(mc.getTimelinePanel().getBounds().getHeight(), 220);
    EXPECT_EQ(readPersistedTimelinePanelHeight(mc), -1) << "showing the panel writes no height";
}

TEST_F(TimelinePanelIntegrationTest, PersistedHeightIsHonouredAtStartup) {
    persistTimelinePanelHeight(400);

    MainComponent mc(std::make_unique<MockProviderTL>());
    mc.setSize(1600, 900);
    mc.simulateToggleTimelineClick();
    ASSERT_TRUE(mc.getTimelinePanel().isVisible());

    const auto panelBounds = mc.getTimelinePanel().getBounds();
    EXPECT_EQ(mc.getTimelinePanelHeight(), 400);
    EXPECT_EQ(panelBounds.getHeight(), 400);
    EXPECT_EQ(panelBounds.getBottom(), mc.getStatusBar().getBounds().getY());
    EXPECT_EQ(mc.getGraphEditor().getBounds().getBottom(), panelBounds.getY());
}

TEST_F(TimelinePanelIntegrationTest, DraggingTheGrabStripResizesLiveAndPersistsOnDragEnd) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    mc.setSize(1600, 900);
    mc.simulateToggleTimelineClick();

    auto& panel = mc.getTimelinePanel();
    ASSERT_EQ(panel.getBounds().getHeight(), 220);

    auto& handle = panel.getResizeHandle();
    handle.mouseDown(makeClickEvent(handle, {10.0f, 2.0f}));
    // 142 px above the grab point, against a pinned bottom edge: 220 + 142.
    handle.mouseDrag(makeDragEvent(handle, {10.0f, -140.0f}, {10.0f, 2.0f}));

    // LIVE: the owner already re-laid out, before any mouse-up.
    EXPECT_EQ(mc.getTimelinePanelHeight(), 362);
    EXPECT_EQ(panel.getBounds().getHeight(), 362);
    EXPECT_EQ(panel.getBounds().getBottom(), mc.getStatusBar().getBounds().getY());
    EXPECT_EQ(mc.getGraphEditor().getBounds().getBottom(), panel.getBounds().getY());
    EXPECT_EQ(readPersistedTimelinePanelHeight(mc), -1) << "not persisted per pixel";

    handle.mouseUp(makeClickEvent(handle, {10.0f, -140.0f}));
    EXPECT_EQ(readPersistedTimelinePanelHeight(mc), 362);

    // A second component reads the same file back — and shows the panel at that height.
    MainComponent mc2(std::make_unique<MockProviderTL>());
    mc2.setSize(1600, 900);
    EXPECT_EQ(mc2.getTimelinePanelHeight(), 362);
}

TEST_F(TimelinePanelIntegrationTest, HeightIsClampedToTheMetricFloorAndThreeQuartersOfTheWindow) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    mc.setSize(1600, 900);
    mc.simulateToggleTimelineClick();

    auto& panel = mc.getTimelinePanel();
    ASSERT_TRUE(panel.onResizeHeight != nullptr);

    panel.onResizeHeight(5000);
    EXPECT_EQ(mc.getTimelinePanelHeight(), 675) << "75% of the 900 px window";
    EXPECT_EQ(panel.getBounds().getHeight(), 675);
    EXPECT_GT(mc.getGraphEditor().getBounds().getHeight(), 0);

    panel.onResizeHeight(10);
    EXPECT_EQ(mc.getTimelinePanelHeight(), 220) << "the theme metric is the floor";
    EXPECT_EQ(panel.getBounds().getHeight(), 220);

    EXPECT_EQ(readPersistedTimelinePanelHeight(mc), -1) << "only the drag-end callback persists";
}

TEST_F(TimelinePanelIntegrationTest, ASmallerWindowReclampsTheHeightSoTheCanvasSurvives) {
    persistTimelinePanelHeight(600);

    MainComponent mc(std::make_unique<MockProviderTL>());
    mc.setSize(1600, 900);
    mc.simulateToggleTimelineClick();
    ASSERT_EQ(mc.getTimelinePanelHeight(), 600); // within 75% of 900

    mc.setSize(1000, 400);
    EXPECT_EQ(mc.getTimelinePanelHeight(), 300) << "75% of the 400 px window";
    EXPECT_EQ(mc.getTimelinePanel().getBounds().getHeight(), 300);
    EXPECT_GT(mc.getGraphEditor().getBounds().getHeight(), 0);
    EXPECT_EQ(mc.getTimelinePanel().getBounds().getBottom(), mc.getStatusBar().getBounds().getY());

    // Shorter than 4/3 of the floor (below the enforced minWindowHeight, so a corner case only):
    // the floor wins rather than the cap.
    mc.setSize(1000, 280);
    EXPECT_EQ(mc.getTimelinePanelHeight(), 220);
}

TEST_F(TimelinePanelIntegrationTest, HidingThePanelReturnsTheCanvasAndReshowingKeepsTheDraggedHeight) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    mc.setSize(1600, 900);
    const auto canvasWithNoPanel = mc.getGraphEditor().getBounds();

    mc.simulateToggleTimelineClick();
    mc.getTimelinePanel().onResizeHeight(420);
    ASSERT_EQ(mc.getTimelinePanel().getBounds().getHeight(), 420);

    mc.simulateToggleTimelineClick(); // hide
    EXPECT_FALSE(mc.getTimelinePanel().isVisible());
    EXPECT_EQ(mc.getGraphEditor().getBounds(), canvasWithNoPanel) << "a hidden panel carves nothing, at any height";
    EXPECT_EQ(mc.getTimelinePanelHeight(), 420) << "the height outlives a hide";

    mc.simulateToggleTimelineClick(); // show again
    EXPECT_EQ(mc.getTimelinePanel().getBounds().getHeight(), 420);
    EXPECT_EQ(mc.getTimelinePanel().getBounds().getBottom(), mc.getStatusBar().getBounds().getY());
}
