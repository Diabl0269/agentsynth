// ActionPickerTests.cpp -- FRO135 (docs/control/midi-remote-ui.md#assign-from-the-panel-control-first-learn):
// the "Choose an action" list -- command-dispatched actions only, grouped by ShortcutCategory in the
// Shortcuts tab's order, named by ShortcutManager::getActionDescription, searchable. Suite name contains
// "MidiRemote" per the ship-task --gtest_filter convention.

#include "ShortcutManager/AppCommands.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/MidiRemote/ActionPicker/ActionPickerComponent.h"

#include <gtest/gtest.h>

using synth::ui::ActionPickerComponent;
using synth::ui::ActionPickerRow;
using synth::ui::buildActionPickerRows;

TEST(MidiRemoteActionPickerTest, ListsOnlyCommandDispatchedActions) {
    const auto rows = buildActionPickerRows({});
    int actions = 0;
    for (const auto& row : rows) {
        if (row.isHeader)
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
        current = ShortcutManager::getCategory(row.actionId);
        ASSERT_FALSE(headers.empty());
        EXPECT_EQ(headers.back(), ShortcutManager::getCategoryName(current))
            << row.actionId << " sits under its own header";
    }

    // Headers appear in getCategoryOrder() order (skipping any category with nothing invokable).
    std::vector<juce::String> expectedOrder;
    for (const auto category : ShortcutManager::getCategoryOrder())
        if (std::find(headers.begin(), headers.end(), ShortcutManager::getCategoryName(category)) != headers.end())
            expectedOrder.push_back(ShortcutManager::getCategoryName(category));
    EXPECT_EQ(headers, expectedOrder);
    EXPECT_EQ(headers.front(), "General");
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
