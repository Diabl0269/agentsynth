// BottomDockDetachedFocusTests.cpp -- Cmd+1/2/3 on a tab whose panel is in its own window raise that window and put
// keyboard focus in its panel; with the Mixer shown in both places the second window is raised and the docked copy
// gets the focus; a docked tab behaves as before (docs/control/shortcuts.md#switching-tabs).
#include "../../TestWait.h"
#include "BottomDockMainAreaFixture.h"
#include "UI/Layout/DetachablePanelHost/DetachedPanelWindow.h"
#include <gtest/gtest.h>
#include <optional>
#include <vector>

namespace {

using synth::ui::BottomDockComponent;
using Tab = BottomDockComponent::Tab;
using Ids = std::vector<juce::String>;

constexpr auto kCmd = juce::ModifierKeys::commandModifier;

struct ShowKey {
    Tab tab;
    juce::juce_wchar digit;
};

const ShowKey kShowKeys[] = {{Tab::Timeline, '1'}, {Tab::Mixer, '2'}, {Tab::MidiRemote, '3'}};

} // namespace

class BottomDockDetachedFocusTest : public BottomDockMainAreaTest {
protected:
    void SetUp() override {
        keysGuard_.emplace(juce::StringArray{"detachedPanelBothPlaces"});
        BottomDockMainAreaTest::SetUp();
        setBothPlaces("move");
        if (!mc_->isBottomDockConfiguredVisible())
            mc_->simulateToggleBottomPanelClick();
        mc_->resized();
        mc_->getFocusRegionsForTest().onRegionFocused = [this](const juce::String& id) { mainFocused_.push_back(id); };
    }
    void TearDown() override {
        BottomDockMainAreaTest::TearDown();
        keysGuard_.reset();
    }
    void setBothPlaces(const char* value) {
        mc_->getAppPropertiesForTest().getUserSettings()->setValue("detachedPanelBothPlaces", value);
        dock().applyDetachBothPlacesPreference();
    }
    // A key as the app receives it: the shortcut runs its command on the next turn of the message loop.
    static void press(juce::Component& target, const juce::KeyPress& key) {
        EXPECT_TRUE(target.keyPressed(key));
        synth::test::flushMessageQueue();
    }
    // Starts recording where the detached window's own registry puts focus; the window's one region is its panel.
    void watch(synth::ui::DetachedPanelWindow& window) {
        window.getFocusRegionsForTest().onRegionFocused = [this](const juce::String& id) {
            windowFocused_.push_back(id);
        };
    }
    static juce::KeyPress cmd(juce::juce_wchar digit) { return juce::KeyPress(digit, kCmd, 0); }

    Ids mainFocused_;
    Ids windowFocused_;
    std::optional<synth::test::PersistedKeysGuard> keysGuard_;
};

TEST_F(BottomDockDetachedFocusTest, TheNumberKeyFocusesTheDetachedTabsWindowInsteadOfTheStrip) {
    for (const auto& row : kShowKeys) {
        auto* host = dock().hostForTab(row.tab);
        host->setDetached(true);
        auto* window = host->getDetachedWindowForTest();
        ASSERT_NE(window, nullptr);
        ASSERT_FALSE(window->getFocusRegionsForTest().getRegions().empty());
        watch(*window);
        windowFocused_.clear();
        mainFocused_.clear();

        press(*mc_, cmd(row.digit));

        EXPECT_EQ(windowFocused_, Ids{window->getFocusRegionsForTest().getRegions().front().id})
            << "the window's panel region took the focus";
        EXPECT_TRUE(mainFocused_.empty()) << "and the dock's tab strip did not";
        EXPECT_TRUE(host->isDetached()) << "raising never redocks";

        host->setDetached(false);
    }
}

TEST_F(BottomDockDetachedFocusTest, TheKeyWorksFromInsideAnotherDetachedWindowToo) {
    dock().getTimelineHost().setDetached(true);
    dock().getMidiRemoteHost().setDetached(true);
    auto* timelineWindow = dock().getTimelineHost().getDetachedWindowForTest();
    auto* controllersWindow = dock().getMidiRemoteHost().getDetachedWindowForTest();
    watch(*controllersWindow);

    press(*timelineWindow, cmd('3')); // Cmd+3 pressed in the Timeline's window

    EXPECT_EQ(windowFocused_.size(), 1u);
    EXPECT_TRUE(mainFocused_.empty());
}

TEST_F(BottomDockDetachedFocusTest, ADockedTabKeepsItsBehaviour) {
    dock().getTimelineHost().setDetached(true);
    mainFocused_.clear();

    press(*mc_, cmd('2'));

    EXPECT_EQ(dock().getActiveTab(), Tab::Mixer);
    EXPECT_EQ(mainFocused_, Ids{"dockTabs"}) << "the docked Mixer's strip is focused";
}

TEST_F(BottomDockDetachedFocusTest, WithBothPlacesOnTheSecondWindowIsRaisedAndTheDockedCopyGetsTheFocus) {
    setBothPlaces("both");
    dock().getDetachIcon(Tab::Mixer)->onClick();
    ASSERT_TRUE(dock().isMixerMirrorOpen());
    dock().setActiveTab(Tab::Timeline);
    auto* mirrorWindow = dock().getMixerMirrorWindowForTest();
    ASSERT_NE(mirrorWindow, nullptr);
    watch(*mirrorWindow);
    mainFocused_.clear();

    press(*mc_, cmd('2'));

    EXPECT_EQ(dock().getActiveTab(), Tab::Mixer) << "the docked Mixer is shown";
    EXPECT_EQ(mainFocused_, Ids{"dockTabs"}) << "focus is in the docked copy's region";
    EXPECT_TRUE(windowFocused_.empty()) << "the raised window does not take it back";
    EXPECT_TRUE(dock().isMixerMirrorOpen());
    EXPECT_FALSE(dock().getMixerHost().isDetached());
}

TEST_F(BottomDockDetachedFocusTest, AWindowThatIsNotThereIsLeftAlone) {
    mainFocused_.clear();
    press(*mc_, cmd('2'));
    EXPECT_EQ(mainFocused_, Ids{"dockTabs"});
    EXPECT_FALSE(dock().isMixerMirrorOpen());
}

// Guard for the shortcut-relay kind: a window opened from the tab strip and one opened from the main area are both
// panel windows that hand the app's shortcuts (undo, redo, Space) to the app.
TEST_F(BottomDockDetachedFocusTest, EveryDetachedPanelWindowHandsAppShortcutsToTheAppFromBothPositions) {
    const juce::KeyPress keys[] = {juce::KeyPress('z', kCmd, 0),
                                   juce::KeyPress('z', kCmd | juce::ModifierKeys::shiftModifier, 0),
                                   juce::KeyPress(juce::KeyPress::spaceKey, juce::ModifierKeys(), ' ')};
    for (const auto& row : kShowKeys) {
        auto* host = dock().hostForTab(row.tab);
        const auto appFallback = host->onAppShortcutFallback;
        ASSERT_TRUE(appFallback) << "MainComponent wires the relay";
        int relayed = 0;
        host->onAppShortcutFallback = [&](const juce::KeyPress& key) {
            ++relayed;
            return appFallback(key);
        };
        for (const bool fromMainArea : {false, true}) {
            SCOPED_TRACE(juce::String(row.digit) + (fromMainArea ? " from the main area" : " from the tab strip"));
            if (const Tab want = fromMainArea ? row.tab : Tab::Canvas; dock().getStoredMainView() != want)
                ASSERT_TRUE(mc_->setMainViewForTest(want));
            if (fromMainArea)
                ASSERT_TRUE(dock().detachMainView());
            else
                host->setDetached(true);
            auto* window = host->getDetachedWindowForTest();
            ASSERT_NE(window, nullptr);
            relayed = 0;
            for (const auto& key : keys)
                EXPECT_TRUE(window->keyPressed(key)) << key.getTextDescription();
            synth::test::flushMessageQueue();
            EXPECT_EQ(relayed, 3);
            host->setDetached(false);
        }
        host->onAppShortcutFallback = appFallback;
        ASSERT_TRUE(mc_->setMainViewForTest(Tab::Canvas));
    }
}
