// BottomDockMainViewTests.cpp -- the dock's main-view model on the real dock: which tabs the strip
// offers (never the main view, never a detached tab), the main-view swap through setMainView(), and the
// "mainView" / "bottomDockTabOrder" settings round trip (docs/layout/chrome.md#panel-collapse-and-persistence).
#include "../../App/MainComponent/MainComponentTestFixture.h"
#include "BottomDockActiveTabResetGuard.h"
#include "UI/Layout/BottomDockComponent/BottomDockComponent.h"
#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <vector>

namespace {

using synth::ui::BottomDockComponent;
using Tab = BottomDockComponent::Tab;

class BottomDockMainViewTest : public MainComponentTest {
protected:
    void SetUp() override {
        MainComponentTest::SetUp();
        resetGuard_.emplace();
        mcOwner_ = std::make_unique<MainComponent>(std::make_unique<MockProvider>());
        mcOwner_->setSize(1400, 900);
        mcOwner_->newPatchForTest();
    }
    void TearDown() override {
        mcOwner_.reset();
        resetGuard_.reset();
        MainComponentTest::TearDown();
    }

    BottomDockComponent& dock() { return mcOwner_->getBottomDock(); }
    juce::PropertiesFile& settings() { return *mcOwner_->getAppPropertiesForTest().getUserSettings(); }
    void reload() { dock().setApplicationProperties(&mcOwner_->getAppPropertiesForTest()); }
    std::vector<juce::String> stripNames() {
        std::vector<juce::String> names;
        for (const auto& tab : dock().getStripTabs())
            names.push_back(tab.name);
        return names;
    }

    std::optional<BottomDockActiveTabResetGuardMDT> resetGuard_;
    std::unique_ptr<MainComponent> mcOwner_;
};

using Names = std::vector<juce::String>;

} // namespace

TEST_F(BottomDockMainViewTest, TheCanvasIsInMainAndItsTabIsNeverOffered) {
    EXPECT_EQ(dock().getMainView(), Tab::Canvas);
    EXPECT_EQ(stripNames(), (Names{"Timeline", "Mixer", "Controllers"}));
    EXPECT_EQ(dock().hostForTab(Tab::Canvas), nullptr) << "the canvas has no detach-to-window host";
    EXPECT_FALSE(dock().getTabButtons().back()->isVisible()) << "the canvas tab button stays hidden";
}

TEST_F(BottomDockMainViewTest, ADetachedTabIsNotOffered) {
    dock().getMixerHost().setDetached(true);
    EXPECT_EQ(stripNames(), (Names{"Timeline", "Controllers"}));
    dock().getMixerHost().setDetached(false);
    EXPECT_EQ(stripNames(), (Names{"Timeline", "Mixer", "Controllers"}));
}

// The view going up leaves the strip and the canvas comes down into its slot, with its "show" action;
// if the dock was showing the view that went up, it shows the canvas tab instead.
TEST_F(BottomDockMainViewTest, SetMainViewPutsTheCanvasTabInTheSlotOfTheViewThatWentUp) {
    dock().setActiveTab(Tab::Mixer);
    dock().setMainView(Tab::Mixer);

    EXPECT_EQ(dock().getMainView(), Tab::Mixer);
    EXPECT_EQ(stripNames(), (Names{"Timeline", "Canvas", "Controllers"}));
    EXPECT_EQ(dock().getStripTabs()[1].actionId, "showCanvas");
    EXPECT_EQ(dock().getActiveTab(), Tab::Canvas) << "the view that came down takes over the active tab";
    EXPECT_EQ(dock().getActivePanelRoot(), nullptr) << "no canvas panel lives in the dock yet";

    // A detached tab stays out of the strip whichever view is in main.
    dock().getTimelineHost().setDetached(true);
    EXPECT_EQ(stripNames(), (Names{"Canvas", "Controllers"}));
    dock().getTimelineHost().setDetached(false);

    dock().setMainView(Tab::Canvas);
    EXPECT_EQ(stripNames(), (Names{"Timeline", "Mixer", "Controllers"}));
    EXPECT_EQ(dock().getActiveTab(), Tab::Mixer);
}

TEST_F(BottomDockMainViewTest, SetMainViewPersistsTheMainViewAndTheOrder) {
    dock().setMainView(Tab::MidiRemote);
    EXPECT_EQ(settings().getValue("mainView"), "midiRemote");
    EXPECT_EQ(settings().getValue("bottomDockTabOrder"), "timeline,mixer,canvas,midiRemote");

    dock().setMainView(Tab::Canvas);
    EXPECT_EQ(settings().getValue("mainView"), "canvas");
    EXPECT_EQ(settings().getValue("bottomDockTabOrder"), "timeline,mixer,midiRemote,canvas");
}

// A main view saved by a build that can swap views opens with the canvas in main here, and the saved
// value survives untouched for when that build runs again.
TEST_F(BottomDockMainViewTest, ASavedNonCanvasMainViewIsKeptButTheCanvasStaysInMain) {
    settings().setValue("mainView", "timeline");
    settings().setValue("bottomDockTabOrder", "canvas,mixer,midiRemote,timeline");
    reload();

    EXPECT_EQ(dock().getMainView(), Tab::Canvas);
    EXPECT_EQ(stripNames(), (Names{"Mixer", "Controllers", "Timeline"}));
    dock().reorderTabsForTest(Tab::Mixer, Tab::Timeline);
    EXPECT_EQ(settings().getValue("mainView"), "timeline") << "a reorder never rewrites the stored main view";
    EXPECT_EQ(settings().getValue("bottomDockTabOrder"), "canvas,timeline,midiRemote,mixer");
}

TEST_F(BottomDockMainViewTest, AnOlderThreeEntryOrderLoadsWithTheCanvasAppended) {
    settings().setValue("bottomDockTabOrder", "midiRemote,timeline,mixer");
    settings().removeValue("mainView");
    reload();

    EXPECT_EQ(dock().getTabOrderForTest(), (std::vector<Tab>{Tab::MidiRemote, Tab::Timeline, Tab::Mixer, Tab::Canvas}));
    EXPECT_EQ(dock().getMainView(), Tab::Canvas);
    EXPECT_EQ(stripNames(), (Names{"Controllers", "Timeline", "Mixer"})) << "an existing install opens as before";
    EXPECT_EQ(settings().getValue("bottomDockTabOrder"), "midiRemote,timeline,mixer") << "loading writes nothing";
}

TEST_F(BottomDockMainViewTest, ASavedActiveTabThatIsTheMainViewFallsBackToTheFirstOfferedTab) {
    settings().setValue("bottomDockActiveTab", "canvas");
    reload();
    EXPECT_EQ(dock().getActiveTab(), Tab::Timeline);
}
