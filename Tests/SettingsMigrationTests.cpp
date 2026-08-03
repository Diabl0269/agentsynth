#include "../Source/SettingsMigration.h"
#include <gtest/gtest.h>
#include <juce_core/juce_core.h>

namespace {

class SettingsMigrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        parentDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                        .getChildFile("SettingsMigrationTest")
                        .getNonexistentChildFile("run", "", false);
        parentDir.createDirectory();
    }

    void TearDown() override { parentDir.deleteRecursively(); }

    static juce::File writeFile(const juce::File& folder, const juce::String& relativePath,
                                const juce::String& contents) {
        auto file = folder.getChildFile(relativePath);
        file.getParentDirectory().createDirectory();
        file.replaceWithText(contents);
        return file;
    }

    juce::File parentDir;
};

} // namespace

TEST_F(SettingsMigrationTest, MigratesFromLegacyFolderWhenCurrentAbsent) {
    writeFile(parentDir.getChildFile("OldName"), "settings.xml", "hello");

    auto result = synth::migrateUserData(parentDir, "NewName", {"OldName"});

    EXPECT_TRUE(result.migrated);
    EXPECT_EQ(result.fromName, "OldName");
    EXPECT_EQ(result.filesCopied, 1);

    auto copied = parentDir.getChildFile("NewName").getChildFile("settings.xml");
    EXPECT_TRUE(copied.existsAsFile());
    EXPECT_EQ(copied.loadFileAsString(), "hello");
}

TEST_F(SettingsMigrationTest, DoesNotOverwriteExistingCurrentFolder) {
    writeFile(parentDir.getChildFile("OldName"), "settings.xml", "legacy");
    writeFile(parentDir.getChildFile("NewName"), "settings.xml", "current");

    auto result = synth::migrateUserData(parentDir, "NewName", {"OldName"});

    EXPECT_FALSE(result.migrated);
    EXPECT_EQ(result.filesCopied, 0);

    auto current = parentDir.getChildFile("NewName").getChildFile("settings.xml");
    EXPECT_EQ(current.loadFileAsString(), "current");
}

TEST_F(SettingsMigrationTest, IsIdempotent) {
    writeFile(parentDir.getChildFile("OldName"), "settings.xml", "hello");

    auto first = synth::migrateUserData(parentDir, "NewName", {"OldName"});
    ASSERT_TRUE(first.migrated);

    auto second = synth::migrateUserData(parentDir, "NewName", {"OldName"});
    EXPECT_FALSE(second.migrated);
    EXPECT_EQ(second.filesCopied, 0);

    auto current = parentDir.getChildFile("NewName").getChildFile("settings.xml");
    EXPECT_EQ(current.loadFileAsString(), "hello");
}

TEST_F(SettingsMigrationTest, NoLegacyFolderIsNoOp) {
    auto result = synth::migrateUserData(parentDir, "NewName", {"OldName", "OlderName"});

    EXPECT_FALSE(result.migrated);
    EXPECT_EQ(result.filesCopied, 0);
    EXPECT_FALSE(parentDir.getChildFile("NewName").exists());
}

TEST_F(SettingsMigrationTest, PicksNewestLegacyWhenSeveralExist) {
    writeFile(parentDir.getChildFile("Newer"), "settings.xml", "newer-data");
    writeFile(parentDir.getChildFile("Older"), "settings.xml", "older-data");

    auto result = synth::migrateUserData(parentDir, "Current", {"Newer", "Older"});

    EXPECT_TRUE(result.migrated);
    EXPECT_EQ(result.fromName, "Newer");

    auto copied = parentDir.getChildFile("Current").getChildFile("settings.xml");
    EXPECT_EQ(copied.loadFileAsString(), "newer-data");
}

TEST_F(SettingsMigrationTest, CopiesNestedSubdirectories) {
    writeFile(parentDir.getChildFile("OldName"), "settings.xml", "settings");
    writeFile(parentDir.getChildFile("OldName"), "Presets/lead.gpreset", "preset-data");
    writeFile(parentDir.getChildFile("OldName"), "Presets/Bass/deep.gpreset", "deep-preset-data");

    auto result = synth::migrateUserData(parentDir, "NewName", {"OldName"});

    EXPECT_TRUE(result.migrated);
    EXPECT_EQ(result.filesCopied, 3);

    auto newDir = parentDir.getChildFile("NewName");
    EXPECT_EQ(newDir.getChildFile("Presets/lead.gpreset").loadFileAsString(), "preset-data");
    EXPECT_EQ(newDir.getChildFile("Presets/Bass/deep.gpreset").loadFileAsString(), "deep-preset-data");
}

TEST_F(SettingsMigrationTest, LeavesLegacyFolderIntact) {
    writeFile(parentDir.getChildFile("OldName"), "settings.xml", "hello");
    writeFile(parentDir.getChildFile("OldName"), "Presets/lead.gpreset", "preset-data");

    synth::migrateUserData(parentDir, "NewName", {"OldName"});

    auto legacyDir = parentDir.getChildFile("OldName");
    EXPECT_EQ(legacyDir.getChildFile("settings.xml").loadFileAsString(), "hello");
    EXPECT_EQ(legacyDir.getChildFile("Presets/lead.gpreset").loadFileAsString(), "preset-data");
}
