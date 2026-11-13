// Concern: Add Track defaults to Cmd+T on the Mac (the physical Ctrl+T belongs to the point editors' "insert a
// point"), and the one-shot migration (ShortcutManager::migrateAddTrackCmdT) moves an install's saved Ctrl+T there.
#include "../../TestSettingsHelpers.h"
#include "ShortcutManager/ShortcutManager.h"
#include <gtest/gtest.h>

namespace {

const juce::KeyPress kCmdT('t', juce::ModifierKeys::commandModifier, 0);
const juce::KeyPress kCtrlT('t', juce::ModifierKeys::ctrlModifier, 0);
const juce::KeyPress kCtrlAltT('t', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::altModifier, 0);
const juce::KeyPress kCmd9('9', juce::ModifierKeys::commandModifier, 0);

const juce::StringArray kTouchedKeys{"shortcutMigration_addTrackCmdT", "shortcut_openAddTrackMenu",
                                     "shortcut_toggleBottomPanel", "shortcutMigration_bottomPanelCmdT",
                                     "shortcutMigration_bottomPanelCmdJ"};

bool hostIsMac() { return ShortcutManager::hostDefaultsPlatform() == ShortcutManager::DefaultsPlatform::Mac; }

void seed(const juce::KeyPress& saved, bool migrated) {
    juce::ApplicationProperties props;
    props.setStorageParameters(synth::test::userSettingsTestOptions());
    auto* settings = props.getUserSettings();
    ASSERT_NE(settings, nullptr);
    settings->setValue("shortcutMigration_addTrackCmdT", migrated);
    settings->setValue("shortcut_openAddTrackMenu", ShortcutManager::encodeKeyPress(saved));
    settings->saveIfNeeded();
}

juce::KeyPress loadAddTrackKey() {
    ShortcutManager manager;
    juce::ApplicationProperties props;
    props.setStorageParameters(synth::test::userSettingsTestOptions());
    manager.loadFromProperties(props);
    return manager.getBinding("openAddTrackMenu");
}

} // namespace

TEST(ShortcutManagerAddTrackKeyTests, TheMacDefaultIsCmdTAndOffTheMacItStaysCtrlAltT) {
    ShortcutManager mac;
    mac.setDefaultsPlatform(ShortcutManager::DefaultsPlatform::Mac);
    EXPECT_EQ(mac.getBinding("openAddTrackMenu"), kCmdT);
    ShortcutManager other;
    other.setDefaultsPlatform(ShortcutManager::DefaultsPlatform::Other);
    EXPECT_EQ(other.getBinding("openAddTrackMenu"), kCtrlAltT);
}

TEST(ShortcutManagerAddTrackKeyTests, NoOtherMacDefaultHoldsCmdTOrTheCtrlTOfThePointEditors) {
    ShortcutManager mac;
    mac.setDefaultsPlatform(ShortcutManager::DefaultsPlatform::Mac);
    for (const auto& id : mac.getActionIds()) {
        if (id == "openAddTrackMenu")
            continue;
        EXPECT_NE(mac.getBinding(id), kCmdT) << id;
        EXPECT_NE(mac.getBinding(id), kCtrlT) << id;
    }
}

TEST(ShortcutManagerAddTrackKeyTests, ASavedCtrlTMovesToCmdTOnceAndPersists) {
    if (!hostIsMac())
        GTEST_SKIP() << "the Mac migration needs Cmd and Ctrl to be distinct keys";
    synth::test::PersistedKeysGuard guard(kTouchedKeys);
    seed(kCtrlT, false);
    EXPECT_EQ(loadAddTrackKey(), kCmdT);
    EXPECT_EQ(loadAddTrackKey(), kCmdT) << "the second launch reads the persisted Cmd+T";
}

TEST(ShortcutManagerAddTrackKeyTests, AUserRebindStaysAndTheMigrationNeverFiresTwice) {
    if (!hostIsMac())
        GTEST_SKIP() << "the Mac migration needs Cmd and Ctrl to be distinct keys";
    synth::test::PersistedKeysGuard guard(kTouchedKeys);
    seed(kCmd9, false);
    EXPECT_EQ(loadAddTrackKey(), kCmd9) << "the user's own key survives";

    seed(kCtrlT, true); // already migrated: a deliberate Ctrl+T is the user's choice
    EXPECT_EQ(loadAddTrackKey(), kCtrlT);
}

TEST(ShortcutManagerAddTrackKeyTests, TheMigrationLeavesCmdTAloneWhenAnotherActionHoldsIt) {
    if (!hostIsMac())
        GTEST_SKIP() << "the Mac migration needs Cmd and Ctrl to be distinct keys";
    synth::test::PersistedKeysGuard guard(kTouchedKeys);
    seed(kCtrlT, false);
    {
        juce::ApplicationProperties props;
        props.setStorageParameters(synth::test::userSettingsTestOptions());
        auto* settings = props.getUserSettings();
        settings->setValue("shortcutMigration_bottomPanelCmdJ", true); // keep the older migrations quiet
        settings->setValue("shortcutMigration_bottomPanelCmdT", true);
        settings->setValue("shortcut_toggleBottomPanel", ShortcutManager::encodeKeyPress(kCmdT));
        settings->saveIfNeeded();
    }
    EXPECT_EQ(loadAddTrackKey(), kCtrlT) << "never two actions on one chord";
}
