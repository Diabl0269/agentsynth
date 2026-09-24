// Concern: FRO278's selection-stepping actions (selectNextModule/PreviousModule/NextTrack/PreviousTrack)
// as table rows -- ids, labels, unbound default, category, command mapping. Dispatch lives in
// Tests/App/FocusArbitration/FocusArbitrationSelectionStepTests.cpp.
#include "ShortcutManagerTestFixture.h"
#include <gtest/gtest.h>

namespace {

struct StepAction {
    const char* id;
    const char* label;
    juce::CommandID command;
};

const std::vector<StepAction>& stepActions() {
    static const std::vector<StepAction> actions{
        {"selectNextModule", "Select Next Module", AppCommands::selectNextModule},
        {"selectPreviousModule", "Select Previous Module", AppCommands::selectPreviousModule},
        {"selectNextTrack", "Select Next Track", AppCommands::selectNextTrack},
        {"selectPreviousTrack", "Select Previous Track", AppCommands::selectPreviousTrack},
    };
    return actions;
}

} // namespace

TEST_F(ShortcutManagerTest, SelectionStepActionsAreRegisteredUnboundGeneralWithTheAgreedLabels) {
    for (const auto& a : stepActions()) {
        EXPECT_TRUE(manager.getActionIds().contains(a.id)) << a.id;
        EXPECT_FALSE(manager.getBinding(a.id).isValid()) << a.id << " ships unbound";
        EXPECT_EQ(ShortcutManager::getActionDescription(a.id), juce::String(a.label));
        EXPECT_EQ(ShortcutManager::getCategory(a.id), ShortcutCategory::General) << a.id;
        EXPECT_TRUE(ShortcutManager::getActionIdsInCategory(ShortcutCategory::General).contains(a.id)) << a.id;
    }
}

TEST_F(ShortcutManagerTest, SelectionStepActionsRoundTripThroughTheirCommandId) {
    for (const auto& a : stepActions()) {
        const auto command = AppCommands::getCommandForAction(a.id);
        EXPECT_EQ(command, a.command) << a.id;
        EXPECT_NE(command, AppCommands::kNoCommand) << a.id;
    }
}
