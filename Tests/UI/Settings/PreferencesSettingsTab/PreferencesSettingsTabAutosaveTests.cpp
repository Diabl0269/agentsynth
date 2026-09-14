#include "PreferencesSettingsTabTestFixture.h"

// Topic: autosave enabled/interval/backup-count preferences (P8-4).

// ---------------------------------------------------------------------------
// P8-4: autosave enabled/interval preferences.
// ---------------------------------------------------------------------------

TEST_F(PreferencesSettingsTabTest, AutosaveDefaultsToOnAtTwoMinutesWithFiveBackupsAndDoesNotWriteUntouched) {
    PreferencesSettingsTab tab(appProperties);
    EXPECT_TRUE(tab.isAutosaveEnabled()) << "a safety net, not an opt-in";
    EXPECT_EQ(tab.getAutosaveIntervalMinutes(), 2);
    EXPECT_EQ(tab.getAutosaveBackupCount(), 5);
    // Reading the defaults must not WRITE them — an untouched preference stays absent from the file,
    // same discipline every other row in this tab follows.
    EXPECT_FALSE(appProperties.getUserSettings()->containsKey("autosaveEnabled"));
    EXPECT_FALSE(appProperties.getUserSettings()->containsKey("autosaveIntervalMinutes"));
    EXPECT_FALSE(appProperties.getUserSettings()->containsKey("autosaveBackupCount"));
}

TEST_F(PreferencesSettingsTabTest, AutosaveEnabledIntervalAndBackupCountRoundTrip) {
    {
        PreferencesSettingsTab tab(appProperties);
        tab.setAutosaveEnabled(false);
        // An exact value outside the old fixed-choice combo's 1/2/5/10 set — proves this is now a
        // free-entry field, not a preset list.
        tab.setAutosaveIntervalMinutes(47);
        tab.setAutosaveBackupCount(13);
        EXPECT_FALSE(tab.isAutosaveEnabled());
        EXPECT_EQ(tab.getAutosaveIntervalMinutes(), 47);
        EXPECT_EQ(tab.getAutosaveBackupCount(), 13);
        EXPECT_EQ(appProperties.getUserSettings()->getValue("autosaveEnabled"), "0");
        EXPECT_EQ(appProperties.getUserSettings()->getIntValue("autosaveIntervalMinutes"), 47);
        EXPECT_EQ(appProperties.getUserSettings()->getIntValue("autosaveBackupCount"), 13);
    }

    // A fresh tab restores what was written — the same key MainComponent::maybeAutosave/
    // performAutosave read directly (no live push from this tab, same "read at use time" idiom as
    // the natural-scrolling preference documents).
    {
        PreferencesSettingsTab tab(appProperties);
        EXPECT_FALSE(tab.isAutosaveEnabled());
        EXPECT_EQ(tab.getAutosaveIntervalMinutes(), 47);
        EXPECT_EQ(tab.getAutosaveBackupCount(), 13);
    }
}

TEST_F(PreferencesSettingsTabTest, LoadsPersistedAutosaveValues) {
    appProperties.getUserSettings()->setValue("autosaveEnabled", "0");
    appProperties.getUserSettings()->setValue("autosaveIntervalMinutes", 10);
    appProperties.getUserSettings()->setValue("autosaveBackupCount", 20);

    PreferencesSettingsTab tab(appProperties);
    EXPECT_FALSE(tab.isAutosaveEnabled());
    EXPECT_EQ(tab.getAutosaveIntervalMinutes(), 10);
    EXPECT_EQ(tab.getAutosaveBackupCount(), 20);
}

TEST_F(PreferencesSettingsTabTest, SettingAutosaveBackupCountToZeroDisablesTheHistory) {
    PreferencesSettingsTab tab(appProperties);
    tab.setAutosaveBackupCount(0);
    EXPECT_EQ(tab.getAutosaveBackupCount(), 0);
    EXPECT_EQ(appProperties.getUserSettings()->getIntValue("autosaveBackupCount"), 0);
}

TEST_F(PreferencesSettingsTabTest, ClickingTheAutosaveToggleWritesTheSetting) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(500, 700);
    ASSERT_TRUE(tab.isAutosaveEnabled());

    tab.setAutosaveEnabled(false);
    EXPECT_EQ(appProperties.getUserSettings()->getValue("autosaveEnabled"), "0");
    tab.setAutosaveEnabled(true);
    EXPECT_EQ(appProperties.getUserSettings()->getValue("autosaveEnabled"), "1");
}
