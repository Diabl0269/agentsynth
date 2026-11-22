// Concern: Loop's key moved from the timeline surface action "timelineToggleLoop" onto the General command
// "transportToggleLoop" (bare L by default); the one-shot migration (migrateLoopKeyToTransport) carries an install's
// saved key across and drops the old id from the settings file.
#include "../../TestSettingsHelpers.h"
#include "ShortcutManager/ShortcutManager.h"
#include <gtest/gtest.h>

namespace {

const juce::KeyPress kBareL('l', juce::ModifierKeys::noModifiers, 0);
const juce::KeyPress kCtrlL('l', juce::ModifierKeys::ctrlModifier, 0);
const juce::KeyPress kBareK('k', juce::ModifierKeys::noModifiers, 0);

const juce::StringArray kTouchedKeys{"shortcut_timelineToggleLoop", "shortcut_transportToggleLoop"};

void seed(const juce::String& oldSurfaceKey, const juce::String& commandKey) {
    juce::ApplicationProperties props;
    props.setStorageParameters(synth::test::userSettingsTestOptions());
    auto* settings = props.getUserSettings();
    ASSERT_NE(settings, nullptr);
    settings->setValue("shortcut_timelineToggleLoop", oldSurfaceKey);
    settings->setValue("shortcut_transportToggleLoop", commandKey);
    settings->saveIfNeeded();
}

juce::KeyPress loadLoopKey() {
    ShortcutManager manager;
    juce::ApplicationProperties props;
    props.setStorageParameters(synth::test::userSettingsTestOptions());
    manager.loadFromProperties(props);
    return manager.getBinding("transportToggleLoop");
}

bool oldIdStillSaved() {
    juce::ApplicationProperties props;
    props.setStorageParameters(synth::test::userSettingsTestOptions());
    return props.getUserSettings()->containsKey("shortcut_timelineToggleLoop");
}

} // namespace

// Regression test for AGE182: a saved install held Loop on the timeline-only surface key and the command unbound.
TEST(ShortcutManagerLoopKeyMigrationTests, ASavedBareLMovesOntoTheTransportCommandAndPersists) {
    synth::test::PersistedKeysGuard guard(kTouchedKeys);
    seed(ShortcutManager::encodeKeyPress(kBareL), ShortcutManager::encodeKeyPress({}));
    EXPECT_EQ(loadLoopKey(), kBareL);
    EXPECT_FALSE(oldIdStillSaved());
    EXPECT_EQ(loadLoopKey(), kBareL) << "the second launch reads the persisted key";
}

TEST(ShortcutManagerLoopKeyMigrationTests, AUserRebindOfTheOldKeyComesAlong) {
    synth::test::PersistedKeysGuard guard(kTouchedKeys);
    seed(ShortcutManager::encodeKeyPress(kCtrlL), ShortcutManager::encodeKeyPress({}));
    EXPECT_EQ(loadLoopKey(), kCtrlL);
}

TEST(ShortcutManagerLoopKeyMigrationTests, AClearedOldKeyLeavesLoopUnbound) {
    synth::test::PersistedKeysGuard guard(kTouchedKeys);
    seed(ShortcutManager::encodeKeyPress({}), ShortcutManager::encodeKeyPress({}));
    EXPECT_FALSE(loadLoopKey().isValid()) << "the user had cleared Loop's key";
}

TEST(ShortcutManagerLoopKeyMigrationTests, AKeyTheUserGaveTheCommandItselfStays) {
    synth::test::PersistedKeysGuard guard(kTouchedKeys);
    seed(ShortcutManager::encodeKeyPress(kBareL), ShortcutManager::encodeKeyPress(kBareK));
    EXPECT_EQ(loadLoopKey(), kBareK);
    EXPECT_FALSE(oldIdStillSaved());
}
