// BottomDockMainSuspendTests.cpp -- the main area is never empty: while the stored main view is in its own window
// or the Mixer is outside the Tab placement, the Canvas fills the main area for the session, the saved setting
// stays, and the stored view comes back when its panel is docked again (docs/layout/chrome.md#the-main-view,
// docs/mixer/panel.md).
#include "BottomDockMainAreaFixture.h"
#include "UI/Mixer/MixerPanelComponent/MixerPanelComponent.h"
#include "UI/Mixer/MixerPlacementController.h"
#include <gtest/gtest.h>
#include <vector>

namespace {

using synth::ui::BottomDockComponent;
using Tab = BottomDockComponent::Tab;
using Names = std::vector<juce::String>;

class BottomDockMainSuspendTest : public BottomDockMainAreaTest {
protected:
    void SetUp() override {
        BottomDockMainAreaTest::SetUp();
        dock().onMainViewChanged = [this] {
            ++mainViewChanges_;
            mc_->resized();
        };
    }
    juce::String savedMainView() { return mc_->getAppPropertiesForTest().getUserSettings()->getValue("mainView"); }
    void usePlacement(const char* placement) {
        auto* settings = mc_->getAppPropertiesForTest().getUserSettings();
        settings->setValue("mixerPlacement", placement);
        settings->saveIfNeeded();
        mc_->getMixerPlacementControllerForTest().applyPlacementPreference();
    }
    Names stripNames() {
        Names names;
        for (const auto& tab : dock().getStripTabs())
            names.push_back(tab.name);
        return names;
    }
    bool mainAreaHasAView() { return holder().isVisible() || mc_->getGraphEditor().isVisible(); }
    int mainViewChanges_ = 0;
};

} // namespace

TEST_F(BottomDockMainSuspendTest, ADockedTabDetachedWhileAnotherViewIsInMainChangesNothingInTheMainArea) {
    ASSERT_TRUE(mc_->setMainViewForTest(Tab::Timeline));
    mainViewChanges_ = 0;

    dock().getMixerHost().setDetached(true);
    EXPECT_EQ(dock().getMainView(), Tab::Timeline);
    EXPECT_EQ(dock().getStoredMainView(), Tab::Timeline);
    EXPECT_TRUE(dock().isInMainArea(Tab::Timeline));
    EXPECT_EQ(mainViewChanges_, 0);
    EXPECT_EQ(stripNames(), (Names{"Canvas", "Controllers"})) << "the detached tab leaves the strip as before";

    dock().getMixerHost().setDetached(false);
    EXPECT_EQ(dock().getMainView(), Tab::Timeline);
    EXPECT_EQ(mainViewChanges_, 0);
}

TEST_F(BottomDockMainSuspendTest, TheMainViewsPanelDetachedBringsTheCanvasBackForTheSessionOnly) {
    ASSERT_TRUE(mc_->setMainViewForTest(Tab::Timeline));
    ASSERT_EQ(savedMainView(), "timeline");
    mainViewChanges_ = 0;

    dock().getTimelineHost().setDetached(true);

    EXPECT_EQ(dock().getMainView(), Tab::Canvas);
    EXPECT_EQ(dock().getStoredMainView(), Tab::Timeline);
    EXPECT_EQ(savedMainView(), "timeline") << "the saved setting is not rewritten";
    EXPECT_EQ(mainViewChanges_, 1);
    EXPECT_TRUE(mc_->getGraphEditor().isVisible());
    EXPECT_FALSE(holder().isVisible());
    EXPECT_EQ(holder().getNumChildComponents(), 0) << "the detached panel's host left the holder";
    EXPECT_FALSE(dock().isInMainArea(Tab::Timeline));
    EXPECT_EQ(mc_->getMainViewFocusRegionId(), "canvas");
    EXPECT_EQ(stripNames(), (Names{"Mixer", "Controllers"}))
        << "the Canvas is in the main area, the Timeline in its window";
    EXPECT_TRUE(mainAreaHasAView());

    dock().getTimelineHost().setDetached(false);

    EXPECT_EQ(dock().getMainView(), Tab::Timeline);
    EXPECT_EQ(dock().getTimelineHost().getParentComponent(), &holder());
    EXPECT_TRUE(holder().isVisible());
    EXPECT_FALSE(mc_->getGraphEditor().isVisible());
    EXPECT_EQ(savedMainView(), "timeline");
    EXPECT_EQ(mainViewChanges_, 2);
    EXPECT_EQ(stripNames(), (Names{"Canvas", "Mixer", "Controllers"}));
}

TEST_F(BottomDockMainSuspendTest, EveryViewComesBackWhenItsPanelIsDockedAgain) {
    for (Tab view : {Tab::Timeline, Tab::Mixer, Tab::MidiRemote}) {
        ASSERT_TRUE(mc_->setMainViewForTest(view));
        auto* host = view == Tab::Timeline ? &dock().getTimelineHost()
                     : view == Tab::Mixer  ? &dock().getMixerHost()
                                           : &dock().getMidiRemoteHost();
        host->setDetached(true);
        EXPECT_EQ(dock().getMainView(), Tab::Canvas);
        host->setDetached(false);
        EXPECT_EQ(dock().getMainView(), view);
        EXPECT_TRUE(dock().isInMainArea(view));
        ASSERT_TRUE(mc_->setMainViewForTest(Tab::Canvas));
    }
}

TEST_F(BottomDockMainSuspendTest, ChoosingAnotherViewWhileOneIsSuspendedReplacesTheStoredChoice) {
    ASSERT_TRUE(mc_->setMainViewForTest(Tab::Timeline));
    dock().getTimelineHost().setDetached(true);

    EXPECT_FALSE(dock().swapTabToMain(Tab::Timeline)) << "its panel is in its own window";
    ASSERT_TRUE(dock().swapTabToMain(Tab::MidiRemote));
    EXPECT_EQ(dock().getMainView(), Tab::MidiRemote);
    EXPECT_EQ(savedMainView(), "midiRemote");

    dock().getTimelineHost().setDetached(false);
    EXPECT_EQ(dock().getMainView(), Tab::MidiRemote) << "the Timeline docks as a tab, it does not take the main area";
    EXPECT_EQ(stripNames(), (Names{"Canvas", "Mixer", "Timeline"})) << "the Timeline took the Controllers' old slot";
}

TEST_F(BottomDockMainSuspendTest, ChoosingTheCanvasWhileAViewIsSuspendedMakesTheCanvasTheChoice) {
    ASSERT_TRUE(mc_->setMainViewForTest(Tab::Mixer));
    dock().getMixerHost().setDetached(true);
    ASSERT_EQ(dock().getMainView(), Tab::Canvas);

    ASSERT_TRUE(dock().swapTabToMain(Tab::Canvas));
    EXPECT_EQ(savedMainView(), "canvas");
    EXPECT_NE(dock().getActiveTab(), Tab::Canvas) << "the Canvas is the main view, not a tab";

    dock().getMixerHost().setDetached(false);
    EXPECT_EQ(dock().getMainView(), Tab::Canvas);
}

TEST_F(BottomDockMainSuspendTest, DetachingEveryTabWithTheCanvasInMainLeavesTheMainAreaAloneAndTheStripEmpty) {
    for (auto* host : {&dock().getTimelineHost(), &dock().getMixerHost(), &dock().getMidiRemoteHost()})
        host->setDetached(true);
    EXPECT_EQ(dock().getMainView(), Tab::Canvas);
    EXPECT_FALSE(dock().hasAnyVisibleTab());
    EXPECT_TRUE(mc_->getGraphEditor().isVisible());

    for (auto* host : {&dock().getTimelineHost(), &dock().getMixerHost(), &dock().getMidiRemoteHost()})
        host->setDetached(false);
    EXPECT_TRUE(dock().hasAnyVisibleTab());
}

TEST_F(BottomDockMainSuspendTest, WithTheTimelineInMainTheCanvasTabKeepsThePanelUsableWhenTheOthersAreDetached) {
    ASSERT_TRUE(mc_->setMainViewForTest(Tab::Timeline));
    dock().getMixerHost().setDetached(true);
    dock().getMidiRemoteHost().setDetached(true);
    EXPECT_TRUE(dock().hasAnyVisibleTab()) << "the Canvas tab is still docked";
    EXPECT_EQ(stripNames(), (Names{"Canvas"}));
}

TEST_F(BottomDockMainSuspendTest, ASavedMainViewWhosePanelIsAlreadyInItsOwnWindowOnLaunchStartsWithTheCanvas) {
    auto* settings = mc_->getAppPropertiesForTest().getUserSettings();
    settings->setValue("mainView", "timeline");
    dock().getTimelineHost().setDetached(true);

    dock().setApplicationProperties(&mc_->getAppPropertiesForTest());

    EXPECT_EQ(dock().getMainView(), Tab::Canvas);
    EXPECT_EQ(dock().getStoredMainView(), Tab::Timeline);
    EXPECT_EQ(settings->getValue("mainView"), "timeline");
    dock().getTimelineHost().setDetached(false);
    EXPECT_EQ(dock().getMainView(), Tab::Timeline);
}

TEST_F(BottomDockMainSuspendTest, TheViewInTheMainAreaHasNoTabAndTheCanvasTabHasNoDetachIcon) {
    ASSERT_TRUE(mc_->setMainViewForTest(Tab::Timeline));
    for (Tab tab : {Tab::Canvas, Tab::Mixer, Tab::MidiRemote}) {
        dock().setActiveTab(tab);
        ASSERT_NE(dock().getActiveTab(), Tab::Timeline);
        EXPECT_FALSE(dock().getTimelineHost().isHeaderShownForTest()) << "the main view's host draws no header";
    }
    dock().setActiveTab(Tab::Canvas);
    EXPECT_EQ(dock().getDetachIcon(Tab::Canvas), nullptr) << "the Canvas tab has nothing to detach";
    EXPECT_FALSE(dock().getDetachIcon(Tab::Timeline)->getParentComponent()->isVisible()) << "and no tab, so no icon";
    EXPECT_FALSE(dock().getTimelineHost().isDetached());
}

TEST_F(BottomDockMainSuspendTest, TheMixerOutsideTheTabPlacementHasNoTabNoSwapAndGivesTheMainAreaToTheCanvas) {
    ASSERT_TRUE(mc_->setMainViewForTest(Tab::Mixer));

    for (const char* placement : {"ownPanel", "window"}) {
        usePlacement(placement);
        EXPECT_EQ(dock().getMainView(), Tab::Canvas) << placement;
        EXPECT_EQ(dock().getStoredMainView(), Tab::Mixer) << placement;
        EXPECT_EQ(savedMainView(), "mixer") << placement << ": the setting is kept";
        EXPECT_TRUE(mc_->getGraphEditor().isVisible());
        EXPECT_FALSE(holder().isVisible());
        EXPECT_EQ(stripNames(), (Names{"Timeline", "Controllers"})) << "no Mixer tab, so no swap icon either";
        EXPECT_FALSE(dock().swapTabToMain(Tab::Mixer));
    }

    usePlacement("tab");
    EXPECT_EQ(dock().getMainView(), Tab::Mixer);
    EXPECT_EQ(dock().getMixerHost().getParentComponent(), &holder());
    EXPECT_EQ(savedMainView(), "mixer");
}

TEST_F(BottomDockMainSuspendTest, TheBothPlacesMirrorLeavesTheMainAreaAlone) {
    auto* settings = mc_->getAppPropertiesForTest().getUserSettings();
    settings->setValue("detachedPanelBothPlaces", "both");
    dock().applyDetachBothPlacesPreference();
    mc_->ensureBottomDockOpen();
    dock().setActiveTab(Tab::Mixer);
    dock().getDetachIcon(Tab::Mixer)->onClick();
    ASSERT_TRUE(dock().isMixerMirrorOpenForTest());

    EXPECT_FALSE(dock().getMixerHost().isDetached()) << "the mirror is a second view, the host stays docked";
    EXPECT_EQ(dock().getMainView(), Tab::Canvas);
    EXPECT_TRUE(dock().swapTabToMain(Tab::Mixer)) << "the Mixer can still go to the main area";
    EXPECT_EQ(dock().getMainView(), Tab::Mixer);
    EXPECT_FALSE(dock().isMixerMirrorOpenForTest() && dock().getMixerHost().isDetached());
    EXPECT_FALSE(dock().getMixerHost().isDetached());
    EXPECT_EQ(dock().getMixerHost().getParentComponent(), &holder());
    settings->setValue("detachedPanelBothPlaces", "move");
    dock().applyDetachBothPlacesPreference();
}
