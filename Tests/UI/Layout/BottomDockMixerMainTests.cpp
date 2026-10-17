// BottomDockMixerMainTests.cpp -- the Mixer in the main area: its host in MainComponent's holder at the canvas's
// rect, showing with the bottom panel closed, not growing the shared panel height, allowed only in the Tab
// placement, and coexisting with the "both places" mirror (docs/layout/chrome.md#the-main-view,
// docs/mixer/panel.md). Reached only through MainComponent::setMainViewForTest.
#include "BottomDockMainAreaFixture.h"
#include "UI/Mixer/MixerPanelComponent/MixerPanelComponent.h"
#include "UI/Mixer/MixerPlacementController.h"
#include <gtest/gtest.h>

namespace {

using synth::ui::BottomDockComponent;
using Tab = BottomDockComponent::Tab;
using Placement = synth::ui::MixerPlacementController::Placement;

class BottomDockMixerMainTest : public BottomDockMainAreaTest {
protected:
    void setLiveSetting(const char* key, const char* value) {
        auto* settings = mc_->getAppPropertiesForTest().getUserSettings();
        ASSERT_NE(settings, nullptr);
        settings->setValue(key, value);
        settings->saveIfNeeded();
    }
    void usePlacement(const char* placement) {
        setLiveSetting("mixerPlacement", placement);
        mc_->getMixerPlacementControllerForTest().applyPlacementPreference();
    }
};

} // namespace

TEST_F(BottomDockMixerMainTest, TheMixerHostFillsTheRectTheCanvasHadAndTheCanvasGoesToTheDock) {
    const auto mainRect = mc_->getGraphEditor().getBounds();

    ASSERT_TRUE(mc_->setMainViewForTest(Tab::Mixer));
    auto& host = dock().getMixerHost();
    EXPECT_EQ(host.getParentComponent(), &holder());
    EXPECT_TRUE(holder().isVisible());
    EXPECT_EQ(holder().getBounds(), mainRect);
    EXPECT_EQ(host.getBounds(), holder().getLocalBounds());
    EXPECT_TRUE(host.isVisible());
    EXPECT_TRUE(dock().getMixerPanel().isVisible());
    EXPECT_TRUE(dock().isInMainArea(Tab::Mixer));
    EXPECT_FALSE(mc_->getGraphEditor().isVisible()) << "the canvas waits in the closed panel";
    EXPECT_EQ(mc_->getMainViewFocusRegionId(), "mixer");
}

TEST_F(BottomDockMixerMainTest, TheMixerInMainIsShowingWithThePanelClosed) {
    ASSERT_FALSE(dock().isMixerShowing());
    ASSERT_TRUE(mc_->setMainViewForTest(Tab::Mixer));
    ASSERT_FALSE(mc_->isBottomDockConfiguredVisible());

    EXPECT_TRUE(dock().isMixerShowing()) << "the meters timer gate is open";
    EXPECT_TRUE(regionOpen("mixer"));
    EXPECT_FALSE(regionOpen("dockTabs"));
}

TEST_F(BottomDockMixerMainTest, GrowingTheMixerToFitItsSectionsNeverResizesThePanel) {
    ASSERT_TRUE(mc_->setMainViewForTest(Tab::Mixer));
    const int before = mc_->getTimelinePanelHeight();
    auto& panel = dock().getMixerPanel();

    ASSERT_TRUE(panel.growHost);
    EXPECT_FALSE(panel.canGrowHost());
    panel.growHost(120, true);
    panel.growHost(120, false);
    panel.growHostToFitSections();
    mc_->simulateToggleBottomPanelClick(); // the dock opens: the fit-after-slide step runs
    EXPECT_EQ(mc_->getTimelinePanelHeight(), before);
}

TEST_F(BottomDockMixerMainTest, TheMixerInTheDockStillGrowsThePanel) {
    mc_->ensureBottomDockOpen();
    dock().setActiveTab(Tab::Mixer);
    auto& panel = dock().getMixerPanel();
    ASSERT_TRUE(panel.canGrowHost());
    const int before = mc_->getTimelinePanelHeight();
    panel.growHost(40, false);
    EXPECT_GT(mc_->getTimelinePanelHeight(), before);
}

TEST_F(BottomDockMixerMainTest, TheMixerCannotGoToMainOutsideTheTabPlacement) {
    usePlacement("ownPanel");
    ASSERT_EQ(mc_->getMixerPlacementControllerForTest().getPlacement(), Placement::OwnPanel);
    EXPECT_FALSE(mc_->setMainViewForTest(Tab::Mixer));
    EXPECT_EQ(dock().getMainView(), Tab::Canvas);
    EXPECT_FALSE(dock().isInMainArea(Tab::Mixer));

    usePlacement("window");
    ASSERT_EQ(mc_->getMixerPlacementControllerForTest().getPlacement(), Placement::Window);
    EXPECT_FALSE(mc_->setMainViewForTest(Tab::Mixer));
    EXPECT_FALSE(holder().isVisible());

    usePlacement("tab");
    EXPECT_TRUE(mc_->setMainViewForTest(Tab::Mixer));
}

TEST_F(BottomDockMixerMainTest, LeavingTheTabPlacementWhileInMainBringsTheCanvasBack) {
    ASSERT_TRUE(mc_->setMainViewForTest(Tab::Mixer));

    usePlacement("ownPanel");
    EXPECT_EQ(dock().getMainView(), Tab::Canvas);
    EXPECT_TRUE(mc_->getGraphEditor().isVisible());
    EXPECT_FALSE(holder().isVisible());
    EXPECT_EQ(dock().getMixerHost().getParentComponent(), &mc_->getMixerPlacementControllerForTest())
        << "the placement controller took the host from the dock";
    EXPECT_FALSE(dock().isInMainArea(Tab::Mixer));
}

TEST_F(BottomDockMixerMainTest, TheCanvasComingBackRestoresTheHostInTheDockWithTheSameLayout) {
    mc_->ensureBottomDockOpen();
    dock().setActiveTab(Tab::Mixer);
    const auto before = dock().getMixerHost().getBounds();
    ASSERT_EQ(before, dock().getLocalBounds().withTrimmedTop(BottomDockComponent::kTabStripHeight));

    ASSERT_TRUE(mc_->setMainViewForTest(Tab::Mixer));
    ASSERT_TRUE(mc_->setMainViewForTest(Tab::Canvas));

    auto& host = dock().getMixerHost();
    EXPECT_EQ(host.getParentComponent(), &dock());
    EXPECT_FALSE(dock().isInMainArea(Tab::Mixer));
    EXPECT_FALSE(holder().isVisible());
    EXPECT_TRUE(mc_->getGraphEditor().isVisible());

    dock().setActiveTab(Tab::Mixer);
    EXPECT_TRUE(host.isVisible());
    EXPECT_TRUE(dock().getMixerPanel().isVisible());
    EXPECT_EQ(host.getBounds(), before);
}

TEST_F(BottomDockMixerMainTest, TheMirrorStaysOutOfTheMainAreaHost) {
    setLiveSetting("detachedPanelBothPlaces", "both");
    dock().applyDetachBothPlacesPreference();
    ASSERT_TRUE(mc_->setMainViewForTest(Tab::Mixer));

    // The view in main has no tab, so its detach icon does nothing.
    ASSERT_NE(dock().getActiveTab(), Tab::Mixer);
    dock().getDetachIcon(Tab::Mixer)->onClick();
    EXPECT_FALSE(dock().isMixerMirrorOpenForTest());
    EXPECT_EQ(dock().getMixerHost().getParentComponent(), &holder());
    EXPECT_FALSE(dock().getMixerHost().isDetached());
}

TEST_F(BottomDockMixerMainTest, AMirrorOpenBeforeTheMixerWentToMainClosesWithoutDetachingTheHost) {
    setLiveSetting("detachedPanelBothPlaces", "both");
    dock().applyDetachBothPlacesPreference();
    dock().setActiveTab(Tab::Mixer);
    dock().getDetachIcon(Tab::Mixer)->onClick();
    ASSERT_TRUE(dock().isMixerMirrorOpenForTest());
    ASSERT_TRUE(mc_->setMainViewForTest(Tab::Mixer));

    setLiveSetting("detachedPanelBothPlaces", "move");
    dock().applyDetachBothPlacesPreference();

    EXPECT_FALSE(dock().isMixerMirrorOpenForTest());
    EXPECT_FALSE(dock().getMixerHost().isDetached());
    EXPECT_EQ(dock().getMixerHost().getParentComponent(), &holder());
    EXPECT_TRUE(dock().isInMainArea(Tab::Mixer));
}
