// CanvasWindowDetachTests.cpp -- the canvas in its own window (docs/layout/chrome.md#the-canvas-window): what moves,
// what fills the main area, what is refused, how it docks back, and that a project load and teardown leave it whole.
#include "../../../TestWait.h"
#include "../BottomDockMainAreaFixture.h"
#include "AudioEngine/AudioEngine.h"
#include "Project/ViewDoc.h"
#include "TestPaths.h"
#include "UI/Chrome/CanvasToolbar/CanvasToolbar.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Graph/ProjectLoad/EditBlockOverlay.h"
#include "UI/Graph/ProjectLoad/LoadStageLine.h"
#include "UI/Layout/CanvasWindow/CanvasWindowController.h"
#include "UI/Layout/DetachablePanelHost/DetachedPanelWindow.h"
#include <gtest/gtest.h>
#include <vector>

namespace {

using synth::ui::BottomDockComponent;
using Tab = BottomDockComponent::Tab;

constexpr auto kCmd = juce::ModifierKeys::commandModifier;

std::vector<juce::Rectangle<int>> cardBounds(GraphEditor& editor) {
    std::vector<juce::Rectangle<int>> out;
    for (auto* card : editor.getModuleComponents())
        out.push_back(card->getBounds());
    return out;
}

} // namespace

class CanvasWindowDetachTest : public BottomDockMainAreaTest {
protected:
    void SetUp() override {
        BottomDockMainAreaTest::SetUp();
        if (!mc_->isBottomDockConfiguredVisible())
            mc_->simulateToggleBottomPanelClick();
        mc_->resized();
    }
    synth::ui::CanvasWindowController& controller() { return mc_->getCanvasWindow(); }
    synth::ui::DetachedPanelWindow* window() { return controller().getWindowForTest(); }
    GraphEditor& canvas() { return mc_->getGraphEditor(); }
    synth::ui::CanvasToolbar& toolbar() { return mc_->getCanvasToolbar(); }
    void expectDockedAtOldZOrder() {
        EXPECT_EQ(canvas().getParentComponent(), mc_.get());
        EXPECT_EQ(mc_->getIndexOfChildComponent(&canvas()), 0) << "under everything, as at startup";
        EXPECT_EQ(toolbar().getParentComponent(), mc_.get());
        EXPECT_LT(mc_->getIndexOfChildComponent(&toolbar()), mc_->getIndexOfChildComponent(&dock()));
    }
};

TEST_F(CanvasWindowDetachTest, DetachingFromTheMainAreaMovesToolbarAndCanvasIntoTheWindowAndTheActiveTabFillsMain) {
    dock().setActiveTab(Tab::Mixer);
    const auto cards = cardBounds(canvas());
    ASSERT_TRUE(dock().canCanvasLeave());

    mc_->setCanvasDetached(true);

    ASSERT_NE(window(), nullptr);
    EXPECT_EQ(window()->getName(), "Canvas");
    EXPECT_TRUE(window()->isParentOf(&canvas()));
    EXPECT_TRUE(window()->isParentOf(&toolbar()));
    EXPECT_EQ(toolbar().getY(), 0) << "the toolbar on top";
    EXPECT_EQ(canvas().getY(), toolbar().getBottom()) << "the canvas under it";
    EXPECT_FALSE(toolbar().isInStrip());
    EXPECT_EQ(toolbar().getDockBackButton(), &controller().getDockBackButtonForTest());
    EXPECT_TRUE(dock().isCanvasAway());
    EXPECT_EQ(dock().getMainView(), Tab::Mixer) << "the active tab took the main area";
    EXPECT_EQ(dock().getStoredMainView(), Tab::Canvas) << "the fill is never saved";
    EXPECT_EQ(dock().getMixerHost().getParentComponent(), &holder());
    EXPECT_EQ(cardBounds(canvas()), cards) << "the same cards, unmoved";
    EXPECT_EQ(dock().getDetachIcon(Tab::Canvas)->getTitle(), "Dock the Canvas back");
    EXPECT_EQ(mc_->getFocusRegionsForTest().findById("canvas"), nullptr) << "the window's registry holds it";

    mc_->setCanvasDetached(false);

    EXPECT_EQ(window(), nullptr);
    expectDockedAtOldZOrder();
    EXPECT_FALSE(dock().isCanvasAway());
    EXPECT_EQ(dock().getMainView(), Tab::Canvas) << "back where it was";
    EXPECT_TRUE(dock().isParentOf(&dock().getMixerHost())) << "the stand-in went back to its tab";
    EXPECT_EQ(toolbar().getDockBackButton(), nullptr);
    EXPECT_EQ(cardBounds(canvas()), cards);
    EXPECT_NE(mc_->getFocusRegionsForTest().findById("canvas"), nullptr);
}

TEST_F(CanvasWindowDetachTest, ACanvasDetachedFromItsTabLeavesTheMainAreaAloneAndReturnsToTheTab) {
    ASSERT_TRUE(mc_->setMainViewForTest(Tab::Timeline));
    dock().setActiveTab(Tab::Canvas);
    ASSERT_TRUE(dock().isCanvasBodyShowing());
    ASSERT_TRUE(toolbar().isInStrip());

    mc_->setCanvasDetached(true);

    ASSERT_NE(window(), nullptr);
    EXPECT_EQ(dock().getMainView(), Tab::Timeline);
    EXPECT_FALSE(dock().isCanvasBodyShowing());
    EXPECT_NE(dock().getActiveTab(), Tab::Canvas) << "the tab has no body while its window is open";
    EXPECT_FALSE(toolbar().isInStrip()) << "captions back on in the window";

    mc_->setCanvasDetached(false);

    EXPECT_EQ(canvas().getParentComponent(), mc_.get());
    EXPECT_EQ(mc_->getIndexOfChildComponent(&canvas()), 0) << "under everything, as at startup";
    EXPECT_EQ(dock().getMainView(), Tab::Timeline);
    EXPECT_FALSE(dock().isInMainArea(Tab::Canvas));
    mc_->resized();
    EXPECT_EQ(dock().getActiveTab(), Tab::Canvas) << "docked back, it is shown in its tab";
    EXPECT_TRUE(dock().isCanvasBodyShowing());
    EXPECT_GT(mc_->getIndexOfChildComponent(&toolbar()), mc_->getIndexOfChildComponent(&dock()))
        << "in the strip row the toolbar is lifted over the dock again";
}

TEST_F(CanvasWindowDetachTest, TheCanvasTabStaysInTheStripAndChoosingItRaisesTheWindow) {
    mc_->setCanvasDetached(true);
    int raises = 0;
    dock().onCanvasWindowRaise = [&] { ++raises; };
    dock().setActiveTab(Tab::Canvas);
    EXPECT_EQ(raises, 1);
    EXPECT_NE(dock().getActiveTab(), Tab::Canvas);
    EXPECT_TRUE(dock().getDetachIcon(Tab::Canvas)->getParentComponent()->isVisible()) << "the tab is still there";
}

TEST_F(CanvasWindowDetachTest, WhileAwayTheCanvasCannotBeSwappedIntoMainAndTheFillCannotDetachAlone) {
    mc_->setCanvasDetached(true);
    const Tab fill = dock().getMainView();
    ASSERT_NE(fill, Tab::Canvas);
    EXPECT_FALSE(mc_->setMainViewForTest(Tab::Canvas));
    EXPECT_TRUE(dock().isCanvasAway());

    // Detaching the stand-in hands the main area to the next view that can fill it.
    ASSERT_TRUE(dock().canDetachMainView());
    ASSERT_TRUE(dock().detachMainView());
    EXPECT_NE(dock().getMainView(), Tab::Canvas);
    EXPECT_NE(dock().getMainView(), fill);
    EXPECT_NE(window(), nullptr);
}

TEST_F(CanvasWindowDetachTest, ADetachThatWouldLeaveTheMainAreaEmptyIsRefused) {
    for (auto* host : {&dock().getTimelineHost(), &dock().getMixerHost(), &dock().getMidiRemoteHost()})
        host->setDetached(true);
    EXPECT_FALSE(dock().canCanvasLeave());
    EXPECT_FALSE(dock().canDetachMainView());
    mc_->setCanvasDetached(true);
    EXPECT_EQ(window(), nullptr) << "refused: nothing else could fill the main area";
    for (auto* host : {&dock().getTimelineHost(), &dock().getMixerHost(), &dock().getMidiRemoteHost()})
        host->setDetached(false);
}

TEST_F(CanvasWindowDetachTest, LosingTheLastStandInDocksTheCanvasBack) {
    mc_->setCanvasDetached(true);
    dock().getMixerHost().setDetached(true);
    dock().getMidiRemoteHost().setDetached(true);
    ASSERT_EQ(dock().getMainView(), Tab::Timeline);
    EXPECT_FALSE(dock().canDetachMainView()) << "the last stand-in is held in the main area";

    dock().getTimelineHost().setDetached(true); // its own window's path, past the refusal

    EXPECT_EQ(window(), nullptr) << "the main area is never left empty";
    EXPECT_EQ(dock().getMainView(), Tab::Canvas);
    expectDockedAtOldZOrder();
    for (auto* host : {&dock().getTimelineHost(), &dock().getMixerHost(), &dock().getMidiRemoteHost()})
        host->setDetached(false);
}

TEST_F(CanvasWindowDetachTest, EscCmdWAndTheDockBackButtonDockTheCanvasBack) {
    const juce::KeyPress keys[] = {juce::KeyPress(juce::KeyPress::escapeKey), juce::KeyPress('w', kCmd, 0)};
    for (const auto& key : keys) {
        SCOPED_TRACE(key.getTextDescription());
        mc_->setCanvasDetached(true);
        ASSERT_NE(window(), nullptr);
        EXPECT_TRUE(window()->keyPressed(key));
        synth::test::flushMessageQueue();
        EXPECT_EQ(window(), nullptr);
        expectDockedAtOldZOrder();
    }
    mc_->setCanvasDetached(true);
    controller().getDockBackButtonForTest().triggerClick();
    synth::test::flushMessageQueue();
    EXPECT_EQ(window(), nullptr);
    expectDockedAtOldZOrder();
}

TEST_F(CanvasWindowDetachTest, AppShortcutsPressedInTheCanvasWindowReachTheApp) {
    int relayed = 0;
    const auto relay = controller().onAppShortcut;
    ASSERT_TRUE(relay) << "MainComponent wires the relay";
    controller().onAppShortcut = [&](const juce::KeyPress& key) {
        ++relayed;
        return relay(key);
    };
    mc_->setCanvasDetached(true);
    const juce::KeyPress keys[] = {juce::KeyPress('z', kCmd, 0),
                                   juce::KeyPress(juce::KeyPress::spaceKey, juce::ModifierKeys(), ' '),
                                   juce::KeyPress('j', kCmd, 0)};
    for (const auto& key : keys)
        EXPECT_TRUE(window()->keyPressed(key)) << key.getTextDescription();
    synth::test::flushMessageQueue();
    EXPECT_EQ(relayed, 3);
    EXPECT_NE(window(), nullptr) << "none of them closes the window";
    controller().onAppShortcut = relay;
}

// A key pressed on the focused canvas in its window goes where JUCE sends it: the canvas first, then up its parents to
// the window, which hands what is left to the app. The edit keys must still edit the canvas from there.
TEST_F(CanvasWindowDetachTest, EditKeysPressedOnTheCanvasInItsWindowEditTheCanvas) {
    ASSERT_TRUE(mc_->setMainViewForTest(Tab::Timeline));
    mc_->setCanvasDetached(true);
    ASSERT_NE(window(), nullptr);
    const auto press = [this](const juce::KeyPress& key) {
        for (auto* c = static_cast<juce::Component*>(&canvas()); c != nullptr; c = c->getParentComponent())
            if (c->keyPressed(key))
                return true;
        return false;
    };
    canvas().addModuleAtCanvasPosition("Oscillator", {400, 300}, {});
    const int cards = canvas().getModuleComponents().size();
    ASSERT_GT(cards, 1) << "the output card and an oscillator to edit";

    EXPECT_TRUE(press(juce::KeyPress('a', kCmd, 0)));
    synth::test::flushMessageQueue();
    EXPECT_EQ(static_cast<int>(canvas().getSelectedNodes().size()), cards) << "Cmd+A selects every card";

    canvas().selectModule(canvas().getModuleComponents().getLast()->getNodeId(), false);
    EXPECT_TRUE(press(juce::KeyPress(juce::KeyPress::deleteKey)));
    synth::test::flushMessageQueue();
    EXPECT_EQ(canvas().getModuleComponents().size(), cards - 1) << "Delete removes the selected card";

    EXPECT_TRUE(press(juce::KeyPress('z', kCmd, 0)));
    synth::test::flushMessageQueue();
    EXPECT_EQ(canvas().getModuleComponents().size(), cards) << "Cmd+Z brings it back";
    EXPECT_NE(window(), nullptr) << "no edit key closes the window";
}

TEST_F(CanvasWindowDetachTest, TheNumberKeyOfTheCanvasTabRaisesItsWindow) {
    ASSERT_TRUE(mc_->setMainViewForTest(Tab::Timeline));
    mc_->setCanvasDetached(true);
    int raises = 0;
    dock().onCanvasWindowRaise = [&] { ++raises; };
    mc_->showBottomDockTabFromShortcut(Tab::Canvas);
    EXPECT_EQ(raises, 1);
    mc_->ensureCanvasShowing();
    EXPECT_NE(window(), nullptr);
}

TEST_F(CanvasWindowDetachTest, AProjectOpenedWhileDetachedLoadsIntoTheWindowAndKeepsItsView) {
    const auto src = juce::File(juce::String(UI_VERIFY_FIXTURES_DIR)).getChildFile("macros.agsproj");
    const auto copy = juce::File::createTempFile(".agsproj");
    ASSERT_TRUE(src.copyDirectoryTo(copy));
    mc_->getAudioEngine().suspendDeviceCallback();
    ASSERT_TRUE(mc_->setMainViewForTest(Tab::Timeline)); // the canvas a tab: the load would queue a panel fit
    mc_->setCanvasDetached(true);

    ASSERT_TRUE(mc_->openProjectForTest(copy));
    synth::test::flushMessageQueue();

    ASSERT_NE(window(), nullptr) << "a load never docks the canvas back";
    EXPECT_TRUE(window()->isParentOf(&canvas()));
    EXPECT_FALSE(canvas().getModuleComponents().isEmpty());
    for (auto* card : canvas().getModuleComponents())
        EXPECT_TRUE(window()->isParentOf(card));
    const auto view = canvas().getViewDoc();

    mc_->setCanvasDetached(false);
    mc_->resized();

    const auto after = canvas().getViewDoc();
    EXPECT_FLOAT_EQ(after.zoom, view.zoom) << "no panel fit waited for the canvas's return";
    EXPECT_FLOAT_EQ(after.panX, view.panX);
    EXPECT_FLOAT_EQ(after.panY, view.panY);
    mc_->newPatchForTest();
    copy.deleteRecursively();
}

TEST_F(CanvasWindowDetachTest, TheAppClosesCleanlyWithTheCanvasWindowOpen) {
    mc_->setCanvasDetached(true);
    ASSERT_NE(window(), nullptr);
    mc_.reset(); // the window goes before the canvas it borrows; a leak or a dangling child would trip here
    SUCCEED();
}

// The load's stage line and edit block sit over the canvas in the main window. With the canvas in its own window they
// hide instead of floating over the view that fills the main area, and come back with the canvas while the load runs.
TEST(CanvasWindowLoadOverlays, TheyHideWhileTheCanvasIsInAnotherWindowAndReturnWithIt) {
    juce::Component main, elsewhere, canvas;
    main.setBounds(0, 0, 800, 600);
    elsewhere.setBounds(0, 0, 800, 600);
    canvas.setBounds(0, 100, 800, 500);
    main.addAndMakeVisible(canvas);
    synth::ui::LoadStageLine line(canvas);
    synth::ui::EditBlockOverlay block(canvas, [] {});
    main.addChildComponent(line);
    main.addChildComponent(block);
    line.update("Loading", 0.5f, true);
    block.setBlocking(true);
    ASSERT_TRUE(line.isVisible());
    ASSERT_TRUE(block.isVisible());

    elsewhere.addAndMakeVisible(canvas);
    EXPECT_FALSE(line.isVisible()) << "no line over the stand-in view";
    EXPECT_FALSE(block.isVisible());

    main.addAndMakeVisible(canvas, 0);
    EXPECT_TRUE(line.isVisible()) << "the load is still running";
    EXPECT_TRUE(block.isVisible());
    EXPECT_EQ(line.getBounds().getCentreX(), canvas.getBounds().getCentreX());

    line.update("Loading", 1.0f, false);
    block.setBlocking(false);
    EXPECT_FALSE(block.isVisible());
}

// Regression test for FRO1514: a drag that starts in the main window (a library row) must raise the canvas window, or
// the main window, raised by the press, covers it and the drop never reaches the canvas.
TEST_F(CanvasWindowDetachTest, ADragStartedInTheMainWindowRaisesTheCanvasWindowOnlyWhileItIsOpen) {
    const juce::DragAndDropTarget::SourceDetails details("Oscillator", mc_.get(), {});

    mc_->dragOperationStarted(details);
    EXPECT_EQ(controller().getDropRaiseCountForTest(), 0) << "docked: nothing to raise";

    mc_->setCanvasDetached(true);
    ASSERT_NE(window(), nullptr);
    mc_->dragOperationStarted(details);
    EXPECT_EQ(controller().getDropRaiseCountForTest(), 1);
}
