// Concern: the tabPrevious / tabNext actions as table rows -- ids, labels, default chords, category,
// command mapping, and that no other binding shares their chords.
#include "ShortcutManager/AppCommands.h"
#include "ShortcutManagerTestFixture.h"
#include <gtest/gtest.h>

namespace {
constexpr int kCmdAlt = juce::ModifierKeys::commandModifier | juce::ModifierKeys::altModifier;

juce::KeyPress cmdAlt(int keyCode) { return juce::KeyPress(keyCode, juce::ModifierKeys(kCmdAlt), 0); }
} // namespace

TEST_F(ShortcutManagerTest, TabActionsAreRegisteredInGeneralWithTheAgreedLabelsAndCommands) {
    for (const auto* id : {"tabPrevious", "tabNext"}) {
        EXPECT_TRUE(manager.getActionIds().contains(id)) << id;
        EXPECT_EQ(ShortcutManager::getCategory(id), ShortcutCategory::General) << id;
        EXPECT_TRUE(ShortcutManager::getActionIdsInCategory(ShortcutCategory::General).contains(id)) << id;
        EXPECT_NE(AppCommands::getCommandForAction(id), AppCommands::kNoCommand) << id;
    }
    EXPECT_EQ(ShortcutManager::getActionDescription("tabPrevious"), "Previous Tab");
    EXPECT_EQ(ShortcutManager::getActionDescription("tabNext"), "Next Tab");
    EXPECT_EQ(AppCommands::getCommandForAction("tabPrevious"), AppCommands::tabPrevious);
    EXPECT_EQ(AppCommands::getCommandForAction("tabNext"), AppCommands::tabNext);
}

TEST_F(ShortcutManagerTest, TabActionsDefaultToCommandOptionLeftAndRight) {
    EXPECT_EQ(manager.getBinding("tabPrevious"), cmdAlt(juce::KeyPress::leftKey));
    EXPECT_EQ(manager.getBinding("tabNext"), cmdAlt(juce::KeyPress::rightKey));
}

// Cmd+Option+arrow is its own exact chord: the Option-only arrows (card and clip moves, piano roll note
// navigation) and every other binding keep their keys, and the tab chords belong to the tab actions alone.
TEST_F(ShortcutManagerTest, TabChordsCollideWithNoOtherAction) {
    EXPECT_EQ(manager.getActionsForKeyPress(cmdAlt(juce::KeyPress::leftKey)), juce::StringArray{"tabPrevious"});
    EXPECT_EQ(manager.getActionsForKeyPress(cmdAlt(juce::KeyPress::rightKey)), juce::StringArray{"tabNext"});

    const auto altOnly = [](int keyCode) { return juce::KeyPress(keyCode, juce::ModifierKeys::altModifier, 0); };
    EXPECT_FALSE(manager.getActionsForKeyPress(altOnly(juce::KeyPress::leftKey)).contains("tabPrevious"));
    EXPECT_FALSE(manager.getActionsForKeyPress(altOnly(juce::KeyPress::rightKey)).contains("tabNext"));
    EXPECT_TRUE(manager.getActionsForKeyPress(altOnly(juce::KeyPress::leftKey)).contains("canvasMoveCardLeft"));
}

TEST_F(ShortcutManagerTest, RebindingATabActionMovesItsChord) {
    manager.setBinding("tabNext", juce::KeyPress('j', juce::ModifierKeys::commandModifier, 0));
    EXPECT_TRUE(manager.getActionsForKeyPress(cmdAlt(juce::KeyPress::rightKey)).isEmpty());
    EXPECT_EQ(manager.getActionsForKeyPress(juce::KeyPress('j', juce::ModifierKeys::commandModifier, 0)),
              juce::StringArray{"tabNext"});
}
