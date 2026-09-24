// ActionPickerTests.cpp -- FRO135 (docs/control/midi-remote-ui.md#assign-from-the-panel-control-first-learn):
// the "Choose an action" list -- command-dispatched actions only, grouped by ShortcutCategory in the
// Shortcuts tab's order, named by ShortcutManager::getActionDescription, searchable. Suite name contains
// "MidiRemote" per the ship-task --gtest_filter convention.

#include "ShortcutManager/AppCommands.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/MidiRemote/ActionPicker/ActionPickerComponent.h"

#include <gtest/gtest.h>
#include <optional>

using synth::ui::ActionPickerComponent;
using synth::ui::ActionPickerRow;
using synth::ui::buildActionPickerRows;

TEST(MidiRemoteActionPickerTest, ListsOnlyCommandDispatchedActions) {
    const auto rows = buildActionPickerRows({});
    int actions = 0;
    for (const auto& row : rows) {
        if (row.isHeader || row.isContinuous) // FRO236: the Continuous group has its own rows below
            continue;
        ++actions;
        EXPECT_NE(AppCommands::getCommandForAction(row.actionId), AppCommands::kNoCommand) << row.actionId;
        EXPECT_EQ(row.label, ShortcutManager::getActionDescription(row.actionId));
    }
    EXPECT_GT(actions, 5);

    // A surface action (rebindable, but consulted by a component's own keyPressed) has no command to invoke.
    ASSERT_EQ(AppCommands::getCommandForAction("timelineToggleLoop"), AppCommands::kNoCommand);
    for (const auto& row : rows)
        EXPECT_NE(row.actionId, "timelineToggleLoop");
}

TEST(MidiRemoteActionPickerTest, GroupsByCategoryInTheShortcutsTabOrderWithAHeaderPerNonEmptyGroup) {
    const auto rows = buildActionPickerRows({});
    ASSERT_FALSE(rows.empty());
    ASSERT_TRUE(rows.front().isHeader);

    std::vector<juce::String> headers;
    ShortcutCategory current = ShortcutCategory::General;
    for (const auto& row : rows) {
        if (row.isHeader) {
            headers.push_back(row.label);
            continue;
        }
        if (row.isContinuous) // FRO236: the Continuous group is not a ShortcutCategory
            continue;
        current = ShortcutManager::getCategory(row.actionId);
        ASSERT_FALSE(headers.empty());
        EXPECT_EQ(headers.back(), ShortcutManager::getCategoryName(current))
            << row.actionId << " sits under its own header";
    }

    // Headers appear in getCategoryOrder() order (skipping any category with nothing invokable),
    // with "Continuous" (FRO236) appended last.
    std::vector<juce::String> expectedOrder;
    for (const auto category : ShortcutManager::getCategoryOrder())
        if (std::find(headers.begin(), headers.end(), ShortcutManager::getCategoryName(category)) != headers.end())
            expectedOrder.push_back(ShortcutManager::getCategoryName(category));
    expectedOrder.push_back("Continuous");
    EXPECT_EQ(headers, expectedOrder);
    EXPECT_EQ(headers.front(), "General");
    EXPECT_EQ(headers.back(), "Continuous");
}

TEST(MidiRemoteActionPickerTest, SearchFiltersByDescriptionCaseInsensitivelyAndDropsEmptyGroups) {
    const auto all = buildActionPickerRows({});
    const auto filtered = buildActionPickerRows("PLAY");
    ASSERT_FALSE(filtered.empty());
    EXPECT_LT(filtered.size(), all.size());
    for (const auto& row : filtered)
        if (!row.isHeader)
            EXPECT_TRUE(row.label.containsIgnoreCase("play")) << row.label;
    // No header without an action under it.
    for (size_t i = 0; i < filtered.size(); ++i)
        if (filtered[i].isHeader) {
            ASSERT_LT(i + 1, filtered.size());
            EXPECT_FALSE(filtered[i + 1].isHeader);
        }
    EXPECT_TRUE(buildActionPickerRows("zzzz-no-such-action").empty());
    EXPECT_EQ(buildActionPickerRows("  ").size(), all.size()) << "a blank search shows everything";
}

// FRO271: the six cursor/loop actions reach the picker on their own (it walks the category table and
// requires a command mapping), and the play/stop toggle carries the "Play / Stop" label so a search for
// "play" or "stop" finds it next to Play and Stop.
TEST(MidiRemoteActionPickerTest, ListsTheCursorAndLoopActionsAndTheRenamedPlayStopToggle) {
    const auto rows = buildActionPickerRows({});
    auto labelOf = [&](const juce::String& id) {
        for (const auto& row : rows)
            if (!row.isHeader && row.actionId == id)
                return row.label;
        return juce::String();
    };
    EXPECT_EQ(labelOf("transportNudgeBackBeat"), "Move Cursor Back (Beat)");
    EXPECT_EQ(labelOf("transportNudgeForwardBeat"), "Move Cursor Forward (Beat)");
    EXPECT_EQ(labelOf("transportNudgeBackBar"), "Move Cursor Back (Bar)");
    EXPECT_EQ(labelOf("transportNudgeForwardBar"), "Move Cursor Forward (Bar)");
    EXPECT_EQ(labelOf("transportJumpToLoopStart"), "Jump to Loop Start");
    EXPECT_EQ(labelOf("transportJumpToLoopEnd"), "Jump to Loop End");
    EXPECT_EQ(labelOf("togglePlayback"), "Play / Stop");
    // FRO278: selection stepping.
    EXPECT_EQ(labelOf("selectNextModule"), "Select Next Module");
    EXPECT_EQ(labelOf("selectPreviousModule"), "Select Previous Module");
    EXPECT_EQ(labelOf("selectNextTrack"), "Select Next Track");
    EXPECT_EQ(labelOf("selectPreviousTrack"), "Select Previous Track");
    // The alias is not a registered action of its own, but reads identically wherever it is named.
    EXPECT_EQ(ShortcutManager::getActionDescription("transportTogglePlayStop"), "Play / Stop");

    for (const char* query : {"play", "stop"}) {
        bool foundToggle = false, foundVerb = false;
        for (const auto& row : buildActionPickerRows(query)) {
            foundToggle = foundToggle || (!row.isHeader && row.label == "Play / Stop");
            foundVerb = foundVerb || (!row.isHeader && row.label.equalsIgnoreCase(query));
        }
        EXPECT_TRUE(foundToggle) << query;
        EXPECT_TRUE(foundVerb) << query;
    }
}

// FRO236 (docs/control/midi-remote.md#continuous-targets).
TEST(MidiRemoteActionPickerTest, ListsTheContinuousGroupWithExactlyThreeRows) {
    const auto rows = buildActionPickerRows({});
    std::vector<juce::String> continuousLabels;
    bool sawContinuousHeader = false;
    for (const auto& row : rows) {
        if (row.isHeader) {
            sawContinuousHeader = sawContinuousHeader || row.label == "Continuous";
            continue;
        }
        if (row.isContinuous)
            continuousLabels.push_back(row.label);
    }
    EXPECT_TRUE(sawContinuousHeader);
    ASSERT_EQ(continuousLabels.size(), 3u);
    EXPECT_EQ(continuousLabels[0], "Tempo (BPM)");
    EXPECT_EQ(continuousLabels[1], "Playhead Position");
    EXPECT_EQ(continuousLabels[2], "Master Volume");
}

TEST(MidiRemoteActionPickerTest, ChoosingAContinuousRowFiresOnContinuousChosenWithTheRightKind) {
    ActionPickerComponent picker;
    std::optional<synth::ContinuousTargetKind> chosen;
    picker.onContinuousChosen = [&](synth::ContinuousTargetKind kind) { chosen = kind; };

    int masterVolumeRow = -1;
    for (int i = 0; i < picker.getRowCountForTest(); ++i)
        if (picker.getRowForTest(i).isContinuous && picker.getRowForTest(i).label == "Master Volume")
            masterVolumeRow = i;
    ASSERT_GE(masterVolumeRow, 0);

    picker.chooseRowForTest(masterVolumeRow);
    ASSERT_TRUE(chosen.has_value());
    EXPECT_EQ(*chosen, synth::ContinuousTargetKind::masterVolume);
}

TEST(MidiRemoteActionPickerTest, ChoosingAnActionRowFiresOnChosenAndAHeaderDoesNot) {
    ActionPickerComponent picker;
    juce::String chosen;
    picker.onChosen = [&](const juce::String& id) { chosen = id; };
    ASSERT_GT(picker.getRowCountForTest(), 2);
    ASSERT_TRUE(picker.getRowForTest(0).isHeader);

    picker.chooseRowForTest(0);
    EXPECT_TRUE(chosen.isEmpty());

    picker.chooseRowForTest(1);
    EXPECT_EQ(chosen, picker.getRowForTest(1).actionId);

    picker.setFilter("no-such-action-anywhere");
    EXPECT_EQ(picker.getRowCountForTest(), 0);
    chosen.clear();
    picker.chooseRowForTest(0);
    EXPECT_TRUE(chosen.isEmpty());
}
