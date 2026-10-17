// BottomDockCanvasTabTests.cpp -- the canvas as a bottom-panel tab: its body rect in the dock, the canvas
// hidden when another tab shows, the reveal seam, the canvas focus region, and the Minimap toggle that
// follows the canvas's size (docs/layout/chrome.md#the-canvas-as-a-tab). Reached only through
// MainComponent::setMainViewForTest: nothing shipped puts another view in the main area yet.
#include "../../App/MainComponent/MainComponentTestFixture.h"
#include "BottomDockActiveTabResetGuard.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Layout/BottomDockComponent/BottomDockComponent.h"
#include "UI/Mixer/MixerPanelComponent/MixerPanelComponent.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <vector>

namespace {

using synth::ui::BottomDockComponent;
using Tab = BottomDockComponent::Tab;

class BottomDockCanvasTabTest : public MainComponentTest {
protected:
    void SetUp() override {
        MainComponentTest::SetUp();
        resetGuard_.emplace();
        mc_ = std::make_unique<MainComponent>(std::make_unique<MockProvider>());
        mc_->setSize(1400, 900);
        mc_->newPatchForTest();
    }
    void TearDown() override {
        mc_.reset();
        resetGuard_.reset();
        MainComponentTest::TearDown();
    }

    BottomDockComponent& dock() { return mc_->getBottomDock(); }
    GraphEditor& canvas() { return mc_->getGraphEditor(); }
    bool regionOpen(const char* id) {
        const auto* region = mc_->getFocusRegionsForTest().findById(id);
        return region != nullptr && region->isCurrentlyOpen();
    }
    juce::Button* minimapButton() {
        for (auto* child : mc_->getChildren())
            if (auto* button = dynamic_cast<juce::Button*>(child))
                if (button->getComponentID() == "toggleMinimap")
                    return button;
        return nullptr;
    }

    std::optional<BottomDockActiveTabResetGuardMDT> resetGuard_;
    std::unique_ptr<MainComponent> mc_;
};

} // namespace

TEST_F(BottomDockCanvasTabTest, TheCanvasFillsTheTabBodyWhileItsTabShows) {
    mc_->setMainViewForTest(Tab::Timeline);
    mc_->ensureCanvasShowing();

    EXPECT_EQ(dock().getActiveTab(), Tab::Canvas);
    EXPECT_TRUE(dock().isCanvasBodyShowing());
    ASSERT_TRUE(canvas().isVisible());
    const auto body = dock().getCanvasBodyBounds();
    EXPECT_FALSE(body.isEmpty());
    EXPECT_EQ(canvas().getBounds(), body + dock().getPosition());
    EXPECT_EQ(canvas().getY(), dock().getY() + BottomDockComponent::kTabStripHeight);
    EXPECT_FALSE(dock().hitTest(body.getCentreX(), body.getCentreY())) << "clicks in the body belong to the canvas";
    EXPECT_EQ(dock().getDetachIcon(Tab::Canvas), nullptr) << "the canvas has no window to open into";
}

TEST_F(BottomDockCanvasTabTest, TheCanvasIsHiddenWhileAnotherTabOrNoPanelShows) {
    mc_->setMainViewForTest(Tab::Timeline);
    EXPECT_FALSE(canvas().isVisible()) << "the panel is closed";

    mc_->ensureCanvasShowing();
    ASSERT_TRUE(canvas().isVisible());
    dock().setActiveTab(Tab::Mixer);
    EXPECT_FALSE(canvas().isVisible());
    EXPECT_TRUE(dock().getCanvasBodyBounds().isEmpty());
    EXPECT_TRUE(dock().getDetachIcon(Tab::Mixer)->isVisible());

    dock().setActiveTab(Tab::Canvas);
    EXPECT_TRUE(canvas().isVisible());

    mc_->simulateToggleBottomPanelClick();
    EXPECT_FALSE(canvas().isVisible()) << "closing the panel hides the canvas with it";
}

TEST_F(BottomDockCanvasTabTest, TheCanvasReturnsToTheMainAreaWithItsView) {
    mc_->setMainViewForTest(Tab::Timeline);
    mc_->setMainViewForTest(Tab::Canvas);
    EXPECT_TRUE(canvas().isVisible());
    EXPECT_GT(canvas().getWidth(), 0);
    EXPECT_LT(canvas().getBottom(), mc_->getHeight()) << "the main area, above the status bar";
}

TEST_F(BottomDockCanvasTabTest, EnsureCanvasShowingOpensThePanelOnTheCanvasTab) {
    mc_->setMainViewForTest(Tab::Timeline);
    ASSERT_FALSE(mc_->isBottomDockConfiguredVisible());
    dock().setActiveTab(Tab::Mixer);

    mc_->ensureCanvasShowing();
    EXPECT_TRUE(mc_->isBottomDockConfiguredVisible());
    EXPECT_EQ(dock().getActiveTab(), Tab::Canvas);
    EXPECT_TRUE(canvas().isVisible());
}

TEST_F(BottomDockCanvasTabTest, EnsureCanvasShowingDoesNothingWhileTheCanvasIsTheMainView) {
    const auto tab = dock().getActiveTab();
    mc_->ensureCanvasShowing();
    EXPECT_FALSE(mc_->isBottomDockConfiguredVisible());
    EXPECT_EQ(dock().getActiveTab(), tab);
}

TEST_F(BottomDockCanvasTabTest, TheMixerAndLocateRevealsGoThroughTheSeam) {
    mc_->setMainViewForTest(Tab::Timeline);
    ASSERT_TRUE(dock().getMixerPanel().onRevealCanvas != nullptr);
    dock().getMixerPanel().onRevealCanvas();
    EXPECT_TRUE(canvas().isVisible());

    mc_->simulateToggleBottomPanelClick();
    ASSERT_FALSE(canvas().isVisible());
    mc_->hideWelcomeScreenForTest(); // the end of every project open and patch load
    EXPECT_TRUE(canvas().isVisible());
}

TEST_F(BottomDockCanvasTabTest, TheCanvasFocusRegionIsOpenOnlyWhileTheCanvasIsOnScreen) {
    EXPECT_TRUE(regionOpen("canvas"));
    mc_->setMainViewForTest(Tab::Timeline);
    EXPECT_FALSE(regionOpen("canvas"));

    mc_->ensureCanvasShowing();
    EXPECT_TRUE(regionOpen("canvas"));
    EXPECT_TRUE(regionOpen("timeline")) << "the timeline is open in the main area, above the canvas tab";
    EXPECT_TRUE(regionOpen("dockTabs"));

    dock().setActiveTab(Tab::Mixer);
    EXPECT_FALSE(regionOpen("canvas"));

    // Asking for the closed region opens the panel on the canvas tab.
    EXPECT_TRUE(mc_->getFocusRegionsForTest().focusRegionById("canvas"));
    EXPECT_EQ(dock().getActiveTab(), Tab::Canvas);
}

TEST_F(BottomDockCanvasTabTest, ClosingThePanelAroundTheCanvasHandsFocusToTheMainView) {
    mc_->setMainViewForTest(Tab::Timeline);
    mc_->ensureCanvasShowing();
    EXPECT_EQ(mc_->getMainViewFocusRegionId(), "timeline");

    std::vector<juce::String> focused;
    mc_->getFocusRegionsForTest().onRegionFocused = [&](const juce::String& id) { focused.push_back(id); };
    mc_->setFocusedComponentOverrideForTest(&canvas());
    mc_->toggleBottomPanelFromShortcut();

    EXPECT_FALSE(mc_->isBottomDockConfiguredVisible());
    EXPECT_EQ(std::count(focused.begin(), focused.end(), "canvas"), 0) << "the canvas is no longer where focus goes";
    EXPECT_EQ(std::count(focused.begin(), focused.end(), "dockTabs"), 0);
}

TEST_F(BottomDockCanvasTabTest, TheMainViewKeepsItsFocusRegionName) {
    EXPECT_EQ(mc_->getMainViewFocusRegionId(), "canvas");
    mc_->setMainViewForTest(Tab::Mixer);
    EXPECT_EQ(mc_->getMainViewFocusRegionId(), "mixer");
    mc_->setMainViewForTest(Tab::MidiRemote);
    EXPECT_EQ(mc_->getMainViewFocusRegionId(), "midiRemote");
}

TEST_F(BottomDockCanvasTabTest, TheMinimapToggleIsGreyedOutUnderTheSizeFloor) {
    auto* button = minimapButton();
    ASSERT_NE(button, nullptr);
    EXPECT_FALSE(canvas().isMinimapSuppressedBySize());
    EXPECT_TRUE(button->isEnabled());
    EXPECT_TRUE(button->getTooltip().contains("Minimap"));

    mc_->setSize(640, 420); // the library sidebar leaves the canvas under 480 wide
    EXPECT_TRUE(canvas().isMinimapSuppressedBySize());
    EXPECT_FALSE(button->isEnabled());
    EXPECT_EQ(button->getTooltip(), "Needs a taller canvas");

    mc_->setSize(1400, 900);
    EXPECT_TRUE(button->isEnabled());
    EXPECT_TRUE(button->getTooltip().contains("Minimap"));
}
