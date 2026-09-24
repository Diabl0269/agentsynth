// MixerDockResizeTests.cpp -- FRO231: the bottom dock resizes from ONE top-edge handle on every
// tab (Timeline, Mixer, MIDI Remote), and MainComponent owns the value the drag reports: default
// from the theme metric, clamp, live relayout, persistence on drag end.

#include "../Timeline/TimelinePanel/TimelinePanelTestFixture.h"
#include "MixerDockActiveTabResetGuard.h"
#include "UI/Layout/PanelResizeHandle.h"
#include "UI/Mixer/MixerDockComponent.h"
#include "UserSettings.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace {

using Dock = synth::ui::MixerDockComponent;
using Handle = synth::ui::PanelResizeHandle;

// Leaves a height in the shared settings file the way an earlier session would have.
void persistDockHeight(int height) {
    juce::ApplicationProperties props;
    props.setStorageParameters(synth::userSettingsOptions());
    if (auto* s = props.getUserSettings()) {
        s->setValue(MainComponent::kTimelinePanelHeightKey, height);
        s->saveIfNeeded();
    }
}

int readPersistedDockHeight(MainComponent& mc) {
    return mc.getAppPropertiesForTest().getUserSettings()->getIntValue(MainComponent::kTimelinePanelHeightKey, -1);
}

// Timeline-panel fixture (clears the height key) plus the dock-tab reset guard, since these tests
// switch the persisted active tab.
class MixerDockResizeTest : public TimelinePanelIntegrationTest {
protected:
    MixerDockActiveTabResetGuardMDT tabGuard_;
};

class MixerDockResizeOnEveryTabTest
    : public MixerDockResizeTest
    , public ::testing::WithParamInterface<Dock::Tab> {
protected:
    // An open dock at the default height, on the tab under test.
    void openDockOnTab(MainComponent& mc) {
        mc.setSize(1600, 900);
        mc.simulateToggleTimelineClick();
        mc.getMixerDock().setActiveTab(GetParam());
        ASSERT_EQ(mc.getMixerDock().getActiveTab(), GetParam());
        ASSERT_EQ(mc.getMixerDock().getHeight(), 220);
    }
};

std::string tabName(const ::testing::TestParamInfo<Dock::Tab>& info) {
    switch (info.param) {
    case Dock::Tab::Timeline:
        return "Timeline";
    case Dock::Tab::Mixer:
        return "Mixer";
    case Dock::Tab::MidiRemote:
        return "MidiRemote";
    }
    return "Unknown";
}

} // namespace

INSTANTIATE_TEST_SUITE_P(EveryTab, MixerDockResizeOnEveryTabTest,
                         ::testing::Values(Dock::Tab::Timeline, Dock::Tab::Mixer, Dock::Tab::MidiRemote), tabName);

TEST_P(MixerDockResizeOnEveryTabTest, HandleCoversTheDockTopEdgeAndKeepsTheTabButtonsClear) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    openDockOnTab(mc);
    auto& dock = mc.getMixerDock();

    auto& handle = dock.getResizeHandle();
    EXPECT_EQ(handle.getBounds(), juce::Rectangle<int>(0, 0, dock.getWidth(), Handle::kHeight));
    EXPECT_TRUE(handle.getMouseCursor() == juce::MouseCursor::UpDownResizeCursor);

    // The strip is chrome ON the tab strip, not extra height: the content below still starts at
    // kTabStripHeight, but no button reaches into the top kHeight px, so a grab never lands on one.
    for (auto* button : dock.getTabButtons())
        EXPECT_GE(button->getY(), Handle::kHeight) << button->getName();
    // "+ Bus" / "Reset Meters" are carved (and so positioned) only on the Mixer tab.
    for (auto* button : {&dock.getAddBusButtonForTest(), &dock.getResetMetersButtonForTest()})
        if (button->isVisible())
            EXPECT_GE(button->getY(), Handle::kHeight) << button->getName();
    EXPECT_EQ(dock.getTimelineHost().getY(), Dock::kTabStripHeight);
    EXPECT_EQ(dock.getMixerHost().getY(), Dock::kTabStripHeight);
    EXPECT_EQ(dock.getMidiRemoteHost().getY(), Dock::kTabStripHeight);
}

TEST_P(MixerDockResizeOnEveryTabTest, HandleWinsTheHitTestAtTheTopEdgeOverEveryTabButton) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    openDockOnTab(mc);
    auto& dock = mc.getMixerDock();
    auto& handle = dock.getResizeHandle();

    EXPECT_EQ(dock.getComponentAt(10, 2), &handle);
    EXPECT_EQ(dock.getComponentAt(dock.getWidth() - 3, 4), &handle) << "right over the detach button's column";
    for (auto* button : dock.getTabButtons()) {
        if (!button->isVisible())
            continue;
        EXPECT_EQ(dock.getComponentAt(button->getBounds().getCentreX(), 2), &handle) << button->getName();
        // ...while the rest of the button below the strip is still the button.
        EXPECT_EQ(dock.getComponentAt(button->getBounds().getCentre()), button) << button->getName();
    }
}

TEST_P(MixerDockResizeOnEveryTabTest, HoverStateFlipsOnlyOnEnterAndExit) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    openDockOnTab(mc);
    auto& dock = mc.getMixerDock();
    auto& handle = dock.getResizeHandle();

    EXPECT_FALSE(dock.isResizeHandleHovered());
    handle.mouseEnter(makeClickEvent(handle, {10.0f, 2.0f}));
    EXPECT_TRUE(dock.isResizeHandleHovered());
    handle.mouseEnter(makeClickEvent(handle, {40.0f, 3.0f})); // a second enter is not a change
    EXPECT_TRUE(dock.isResizeHandleHovered());
    handle.mouseExit(makeClickEvent(handle, {40.0f, 3.0f}));
    EXPECT_FALSE(dock.isResizeHandleHovered());
}

// The dock reports a DESIRED TOTAL height measured from its fixed bottom edge, unclamped -- no
// tab-strip translation, clamping and layout belong to the owner; commit fires once, on mouse-up.
TEST_P(MixerDockResizeOnEveryTabTest, DragReportsTheTotalDockHeightAndCommitsOnlyOnMouseUpAfterMoving) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    openDockOnTab(mc);
    auto& dock = mc.getMixerDock();

    std::vector<int> live, committed;
    dock.onResizeHeight = [&live](int h) { live.push_back(h); };
    dock.onResizeHeightCommitted = [&committed](int h) { committed.push_back(h); };

    auto& handle = dock.getResizeHandle();
    // Grabbed 2 px into the strip, dragged 62 px UP: bottom edge pinned, so 220 + 62.
    handle.mouseDown(makeClickEvent(handle, {10.0f, 2.0f}));
    handle.mouseDrag(makeDragEvent(handle, {10.0f, -60.0f}, {10.0f, 2.0f}));
    handle.mouseDrag(makeDragEvent(handle, {10.0f, -80.0f}, {10.0f, 2.0f}));
    ASSERT_EQ(live.size(), 2u) << "the live callback fires on every drag step";
    EXPECT_EQ(live[0], 282);
    EXPECT_EQ(live[1], 302);
    EXPECT_TRUE(committed.empty()) << "nothing is committed mid-drag";

    handle.mouseUp(makeClickEvent(handle, {10.0f, -80.0f}));
    ASSERT_EQ(committed.size(), 1u);
    EXPECT_EQ(committed.front(), 302);
    EXPECT_EQ(live.size(), 2u) << "mouse-up adds no extra layout step";

    // A downward drag shrinks it, and the reported value is NOT clamped to the dock's minimum.
    live.clear();
    committed.clear();
    handle.mouseDown(makeClickEvent(handle, {10.0f, 2.0f}));
    handle.mouseDrag(makeDragEvent(handle, {10.0f, 90.0f}, {10.0f, 2.0f}));
    ASSERT_EQ(live.size(), 1u);
    EXPECT_EQ(live.front(), 132); // 220 - 88
    handle.mouseUp(makeClickEvent(handle, {10.0f, 90.0f}));

    // A stray click that never dragged commits nothing: no settings write on a click.
    live.clear();
    committed.clear();
    handle.mouseDown(makeClickEvent(handle, {10.0f, 2.0f}));
    handle.mouseUp(makeClickEvent(handle, {10.0f, 2.0f}));
    EXPECT_TRUE(live.empty());
    EXPECT_TRUE(committed.empty());
}

TEST_P(MixerDockResizeOnEveryTabTest, DraggingResizesLiveAndPersistsOnDragEnd) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    openDockOnTab(mc);
    auto& dock = mc.getMixerDock();
    auto& handle = dock.getResizeHandle();

    handle.mouseDown(makeClickEvent(handle, {10.0f, 2.0f}));
    // 142 px above the grab point, against a pinned bottom edge: 220 + 142.
    handle.mouseDrag(makeDragEvent(handle, {10.0f, -140.0f}, {10.0f, 2.0f}));

    // LIVE: the owner already re-laid out, before any mouse-up.
    EXPECT_EQ(mc.getTimelinePanelHeight(), 362);
    EXPECT_EQ(dock.getHeight(), 362);
    EXPECT_EQ(dock.getBottom(), mc.getStatusBar().getBounds().getY());
    EXPECT_EQ(mc.getGraphEditor().getBounds().getBottom(), dock.getY());
    EXPECT_EQ(readPersistedDockHeight(mc), -1) << "not persisted per pixel";
    EXPECT_EQ(handle.getBounds(), juce::Rectangle<int>(0, 0, dock.getWidth(), Handle::kHeight))
        << "the handle rides the dock's top edge";

    handle.mouseUp(makeClickEvent(handle, {10.0f, -140.0f}));
    EXPECT_EQ(readPersistedDockHeight(mc), 362);

    // A second component reads the same file back.
    MainComponent mc2(std::make_unique<MockProviderTL>());
    mc2.setSize(1600, 900);
    EXPECT_EQ(mc2.getTimelinePanelHeight(), 362);
}

TEST_P(MixerDockResizeOnEveryTabTest, AStrayClickOnTheHandleNeverPersists) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    openDockOnTab(mc);
    auto& handle = mc.getMixerDock().getResizeHandle();

    handle.mouseDown(makeClickEvent(handle, {10.0f, 2.0f}));
    handle.mouseUp(makeClickEvent(handle, {10.0f, 2.0f}));
    EXPECT_EQ(mc.getTimelinePanelHeight(), 220);
    EXPECT_EQ(readPersistedDockHeight(mc), -1);
}

// The height belongs to the dock, not to whichever panel is showing: resizing on one tab carries
// to every other one.
TEST_F(MixerDockResizeTest, AHeightDraggedOnOneTabHoldsWhenSwitchingTabs) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    mc.setSize(1600, 900);
    mc.simulateToggleTimelineClick();
    auto& dock = mc.getMixerDock();

    dock.setActiveTab(Dock::Tab::Mixer);
    auto& handle = dock.getResizeHandle();
    handle.mouseDown(makeClickEvent(handle, {10.0f, 2.0f}));
    handle.mouseDrag(makeDragEvent(handle, {10.0f, -100.0f}, {10.0f, 2.0f}));
    handle.mouseUp(makeClickEvent(handle, {10.0f, -100.0f}));
    ASSERT_EQ(mc.getTimelinePanelHeight(), 322);

    for (auto tab : {Dock::Tab::Timeline, Dock::Tab::MidiRemote, Dock::Tab::Mixer}) {
        dock.setActiveTab(tab);
        EXPECT_EQ(dock.getHeight(), 322);
    }
}

// The handle belongs to the dock, so a detached Timeline (its panel now lives in its own window)
// no longer takes the resize gesture with it: the dock stays resizable, and the panel has no
// handle of its own left to drag.
TEST_F(MixerDockResizeTest, ADetachedTimelineStillLeavesTheDockResizable) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    mc.setSize(1600, 900);
    mc.simulateToggleTimelineClick();
    auto& dock = mc.getMixerDock();

    dock.getTimelineHost().setDetached(true);
    ASSERT_TRUE(dock.getTimelineHost().isDetached());
    EXPECT_EQ(mc.getTimelinePanel().findChildWithID("timelineResizeHandle"), nullptr);

    auto& handle = dock.getResizeHandle();
    handle.mouseDown(makeClickEvent(handle, {10.0f, 2.0f}));
    handle.mouseDrag(makeDragEvent(handle, {10.0f, -80.0f}, {10.0f, 2.0f}));
    EXPECT_EQ(mc.getTimelinePanelHeight(), 302);
    handle.mouseUp(makeClickEvent(handle, {10.0f, -80.0f}));
    EXPECT_EQ(readPersistedDockHeight(mc), 302);

    dock.getTimelineHost().setDetached(false);
}

// The transport controls now use their whole strip: nothing inside the Timeline panel gives up
// rows to a handle any more.
TEST_F(MixerDockResizeTest, TheTimelinePanelHasNoHandleOfItsOwnAndItsTransportBarUsesTheFullStrip) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    mc.setSize(1600, 900);
    mc.simulateToggleTimelineClick();
    auto& panel = mc.getTimelinePanel();

    EXPECT_EQ(panel.findChildWithID("timelineResizeHandle"), nullptr);
    EXPECT_EQ(panel.getTransportBarBounds().getY(), 0);
    EXPECT_EQ(panel.getTransportBar().getY(), 0);
    EXPECT_EQ(panel.getTransportBar().getHeight(), panel.getTransportBarBounds().getHeight());
    EXPECT_EQ(panel.getSnapCombo().getY(), 2) << "the snap combo's own 2 px inset, nothing more";
}

// ---- MainComponent owns the height the dock reports ----

TEST_F(MixerDockResizeTest, AbsentSettingFallsBackToTheThemeMetric) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    mc.setSize(1600, 900);
    mc.simulateToggleTimelineClick();

    EXPECT_EQ(mc.getTimelinePanelHeight(), 220); // Metrics::timelinePanelHeight literal default
    EXPECT_EQ(mc.getMixerDock().getHeight(), 220);
    // The panel's own local height is the dock's total carve minus the tab strip.
    EXPECT_EQ(mc.getTimelinePanel().getBounds().getHeight(), 220 - Dock::kTabStripHeight);
    EXPECT_EQ(readPersistedDockHeight(mc), -1) << "showing the panel writes no height";
}

TEST_F(MixerDockResizeTest, PersistedHeightIsHonouredAtStartup) {
    persistDockHeight(400);

    MainComponent mc(std::make_unique<MockProviderTL>());
    mc.setSize(1600, 900);
    mc.simulateToggleTimelineClick();
    ASSERT_TRUE(timelinePanelIsOpen(mc));

    const auto& dock = mc.getMixerDock();
    EXPECT_EQ(mc.getTimelinePanelHeight(), 400);
    EXPECT_EQ(dock.getHeight(), 400);
    EXPECT_EQ(dock.getBottom(), mc.getStatusBar().getBounds().getY());
    EXPECT_EQ(mc.getGraphEditor().getBounds().getBottom(), dock.getY());
}

TEST_F(MixerDockResizeTest, HeightIsClampedToTheMetricFloorAndThreeQuartersOfTheWindow) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    mc.setSize(1600, 900);
    mc.simulateToggleTimelineClick();

    auto& dock = mc.getMixerDock();
    ASSERT_TRUE(dock.onResizeHeight != nullptr);

    dock.onResizeHeight(5000);
    EXPECT_EQ(mc.getTimelinePanelHeight(), 675) << "75% of the 900 px window";
    EXPECT_EQ(dock.getHeight(), 675);
    EXPECT_GT(mc.getGraphEditor().getBounds().getHeight(), 0);

    dock.onResizeHeight(10);
    EXPECT_EQ(mc.getTimelinePanelHeight(), 220) << "the theme metric is the floor";
    EXPECT_EQ(dock.getHeight(), 220);

    EXPECT_EQ(readPersistedDockHeight(mc), -1) << "only the drag-end callback persists";

    dock.onResizeHeightCommitted(5000);
    EXPECT_EQ(readPersistedDockHeight(mc), 675) << "the committed value is the clamped one";
}

TEST_F(MixerDockResizeTest, ASmallerWindowReclampsTheHeightSoTheCanvasSurvives) {
    persistDockHeight(600);

    MainComponent mc(std::make_unique<MockProviderTL>());
    mc.setSize(1600, 900);
    mc.simulateToggleTimelineClick();
    ASSERT_EQ(mc.getTimelinePanelHeight(), 600); // within 75% of 900

    mc.setSize(1000, 400);
    EXPECT_EQ(mc.getTimelinePanelHeight(), 300) << "75% of the 400 px window";
    EXPECT_EQ(mc.getMixerDock().getHeight(), 300);
    EXPECT_GT(mc.getGraphEditor().getBounds().getHeight(), 0);
    EXPECT_EQ(mc.getMixerDock().getBottom(), mc.getStatusBar().getBounds().getY());

    // Shorter than 4/3 of the floor (below the enforced minWindowHeight, so a corner case only):
    // the floor wins rather than the cap.
    mc.setSize(1000, 280);
    EXPECT_EQ(mc.getTimelinePanelHeight(), 220);
}

TEST_F(MixerDockResizeTest, HidingTheDockReturnsTheCanvasAndReshowingKeepsTheDraggedHeight) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    mc.setSize(1600, 900);
    const auto canvasWithNoPanel = mc.getGraphEditor().getBounds();

    mc.simulateToggleTimelineClick();
    // The dock reports TOTAL height, so 420 here is exactly the dock's carve.
    mc.getMixerDock().onResizeHeight(420);
    ASSERT_EQ(mc.getMixerDock().getHeight(), 420);

    mc.simulateToggleTimelineClick();      // hide
    EXPECT_FALSE(timelinePanelIsOpen(mc)); // see HiddenByDefaultAndCarvesNothing's comment
    EXPECT_EQ(mc.getGraphEditor().getBounds(), canvasWithNoPanel) << "a hidden dock carves nothing, at any height";
    EXPECT_EQ(mc.getTimelinePanelHeight(), 420) << "the height outlives a hide";

    mc.simulateToggleTimelineClick(); // show again
    EXPECT_EQ(mc.getMixerDock().getHeight(), 420);
    EXPECT_EQ(mc.getMixerDock().getBottom(), mc.getStatusBar().getBounds().getY());
}
