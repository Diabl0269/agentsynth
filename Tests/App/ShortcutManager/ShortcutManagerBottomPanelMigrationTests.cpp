// Concern: the one-shot migration (ShortcutManager::migrateBottomPanelToggleKeys) that moves
// an existing install's Cmd+T off toggleTimelinePanel and onto the new toggleBottomPanel action,
// giving toggleTimelinePanel its own new Cmd+1 default -- same shape as migrateSaveAsChordSwap.
#include "../../TestSettingsHelpers.h"
#include "ShortcutManager/ShortcutManager.h"
#include <gtest/gtest.h>

namespace {

const juce::KeyPress kCmdT('t', juce::ModifierKeys::commandModifier, 0);
const juce::KeyPress kCmd1('1', juce::ModifierKeys::commandModifier, 0);
const juce::KeyPress kCmd9('9', juce::ModifierKeys::commandModifier, 0);

// Every key this test's seeded/migrated settings could touch, restored exactly on scope exit --
// same PersistedKeysGuard idiom TestSettingsHelpers.h documents for any test that reaches the real
// on-disk settings file.
const juce::StringArray kTouchedKeys{"shortcutMigration_bottomPanelCmdT", "shortcut_toggleTimelinePanel",
                                     "shortcut_toggleBottomPanel", "shortcut_toggleMixerPanel"};

} // namespace

TEST(ShortcutManagerBottomPanelMigrationTests, AnOldCmdTOnTimelineMovesToTheNewBottomPanelToggle) {
    synth::test::PersistedKeysGuard guard(kTouchedKeys);
    {
        juce::ApplicationProperties props;
        props.setStorageParameters(synth::test::userSettingsTestOptions());
        auto* settings = props.getUserSettings();
        ASSERT_NE(settings, nullptr);
        settings->removeValue("shortcutMigration_bottomPanelCmdT");
        settings->setValue("shortcut_toggleTimelinePanel", ShortcutManager::encodeKeyPress(kCmdT));
        settings->removeValue("shortcut_toggleBottomPanel"); // absent, like every legacy install
        settings->saveIfNeeded();
    }

    ShortcutManager manager;
    juce::ApplicationProperties props;
    props.setStorageParameters(synth::test::userSettingsTestOptions());
    manager.loadFromProperties(props);

    EXPECT_EQ(manager.getBinding("toggleBottomPanel"), kCmdT) << "claims the old Cmd+T";
    EXPECT_EQ(manager.getBinding("toggleTimelinePanel"), kCmd1) << "moved off Cmd+T onto its own new default";
}

TEST(ShortcutManagerBottomPanelMigrationTests, ARebindAwayFromCmdTIsLeftAlone) {
    synth::test::PersistedKeysGuard guard(kTouchedKeys);
    {
        juce::ApplicationProperties props;
        props.setStorageParameters(synth::test::userSettingsTestOptions());
        auto* settings = props.getUserSettings();
        ASSERT_NE(settings, nullptr);
        settings->removeValue("shortcutMigration_bottomPanelCmdT");
        // A user who rebound "Show Timeline Tab" away from Cmd+T before ever upgrading.
        settings->setValue("shortcut_toggleTimelinePanel", ShortcutManager::encodeKeyPress(kCmd9));
        settings->removeValue("shortcut_toggleBottomPanel");
        settings->saveIfNeeded();
    }

    ShortcutManager manager;
    juce::ApplicationProperties props;
    props.setStorageParameters(synth::test::userSettingsTestOptions());
    manager.loadFromProperties(props);

    EXPECT_EQ(manager.getBinding("toggleTimelinePanel"), kCmd9) << "the user's own rebind must survive";
    EXPECT_EQ(manager.getBinding("toggleBottomPanel"), kCmdT) << "still gets its own fresh Cmd+T default";
}

TEST(ShortcutManagerBottomPanelMigrationTests, RunsOnlyOnce) {
    synth::test::PersistedKeysGuard guard(kTouchedKeys);
    {
        juce::ApplicationProperties props;
        props.setStorageParameters(synth::test::userSettingsTestOptions());
        auto* settings = props.getUserSettings();
        ASSERT_NE(settings, nullptr);
        settings->setValue("shortcutMigration_bottomPanelCmdT", true); // already migrated once
        settings->setValue("shortcut_toggleTimelinePanel", ShortcutManager::encodeKeyPress(kCmdT));
        settings->removeValue("shortcut_toggleBottomPanel");
        settings->saveIfNeeded();
    }

    ShortcutManager manager;
    juce::ApplicationProperties props;
    props.setStorageParameters(synth::test::userSettingsTestOptions());
    manager.loadFromProperties(props);

    // The flag says this already ran (or was never needed) -- a fresh Cmd+T default for
    // toggleBottomPanel, still colliding with toggleTimelinePanel's own leftover Cmd+T, is exactly
    // what "don't re-run" means here; this pins that the migration does not fire a second time.
    EXPECT_EQ(manager.getBinding("toggleTimelinePanel"), kCmdT);
}
