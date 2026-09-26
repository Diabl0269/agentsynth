// MainComponentCommandTableTests.cpp — FRO76: pins the CommandSpec table's shape so the
// getAllCommands/getCommandInfo/perform table rewrite can never silently drop, reorder, or
// duplicate a command. Uses getCommandTableForTest() (CommandSpec itself stays private -- read
// via auto, per that accessor's own comment).
#include "MainComponentTestFixture.h"
#include "ShortcutManager/AppCommands.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <set>

namespace {

// The exact getAllCommands() order from the pre-FRO76 switch-based implementation (see git
// history for MainComponentCommands.cpp) -- the table's row order must reproduce it unchanged,
// since it is the menu order contract.
const std::vector<juce::CommandID> kExpectedOrder = {
    AppCommands::openSettings,
    AppCommands::savePreset,
    AppCommands::saveProjectAs,
    AppCommands::exportPatchOnly,
    AppCommands::exportAudio,
    AppCommands::exportStems,
    AppCommands::openPreset,
    AppCommands::openProject,
    AppCommands::newPatch,
    AppCommands::undo,
    AppCommands::redo,
    AppCommands::toggleModMatrix,
    AppCommands::toggleMinimap,
    AppCommands::toggleAiPanel,
    AppCommands::autoArrange,
    AppCommands::groupSelection,
    AppCommands::ungroupSelection,
    AppCommands::collapseMacro,
    AppCommands::locateMaster,
    AppCommands::toggleLibrary,
    AppCommands::selectAllModules,
    AppCommands::saveSnippet,
    AppCommands::copySelection,
    AppCommands::pasteSelection,
    AppCommands::duplicateSelection,
    AppCommands::cutSelection,
    AppCommands::repeatSelection,
    AppCommands::togglePlayback,
    AppCommands::snapSetWhole,
    AppCommands::snapSetHalf,
    AppCommands::snapSetQuarter,
    AppCommands::snapSetEighth,
    AppCommands::snapSetSixteenth,
    AppCommands::snapSetThirtySecond,
    AppCommands::snapSetSixtyFourth,
    AppCommands::snapSetHundredTwentyEighth,
    AppCommands::snapCyclePrev,
    AppCommands::snapCycleNext,
    AppCommands::zoomInHorizontal,
    AppCommands::zoomOutHorizontal,
    AppCommands::zoomInVertical,
    AppCommands::zoomOutVertical,
    AppCommands::toggleTimelinePanel,
    // FRO11 (P9-5): the new row sits right after toggleTimelinePanel in
    // MainComponentCommandTable.cpp -- see that file's own comment for why.
    AppCommands::toggleMixerPanel,
    // FRO131: same shape as toggleMixerPanel's own row above -- a third tab on the same dock.
    AppCommands::toggleMidiRemotePanel,
    AppCommands::focusNextRegion,
    AppCommands::focusPrevRegion,
    AppCommands::focusTimeline,
    AppCommands::focusLibrary,
    AppCommands::focusLibrarySearch,
    AppCommands::showWelcomeScreen,
    AppCommands::whatsNew,
#if JUCE_MAC || JUCE_WINDOWS
    AppCommands::checkForUpdates,
#endif
    AppCommands::contribute,
    // FRO125: buildTransportCommandRows(), appended last in commandTable() -- see that function's
    // own comment for why appending (never interleaving) is always safe here.
    AppCommands::transportPlay,
    AppCommands::transportStop,
    AppCommands::transportToggleLoop,
    AppCommands::transportRecord,
    AppCommands::transportToggleMetronome,
    AppCommands::transportReturnToStart,
    // FRO271: cursor moves and loop-locator jumps, appended after the FRO125 rows.
    AppCommands::transportNudgeBackBeat,
    AppCommands::transportNudgeForwardBeat,
    AppCommands::transportNudgeBackBar,
    AppCommands::transportNudgeForwardBar,
    AppCommands::transportJumpToLoopStart,
    AppCommands::transportJumpToLoopEnd,
    // FRO277: jump to the next/previous timeline marker, appended after the FRO271 rows.
    AppCommands::transportJumpToNextMarker,
    AppCommands::transportJumpToPreviousMarker,
    // FRO278: buildSelectionStepCommandRows(), appended after the transport rows.
    AppCommands::selectNextModule,
    AppCommands::selectPreviousModule,
    AppCommands::selectNextTrack,
    AppCommands::selectPreviousTrack,
};

} // namespace

// Every id in the table is unique -- a duplicate would mean getCommandInfo/perform silently
// resolve to whichever row happens to come first, and the second would be permanently dead.
TEST_F(MainComponentTest, CommandTableIdsAreUnique) {
    MainComponent mc(std::make_unique<MockProvider>());
    std::set<juce::CommandID> seen;
    for (const auto& spec : mc.getCommandTableForTest())
        EXPECT_TRUE(seen.insert(spec.id).second) << "duplicate command id " << spec.id;
}

// Every id getAllCommands() reports has a matching table row (trivially true by construction --
// getAllCommands() IS built from the table -- but pinned anyway so a future refactor that
// decouples them again gets caught immediately).
TEST_F(MainComponentTest, EveryGetAllCommandsIdHasATableRow) {
    MainComponent mc(std::make_unique<MockProvider>());
    juce::Array<juce::CommandID> ids;
    mc.getAllCommands(ids);
    const auto& table = mc.getCommandTableForTest();
    for (auto id : ids) {
        const bool found = std::any_of(table.begin(), table.end(), [id](const auto& spec) { return spec.id == id; });
        EXPECT_TRUE(found) << "getAllCommands() id " << id << " has no table row";
    }
}

// The table's row order IS the menu order contract -- pin it against the pre-FRO76 order.
TEST_F(MainComponentTest, TableOrderMatchesHistoricGetAllCommandsOrder) {
    MainComponent mc(std::make_unique<MockProvider>());
    juce::Array<juce::CommandID> ids;
    mc.getAllCommands(ids);
    ASSERT_EQ((size_t)ids.size(), kExpectedOrder.size());
    for (size_t i = 0; i < kExpectedOrder.size(); ++i)
        EXPECT_EQ(ids[(int)i], kExpectedOrder[i]) << "order mismatch at index " << i;
}

// Every row that names an actionId must round-trip through AppCommands::getCommandForAction --
// the same string ShortcutManager::getBinding(actionId) resolves a keypress against, so a typo'd
// or stale actionId would silently bind the wrong command's shortcut.
TEST_F(MainComponentTest, EveryActionIdRoundTripsToItsOwnCommand) {
    MainComponent mc(std::make_unique<MockProvider>());
    for (const auto& spec : mc.getCommandTableForTest()) {
        if (spec.actionId == nullptr)
            continue;
        EXPECT_EQ(AppCommands::getCommandForAction(spec.actionId), spec.id)
            << "actionId \"" << spec.actionId << "\" does not round-trip to its own row's id";
    }
}

// FRO94: the Help > contribute item. Dispatched through the command manager (what the menu item does)
// with the browser launch replaced, so the test sees the URL without opening a real browser.
TEST_F(MainComponentTest, ContributeCommandOpensTheContributePageExactlyOnce) {
    MainComponent mc(std::make_unique<MockProvider>());
    std::vector<juce::String> opened;
    mc.setUrlOpenerForTest([&opened](const juce::URL& u) { opened.push_back(u.toString(true)); });

    juce::ApplicationCommandInfo info(AppCommands::contribute);
    mc.getCommandInfo(AppCommands::contribute, info);
    EXPECT_TRUE((info.flags & juce::ApplicationCommandInfo::isDisabled) == 0) << "must not be greyed out";
    EXPECT_TRUE(info.defaultKeypresses.isEmpty()) << "menu-only: no chord";

    ASSERT_TRUE(mc.getCommandManager().invokeDirectly(AppCommands::contribute, false));
    ASSERT_EQ(opened.size(), 1u);
    EXPECT_EQ(opened[0], juce::String(synth::branding::kContributeUrl));
}
