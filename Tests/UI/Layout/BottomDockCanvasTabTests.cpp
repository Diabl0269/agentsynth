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
#include <string>
#include <utility>
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
        return dynamic_cast<juce::Button*>(mc_->getCanvasToolbar().findChildWithID("toggleMinimap"));
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
    // The canvas toolbar shares the tab strip's row, so the canvas takes the whole body.
    const auto bodyInWindow = body + dock().getPosition();
    EXPECT_EQ(canvas().getBounds(), bodyInWindow);
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

TEST_F(BottomDockCanvasTabTest, TheMinimapStaysAvailableInAShortCanvasAndShrinksToFit) {
    auto* button = minimapButton();
    ASSERT_NE(button, nullptr);
    EXPECT_FALSE(canvas().isMinimapSuppressedBySize());
    EXPECT_TRUE(button->isEnabled());
    const int tallH = canvas().getMinimap().getHeight();

    mc_->setSize(640, 420); // the library sidebar leaves the canvas under 480 wide: no longer a reason to hide it
    EXPECT_FALSE(canvas().isMinimapSuppressedBySize());
    EXPECT_TRUE(button->isEnabled());
    EXPECT_TRUE(button->getTooltip().contains("Minimap"));
    EXPECT_TRUE(canvas().getMinimap().isVisible());
    EXPECT_GE(canvas().getMinimap().getWidth(), synth::ui::MinimapComponent::kMinMapWidth);
    EXPECT_GE(canvas().getMinimap().getHeight(), synth::ui::MinimapComponent::kMinMapHeight);
    EXPECT_LE(canvas().getMinimap().getHeight(), tallH);
    EXPECT_LE(canvas().getMinimap().getBottom(), canvas().getHeight());

    mc_->setSize(1400, 900);
    EXPECT_TRUE(button->isEnabled());
}

TEST(MinimapFloor, OnlyAnEditorTooSmallForALegibleMapSuppressesIt) {
    using synth::ui::MinimapComponent;
    EXPECT_TRUE(MinimapComponent::fitsEditor(600, 200));
    EXPECT_TRUE(MinimapComponent::fitsEditor(240, 160));
    EXPECT_FALSE(MinimapComponent::fitsEditor(239, 400));
    EXPECT_FALSE(MinimapComponent::fitsEditor(800, 159));
}

// The layout-overlap sweep over the main-view swap states.
namespace {

std::string tabNameForSweep(Tab t) {
    switch (t) {
    case Tab::Canvas:
        return "Canvas";
    case Tab::Mixer:
        return "Mixer";
    case Tab::MidiRemote:
        return "MidiRemote";
    case Tab::Timeline:
        break;
    }
    return "Timeline";
}

class MainViewSwapLayoutTest : public MainComponentTest {
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

    struct Block {
        std::string name;
        juce::Rectangle<int> bounds;
    };

    // Every visible piece of the shell that owns a rectangle of its own, in window coordinates.
    std::vector<Block> shellBlocks() {
        std::vector<Block> blocks;
        auto& dock = mc_->getBottomDock();
        const auto add = [&](const std::string& name, juce::Rectangle<int> r) {
            if (!r.isEmpty())
                blocks.push_back({name, r});
        };
        auto& toolbar = mc_->getCanvasToolbar();
        if (toolbar.isVisible()) {
            add("canvasToolbar", toolbar.getBounds());
            for (auto* b : {&toolbar.autoArrangeButton(), &toolbar.minimapButton(), &toolbar.matrixButton()})
                if (b->isVisible())
                    add(std::string("canvasButton:") + b->getComponentID().toStdString(),
                        toolbar.getLocalArea(b, b->getLocalBounds()) + toolbar.getPosition());
        }
        if (mc_->getGraphEditor().isVisible())
            add("canvas", mc_->getGraphEditor().getBounds());
        if (mc_->getMainViewHolderForTest().isVisible())
            add("mainView", mc_->getMainViewHolderForTest().getBounds());
        if (dock.isVisible())
            for (auto* tab : dock.getTabButtons())
                if (tab->isVisible())
                    add("dockTab:" + tab->getName().toStdString(), tab->getBounds() + dock.getPosition());
        return blocks;
    }

    // Walks every pair: two blocks may touch but not overlap, except a button, which is inside its toolbar.
    void expectNoOverlap(const std::string& state) {
        const auto blocks = shellBlocks();
        const auto window = mc_->getLocalBounds();
        for (size_t i = 0; i < blocks.size(); ++i) {
            EXPECT_TRUE(window.contains(blocks[i].bounds)) << state << ": " << blocks[i].name << " leaves the window";
            for (size_t j = i + 1; j < blocks.size(); ++j) {
                const bool nested = blocks[i].name == "canvasToolbar" && blocks[j].name.rfind("canvasButton:", 0) == 0;
                if (nested)
                    continue;
                EXPECT_FALSE(blocks[i].bounds.intersects(blocks[j].bounds))
                    << state << ": " << blocks[i].name << " overlaps " << blocks[j].name;
            }
        }
    }

    std::optional<BottomDockActiveTabResetGuardMDT> resetGuard_;
    std::unique_ptr<MainComponent> mc_;
};

} // namespace

TEST_F(MainViewSwapLayoutTest, NoShellPartsOverlapInAnyMainViewDockTabAndWindowSize) {
    const std::vector<Tab> views{Tab::Canvas, Tab::Timeline, Tab::Mixer, Tab::MidiRemote};
    const std::vector<std::pair<int, int>> sizes{{1400, 900}, {1000, 700}, {700, 480}};
    int states = 0;
    for (auto [w, h] : sizes) {
        for (Tab main : views) {
            for (Tab shown : views) {
                if (shown == main)
                    continue;
                mc_->setSize(w, h);
                if (!mc_->setMainViewForTest(main))
                    continue; // this placement cannot hold that view
                mc_->ensureBottomDockOpen();
                mc_->getBottomDock().setActiveTab(shown);
                const std::string state = std::to_string(w) + "x" + std::to_string(h) +
                                          " main=" + tabNameForSweep(main) + " dock=" + tabNameForSweep(shown);
                expectNoOverlap(state);
                ++states;
            }
        }
    }
    EXPECT_GE(states, 10) << "the sweep must reach every swap state, not skip them";
}

TEST_F(MainViewSwapLayoutTest, TheCanvasButtonsShareTheTabStripRowWhenTheCanvasIsATab) {
    mc_->setMainViewForTest(Tab::Timeline);
    mc_->ensureCanvasShowing();
    auto& dock = mc_->getBottomDock();
    auto& toolbar = mc_->getCanvasToolbar();
    ASSERT_TRUE(dock.isCanvasBodyShowing());
    ASSERT_TRUE(toolbar.isVisible());

    const auto strip = dock.getStripAccessoryBounds() + dock.getPosition();
    EXPECT_EQ(toolbar.getBounds(), strip) << "the buttons sit in the tab strip's row";
    EXPECT_LE(toolbar.getBottom(), dock.getY() + BottomDockComponent::kTabStripHeight);
    EXPECT_EQ(mc_->getGraphEditor().getY(), dock.getY() + BottomDockComponent::kTabStripHeight)
        << "no second header row: the canvas starts right under the tab strip";
    EXPECT_TRUE(toolbar.isInStrip());
    for (auto* tab : dock.getTabButtons())
        if (tab->isVisible())
            EXPECT_LE((tab->getBounds() + dock.getPosition()).getRight(), toolbar.getX()) << tab->getName();
}

TEST_F(MainViewSwapLayoutTest, TheStripButtonsPaintOverTheDockAndTheMainViewGivesTheCaptionsBack) {
    auto& toolbar = mc_->getCanvasToolbar();
    mc_->setMainViewForTest(Tab::Timeline);
    mc_->ensureCanvasShowing();
    ASSERT_TRUE(toolbar.isInStrip());
    EXPECT_GT(mc_->getIndexOfChildComponent(&toolbar), mc_->getIndexOfChildComponent(&mc_->getBottomDock()))
        << "a sibling under the dock would be painted over by its tab strip";
    for (auto* b : {&toolbar.autoArrangeButton(), &toolbar.minimapButton(), &toolbar.matrixButton()}) {
        EXPECT_TRUE(b->getButtonText().isEmpty()) << b->getComponentID() << " is a chip in the strip";
        EXPECT_FALSE(b->getTitle().isEmpty()) << b->getComponentID() << " keeps its screen-reader name";
        EXPECT_FALSE(b->getTooltip().isEmpty()) << b->getComponentID() << " keeps its tooltip";
    }

    mc_->getBottomDock().swapTabToMain(Tab::Canvas);
    EXPECT_FALSE(toolbar.isInStrip());
    EXPECT_EQ(toolbar.getHeight(), synth::ui::CanvasToolbar::kHeight);
    EXPECT_EQ(mc_->getBottomDock().getStripAccessoryBounds(), juce::Rectangle<int>());
    EXPECT_FALSE(toolbar.minimapButton().getButtonText().isEmpty()) << "the captions come back with the main view";
}
