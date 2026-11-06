// Concern: Cmd+K opens the command palette, the minimap toggle moved to Opt+Cmd+K, and the one-shot migration
// (ShortcutManager::migrateMinimapOptCmdK) moves an install's saved Cmd+K minimap binding across.
#include "../../TestSettingsHelpers.h"
#include "ShortcutManager/AppCommands.h"
#include "ShortcutManager/ShortcutManager.h"
#include <gtest/gtest.h>

namespace {

const juce::KeyPress kCmdK('k', juce::ModifierKeys::commandModifier, 0);
const juce::KeyPress kOptCmdK('k', juce::ModifierKeys::commandModifier | juce::ModifierKeys::altModifier, 0);
const juce::KeyPress kCmd9('9', juce::ModifierKeys::commandModifier, 0);

const juce::StringArray kTouchedKeys{"shortcutMigration_minimapOptCmdK", "shortcut_toggleMinimap",
                                     "shortcut_openCommandPalette", "shortcut_openSettings"};

void seedMinimap(const juce::KeyPress& saved, bool migrated) {
    juce::ApplicationProperties props;
    props.setStorageParameters(synth::test::userSettingsTestOptions());
    auto* settings = props.getUserSettings();
    ASSERT_NE(settings, nullptr);
    settings->setValue("shortcutMigration_minimapOptCmdK", migrated);
    settings->setValue("shortcut_toggleMinimap", ShortcutManager::encodeKeyPress(saved));
    settings->saveIfNeeded();
}

struct Loaded {
    juce::KeyPress minimap, palette;
};

Loaded load() {
    ShortcutManager manager;
    juce::ApplicationProperties props;
    props.setStorageParameters(synth::test::userSettingsTestOptions());
    manager.loadFromProperties(props);
    return {manager.getBinding("toggleMinimap"), manager.getBinding("openCommandPalette")};
}

} // namespace

TEST(ShortcutManagerCommandPaletteKeyTests, CmdKOpensThePaletteAndTheMinimapTakesOptCmdK) {
    ShortcutManager manager;
    EXPECT_EQ(manager.getBinding("openCommandPalette"), kCmdK);
    EXPECT_EQ(manager.getBinding("toggleMinimap"), kOptCmdK);
    EXPECT_EQ(AppCommands::getCommandForAction("openCommandPalette"), AppCommands::openCommandPalette);
    EXPECT_EQ(ShortcutManager::getActionDescription("openCommandPalette"), "Search Everything");
    EXPECT_EQ(manager.getActionsForKeyPress(kCmdK), juce::StringArray{"openCommandPalette"});
    EXPECT_EQ(manager.getActionsForKeyPress(kOptCmdK), juce::StringArray{"toggleMinimap"});
}

// Regression test for FRO733: an install that saved its bindings kept the minimap on Cmd+K, shadowing the palette.
TEST(ShortcutManagerCommandPaletteKeyTests, ASavedCmdKMinimapMovesToOptCmdKOnceAndPersists) {
    synth::test::PersistedKeysGuard guard(kTouchedKeys);
    seedMinimap(kCmdK, false);
    const auto first = load();
    EXPECT_EQ(first.minimap, kOptCmdK);
    EXPECT_EQ(first.palette, kCmdK);
    EXPECT_EQ(load().minimap, kOptCmdK) << "the second launch reads the persisted Opt+Cmd+K";
}

TEST(ShortcutManagerCommandPaletteKeyTests, AUserRebindStaysAndTheMigrationNeverFiresTwice) {
    synth::test::PersistedKeysGuard guard(kTouchedKeys);
    seedMinimap(kCmd9, false);
    EXPECT_EQ(load().minimap, kCmd9) << "the user's own key survives";

    seedMinimap(kCmdK, true); // already migrated: a deliberate Cmd+K is the user's choice
    EXPECT_EQ(load().minimap, kCmdK);
}

TEST(ShortcutManagerCommandPaletteKeyTests, TheMigrationLeavesTheMinimapAloneWhenAnotherActionHoldsOptCmdK) {
    synth::test::PersistedKeysGuard guard(kTouchedKeys);
    seedMinimap(kCmdK, false);
    {
        juce::ApplicationProperties props;
        props.setStorageParameters(synth::test::userSettingsTestOptions());
        auto* settings = props.getUserSettings();
        settings->setValue("shortcut_openSettings", ShortcutManager::encodeKeyPress(kOptCmdK));
        settings->saveIfNeeded();
    }
    EXPECT_EQ(load().minimap, kCmdK) << "never two actions on one chord";
}
