// TestSettingsIsolationTests.cpp
// The suite must never touch the developer's real settings folder: Tests/TestMain.cpp gives every run a
// private settings folder (AGENTSYNTH_SETTINGS_DIR, or a fresh temp folder when that is unset), and every
// on-disk store resolves through synth::userSettingsOptions() / userSettingsRootDirectory().

#include "Branding.h"
#include "UserSettings.h"
#include <gtest/gtest.h>

namespace {
// The real stores' root, and the real settings file's folder (macOS puts it under "Application Support").
juce::File realSettingsRoot() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile(synth::branding::kSettingsFolderName);
}
juce::File realSettingsFile() {
    auto options = synth::userSettingsOptions();
    options.folderName = synth::branding::kSettingsFolderName;
    return options.getDefaultFile();
}
} // namespace

// Regression test for FRO404: local runs overwrote the real recent-projects and plugin scan lists.
TEST(TestSettingsIsolation, SettingsRootIsNeverTheRealUserFolder) {
    const auto root = synth::userSettingsRootDirectory();
    EXPECT_NE(root, realSettingsRoot());
    EXPECT_FALSE(root.isAChildOf(realSettingsRoot()));
}

TEST(TestSettingsIsolation, SettingsFileLivesInsideTheRunsOwnFolder) {
    const auto settingsFile = synth::userSettingsOptions().getDefaultFile();
    EXPECT_TRUE(settingsFile.isAChildOf(synth::userSettingsRootDirectory()) ||
                settingsFile.getParentDirectory() == synth::userSettingsRootDirectory())
        << settingsFile.getFullPathName();
    EXPECT_NE(settingsFile, realSettingsFile()) << settingsFile.getFullPathName();
    EXPECT_FALSE(settingsFile.isAChildOf(realSettingsRoot())) << settingsFile.getFullPathName();
}
