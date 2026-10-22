// BottomDockSwapKeysTests.cpp -- the keys behind the main area: Cmd+J for the bottom panel, Opt+Cmd+1..4 to swap a
// view into the main area (default keys, routing, clean refusals, the announcement, the icon tooltips) and the
// Tab order following the layout (docs/control/shortcuts.md, docs/control/focus-regions.md).
#include "BottomDockMainAreaFixture.h"
#include "ShortcutManager/AppCommands.h"
#include "UI/Layout/IconButton.h"
#include "UI/Layout/UIAnimation.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <vector>

namespace {

using synth::ui::BottomDockComponent;
using Tab = BottomDockComponent::Tab;

constexpr auto kCmd = juce::ModifierKeys::commandModifier;
constexpr auto kCmdAlt = juce::ModifierKeys::commandModifier | juce::ModifierKeys::altModifier;

struct SwapRow {
    const char* swapAction;
    const char* showAction;
    juce::CommandID command;
    Tab view;
    juce::juce_wchar digit;
    const char* announcement;
};

const SwapRow kRows[] = {
    {"swapTimelineToMain", "toggleTimelinePanel", AppCommands::swapTimelineToMain, Tab::Timeline, '1',
     "Timeline in the main area"},
    {"swapMixerToMain", "toggleMixerPanel", AppCommands::swapMixerToMain, Tab::Mixer, '2', "Mixer in the main area"},
    {"swapMidiRemoteToMain", "toggleMidiRemotePanel", AppCommands::swapMidiRemoteToMain, Tab::MidiRemote, '3',
     "Controllers in the main area"},
    {"swapCanvasToMain", "showCanvas", AppCommands::swapCanvasToMain, Tab::Canvas, '4', "Canvas in the main area"},
};

} // namespace

TEST(BottomDockSwapKeyDefaults, TheBottomPanelKeyIsCmdJAndCmdTIsFree) {
    ShortcutManager manager;
    EXPECT_EQ(manager.getBinding("toggleBottomPanel"), juce::KeyPress('j', kCmd, 0));
    for (const auto& id : manager.getActionIds())
        EXPECT_NE(manager.getBinding(id), juce::KeyPress('t', kCmd, 0)) << id << " holds the released Cmd+T";
}

TEST(BottomDockSwapKeyDefaults, EachViewSwapActionStaysRoutedAndDescribedWithoutADigit) {
    ShortcutManager manager;
    for (const auto& row : kRows) {
        EXPECT_FALSE(manager.getBinding(row.swapAction).isValid()) << "the digits belong to the positional actions";
        EXPECT_FALSE(manager.getBinding(row.showAction).isValid()) << row.showAction;
        EXPECT_EQ(AppCommands::getCommandForAction(row.swapAction), row.command);
        EXPECT_NE(ShortcutManager::getActionDescription(row.swapAction), juce::String(row.swapAction));
    }
    EXPECT_EQ(ShortcutManager::getActionDescription("swapTimelineToMain"), "Show Timeline in the Main Area");
}

TEST(BottomDockSwapKeyDefaults, TheNewCommandIdsComeAfterEveryExistingOne) {
    EXPECT_GT(AppCommands::swapTimelineToMain, AppCommands::showCanvas);
    EXPECT_EQ(AppCommands::swapCanvasToMain, AppCommands::swapTimelineToMain + 3);
    EXPECT_GT(AppCommands::showDockTab1, AppCommands::swapCanvasToMain);
}

class BottomDockSwapKeysTest : public BottomDockMainAreaTest {
protected:
    void SetUp() override {
        BottomDockMainAreaTest::SetUp();
        if (!mc_->isBottomDockConfiguredVisible())
            mc_->simulateToggleBottomPanelClick();
        mc_->resized();
        dock().setAnnouncementHookForTest([this](const juce::String& text) { announced_.push_back(text); });
    }
    bool run(juce::CommandID id) { return mc_->getCommandManager().invokeDirectly(id, false); }
    std::vector<juce::String> regionIds() {
        std::vector<juce::String> ids;
        for (const auto& r : mc_->getFocusRegionsForTest().getRegions())
            ids.push_back(r.id);
        return ids;
    }
    static int indexOf(const std::vector<juce::String>& ids, const char* id) {
        return int(std::find(ids.begin(), ids.end(), juce::String(id)) - ids.begin());
    }
    std::vector<juce::String> announced_;
};

TEST_F(BottomDockSwapKeysTest, EachSwapActionPutsItsViewInTheMainAreaAndAnnouncesIt) {
    for (const auto& row : kRows) {
        if (row.view == Tab::Canvas)
            continue;
        announced_.clear();
        EXPECT_TRUE(run(row.command)) << row.swapAction;
        EXPECT_EQ(dock().getMainView(), row.view) << row.swapAction;
        EXPECT_EQ(announced_, std::vector<juce::String>{row.announcement}) << "once per swap";
        EXPECT_TRUE(run(AppCommands::swapCanvasToMain));
        EXPECT_EQ(dock().getMainView(), Tab::Canvas);
        EXPECT_EQ(announced_.back(), "Canvas in the main area");
    }
}

TEST_F(BottomDockSwapKeysTest, ASwapRefusedForAnyReasonChangesNothingAnnouncesNothingAndStillConsumesTheKey) {
    EXPECT_TRUE(run(AppCommands::swapCanvasToMain)) << "the canvas is already in the main area";

    dock().getTimelineHost().setDetached(true);
    EXPECT_TRUE(run(AppCommands::swapTimelineToMain)) << "a view in its own window";

    dock().setMixerTabEnabled(false);
    EXPECT_TRUE(run(AppCommands::swapMixerToMain)) << "the Mixer outside the Tab placement";

    EXPECT_EQ(dock().getMainView(), Tab::Canvas);
    EXPECT_TRUE(announced_.empty());
    EXPECT_FALSE(holder().isVisible());
}

TEST_F(BottomDockSwapKeysTest, TheSwapActionsWorkWithThePanelClosed) {
    mc_->simulateToggleBottomPanelClick();
    ASSERT_FALSE(mc_->isBottomDockConfiguredVisible());
    EXPECT_TRUE(run(AppCommands::swapTimelineToMain));
    EXPECT_EQ(dock().getMainView(), Tab::Timeline);
    EXPECT_TRUE(regionOpen("timeline"));
}

TEST_F(BottomDockSwapKeysTest, ThePanelKeyTogglesThePanelAroundAViewInTheMainArea) {
    ASSERT_TRUE(run(AppCommands::swapTimelineToMain));
    ASSERT_TRUE(run(AppCommands::toggleBottomPanel));
    EXPECT_FALSE(mc_->isBottomDockConfiguredVisible());
    EXPECT_TRUE(regionOpen("timeline")) << "a view in the main area stays on screen with the panel closed";
    EXPECT_TRUE(holder().isVisible());

    ASSERT_TRUE(run(AppCommands::toggleBottomPanel));
    EXPECT_TRUE(mc_->isBottomDockConfiguredVisible());
}

TEST_F(BottomDockSwapKeysTest, TheSwapIconTooltipNamesTheKeyAndFollowsARebind) {
    ASSERT_TRUE(run(AppCommands::swapTimelineToMain));
    auto& shortcuts = mc_->getShortcutManager();
    const auto tooltipFor = [&](Tab view, const char* name) {
        const auto key =
            ShortcutManager::keyPressToDisplayString(shortcuts.getBinding(dock().swapActionIdForTab(view)));
        return synth::ui::formatShortcutHint(juce::String("Show ") + name + " in the main area", key);
    };
    for (const auto& [view, name] : {std::pair{Tab::Mixer, "Mixer"}, std::pair{Tab::MidiRemote, "Controllers"},
                                     std::pair{Tab::Canvas, "Canvas"}}) {
        const auto tip = dock().getSwapIcon(view).getTooltip();
        EXPECT_EQ(tip, tooltipFor(view, name));
        EXPECT_TRUE(tip.contains("(")) << "the key is part of the tooltip: " << tip;
        EXPECT_EQ(dock().getSwapIcon(view).getTitle(), juce::String("Show ") + name + " in the main area")
            << "the accessible name stays the plain wording";
    }

    const auto oldTip = dock().getSwapIcon(Tab::Mixer).getTooltip();
    ASSERT_EQ(dock().keyPositionOf(Tab::Mixer), 2);
    shortcuts.setBinding("swapDockTab2ToMain", juce::KeyPress('9', kCmdAlt, 0));
    shortcuts.onBindingsChanged();
    EXPECT_NE(dock().getSwapIcon(Tab::Mixer).getTooltip(), oldTip);
    EXPECT_EQ(dock().getSwapIcon(Tab::Mixer).getTooltip(), tooltipFor(Tab::Mixer, "Mixer"));
    shortcuts.setBinding("swapDockTab2ToMain", juce::KeyPress());
    shortcuts.onBindingsChanged();
    EXPECT_EQ(dock().getSwapIcon(Tab::Mixer).getTooltip(), "Show Mixer in the main area")
        << "an unbound key is not named";
    shortcuts.resetToDefaults();
}

TEST_F(BottomDockSwapKeysTest, TheCmdHoldHintsSkipTheViewInMainAndShowTheCanvasTabItsKey) {
    ASSERT_TRUE(run(AppCommands::swapTimelineToMain));
    std::vector<juce::String> actions;
    for (const auto& tab : dock().getStripTabs())
        actions.push_back(tab.actionId);
    EXPECT_EQ(actions, (std::vector<juce::String>{"showDockTab1", "showDockTab2", "showDockTab3"}));
    for (const auto& action : actions)
        EXPECT_TRUE(mc_->getShortcutManager().getBinding(action).isValid()) << action;
}

TEST_F(BottomDockSwapKeysTest, TheTabOrderFollowsTheLayoutWithAnyViewInTheMainArea) {
    // Canvas in main: it comes first, then the strip and the panel's views.
    auto ids = regionIds();
    EXPECT_LT(indexOf(ids, "library"), indexOf(ids, "canvasToolbar"));
    EXPECT_EQ(indexOf(ids, "canvasToolbar") + 1, indexOf(ids, "canvas")) << "the canvas toolbar is right before it";
    EXPECT_LT(indexOf(ids, "canvas"), indexOf(ids, "dockTabs"));
    EXPECT_LT(indexOf(ids, "dockTabs"), indexOf(ids, "timeline"));

    for (const auto& row : kRows) {
        if (row.view == Tab::Canvas)
            continue;
        ASSERT_TRUE(run(row.command));
        ids = regionIds();
        const juce::String mainRegionId = mc_->getMainViewFocusRegionId();
        const char* mainRegion = mainRegionId.toRawUTF8();
        EXPECT_LT(indexOf(ids, "library"), indexOf(ids, mainRegion)) << row.swapAction;
        EXPECT_LT(indexOf(ids, mainRegion), indexOf(ids, "dockTabs")) << row.swapAction;
        EXPECT_LT(indexOf(ids, "dockTabs"), indexOf(ids, "canvas")) << row.swapAction << ": the canvas is in the panel";
        EXPECT_EQ(indexOf(ids, "canvasToolbar") + 1, indexOf(ids, "canvas")) << row.swapAction << ": toolbar leads it";
        EXPECT_LT(indexOf(ids, "canvas"), indexOf(ids, "aiPanel"));
        EXPECT_EQ(std::count(ids.begin(), ids.end(), juce::String(mainRegion)), 1) << "registered once";
        ASSERT_TRUE(run(AppCommands::swapCanvasToMain));
    }
    ids = regionIds();
    EXPECT_LT(indexOf(ids, "canvas"), indexOf(ids, "dockTabs")) << "back to the canvas first";
}

TEST_F(BottomDockSwapKeysTest, TheTimelinesSidePanesStayRightAfterItWhereverItSits) {
    ASSERT_TRUE(run(AppCommands::swapTimelineToMain));
    const auto ids = regionIds();
    const int timeline = indexOf(ids, "timeline");
    EXPECT_EQ(indexOf(ids, "timelineRouting"), timeline + 1);
    EXPECT_EQ(indexOf(ids, "scaleAssist"), timeline + 2);
}
