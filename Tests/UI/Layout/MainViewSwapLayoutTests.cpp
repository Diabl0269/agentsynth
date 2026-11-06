// MainViewSwapLayoutTests.cpp -- the layout-overlap check for the main-view swap states: whichever view is in
// the main area and whichever tab the dock shows, the canvas toolbar, the dock's tab buttons, the canvas and the
// main-view holder never overlap, the canvas buttons share the tab strip's row instead of stacking under it, and
// all of it stays inside the window (docs/layout/chrome.md#the-canvas-as-a-tab).
#include "../../App/MainComponent/MainComponentTestFixture.h"
#include "BottomDockActiveTabResetGuard.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Layout/BottomDockComponent/BottomDockComponent.h"
#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using synth::ui::BottomDockComponent;
using Tab = BottomDockComponent::Tab;

std::string nameOf(Tab t) {
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
                const std::string state =
                    std::to_string(w) + "x" + std::to_string(h) + " main=" + nameOf(main) + " dock=" + nameOf(shown);
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
