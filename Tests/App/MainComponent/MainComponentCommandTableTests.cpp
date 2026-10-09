// MainComponentCommandTableTests.cpp — pins the CommandSpec table's shape so the
// getAllCommands/getCommandInfo/perform table rewrite can never silently drop, reorder, or
// duplicate a command. Uses getCommandTableForTest() (CommandSpec itself stays private -- read
// via auto, per that accessor's own comment).
#include "AudioEngine/AudioEngine.h"
#include "MainComponentTestFixture.h"
#include "ShortcutManager/AppCommands.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <set>

namespace {

// The exact getAllCommands() order from the legacy switch-based implementation (see git
// history for MainComponentCommands.cpp) -- the table's row order must reproduce it unchanged,
// since it is the menu order contract.
const std::vector<juce::CommandID> kExpectedOrder = {
    AppCommands::openSettings,
    AppCommands::savePreset,
    AppCommands::saveProjectAs,
    AppCommands::exportPatchOnly,
    AppCommands::exportAudio,
    AppCommands::exportStems,
    AppCommands::exportMidi,
    AppCommands::collectAndArchive,
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
    AppCommands::foldAndPackMacros,
    AppCommands::locateMaster,
    AppCommands::renameSelectedModule,
    AppCommands::replaceSelectedModule,
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
    // The ONE bottom-dock open/close toggle -- see MainComponentCommandTable.cpp's own
    // comment. The three rows below now each just show their own tab.
    AppCommands::toggleBottomPanel,
    AppCommands::toggleTimelinePanel,
    // The new row sits right after toggleTimelinePanel in
    // MainComponentCommandTable.cpp -- see that file's own comment for why.
    AppCommands::toggleMixerPanel,
    // Same shape as toggleMixerPanel's own row above -- a third tab on the same dock.
    AppCommands::toggleMidiRemotePanel,
    AppCommands::toggleSidePane,
    AppCommands::focusNextRegion,
    AppCommands::focusPrevRegion,
    AppCommands::focusTimeline,
    AppCommands::focusLibrary,
    AppCommands::focusLibrarySearch,
    AppCommands::openContextMenu,
    AppCommands::openAddTrackMenu,
    AppCommands::openFeedback,
    AppCommands::showWelcomeScreen,
    AppCommands::whatsNew,
#if JUCE_MAC || JUCE_WINDOWS
    AppCommands::checkForUpdates,
#endif
    AppCommands::contribute,
    // buildTransportCommandRows(), appended last in commandTable() -- see that function's
    // own comment for why appending (never interleaving) is always safe here.
    AppCommands::transportPlay,
    AppCommands::transportStop,
    AppCommands::transportToggleLoop,
    AppCommands::transportRecord,
    AppCommands::transportToggleMetronome,
    AppCommands::transportReturnToStart,
    // Cursor moves and loop-locator jumps, appended after the rows above.
    AppCommands::transportNudgeBackBeat,
    AppCommands::transportNudgeForwardBeat,
    AppCommands::transportNudgeBackBar,
    AppCommands::transportNudgeForwardBar,
    AppCommands::transportJumpToLoopStart,
    AppCommands::transportJumpToLoopEnd,
    // Jump to the next/previous timeline marker, appended after the rows above.
    AppCommands::transportJumpToNextMarker,
    AppCommands::transportJumpToPreviousMarker,
    // buildSelectionStepCommandRows(), appended after the transport rows.
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

// The table's row order IS the menu order contract -- pin it against the legacy order.
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

// The Help > contribute item. Dispatched through the command manager (what the menu item does)
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

// Export MIDI is menu-only and never gated on the offline render (it reads the document). With a
// loop range set it asks for the range, then for a file, and writes a readable .mid.
TEST_F(MainComponentTest, ExportMidiCommandWritesTheChosenRangeToTheChosenFile) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.getAudioEngine().suspendDeviceCallback();

    juce::ApplicationCommandInfo info(AppCommands::exportMidi);
    mc.getCommandInfo(AppCommands::exportMidi, info);
    EXPECT_TRUE((info.flags & juce::ApplicationCommandInfo::isDisabled) == 0) << "must not be greyed out";
    EXPECT_TRUE(info.defaultKeypresses.isEmpty()) << "menu-only: no chord";

    const auto track = mc.getTimelineDoc().addTrack(synth::TrackKind::Midi, "Lead");
    const auto clip = mc.getTimelineDoc().addClip(track, 0.0, 8.0, "c");
    // The transport's default loop is [0, 4): the first note is inside it, the second is not.
    synth::MidiNote inside;
    inside.startBeat = 1.0;
    synth::MidiNote outside;
    outside.startBeat = 5.0;
    ASSERT_TRUE(mc.getTimelineDoc().addNote(clip, inside).isValid());
    ASSERT_TRUE(mc.getTimelineDoc().addNote(clip, outside).isValid());

    int rangePrompts = 0;
    mc.midiExportSeams.rangePrompt = [&rangePrompts](std::function<void(synth::MidiExportRange)> onChoice) {
        ++rangePrompts;
        onChoice(synth::MidiExportRange::LoopRange);
    };
    juce::TemporaryFile temp(".mid");
    mc.midiExportSeams.filePrompt = [&temp](std::function<void(const juce::File&)> onFile) { onFile(temp.getFile()); };

    ASSERT_TRUE(mc.getCommandManager().invokeDirectly(AppCommands::exportMidi, false));
    EXPECT_EQ(rangePrompts, 1);

    juce::MidiFile written;
    juce::FileInputStream in(temp.getFile());
    ASSERT_TRUE(in.openedOk());
    ASSERT_TRUE(written.readFrom(in));
    ASSERT_EQ(written.getNumTracks(), 2); // conductor + Lead
    // Loop range chosen: the beat-5 note is trimmed away, leaving the beat-1 one (on + off + name + end).
    EXPECT_EQ(written.getTrack(1)->getNumEvents(), 4);
    EXPECT_DOUBLE_EQ(written.getTrack(1)->getEventPointer(1)->message.getTimeStamp(), 960.0);
}
