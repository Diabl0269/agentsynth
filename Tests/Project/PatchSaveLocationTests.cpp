#include "PatchSaveLocation.h"
#include "ProjectBundle.h"
#include "UserSettings.h"
#include <gtest/gtest.h>

// The patch-dialog start folder: per project / shared / custom, and every fallback.
// See docs/architecture/project-bundle.md (patch save location).

using synth::PatchSaveLocation;
using synth::PatchSaveMode;

class PatchSaveLocationTest : public ::testing::Test {
protected:
    void SetUp() override {
        root = synth::userSettingsRootDirectory().getChildFile("agentsynth-patchsave-tests");
        root.deleteRecursively();
        root.createDirectory();
        projects = root.getChildFile("Projects");
        projects.createDirectory();
        bundle = root.getChildFile("Song.agsproj");
        bundle.createDirectory();
        bundle.getChildFile("project.json").replaceWithText("{}");
        ASSERT_TRUE(synth::ProjectBundle::isBundle(bundle));
    }
    void TearDown() override { root.deleteRecursively(); }

    juce::File root, projects, bundle;
};

TEST_F(PatchSaveLocationTest, GlobalIsPatchesUnderTheProjectsRootAndCreatedOnDemand) {
    const auto expected = projects.getChildFile("Patches");
    ASSERT_FALSE(expected.exists());
    bool fallback = true;
    EXPECT_EQ(PatchSaveLocation::resolve(PatchSaveMode::Global, bundle, {}, projects, &fallback), expected);
    EXPECT_TRUE(expected.isDirectory());
    EXPECT_FALSE(fallback) << "Global is the requested mode, not a fallback";
}

TEST_F(PatchSaveLocationTest, PerProjectUsesTheBundlesPatchesFolder) {
    bool fallback = true;
    const auto dir = PatchSaveLocation::resolve(PatchSaveMode::PerProject, bundle, {}, projects, &fallback);
    EXPECT_EQ(dir, bundle.getChildFile("Patches"));
    EXPECT_TRUE(dir.isDirectory());
    EXPECT_FALSE(fallback);
    EXPECT_FALSE(projects.getChildFile("Patches").exists()) << "the shared folder is not created needlessly";
}

TEST_F(PatchSaveLocationTest, PerProjectFallsBackToGlobalWithoutASavedBundle) {
    const auto global = projects.getChildFile("Patches");
    bool fallback = false;
    EXPECT_EQ(PatchSaveLocation::resolve(PatchSaveMode::PerProject, {}, {}, projects, &fallback), global);
    EXPECT_TRUE(fallback);

    fallback = false; // a directory that is not a bundle (no project.json) counts as unsaved
    EXPECT_EQ(
        PatchSaveLocation::resolve(PatchSaveMode::PerProject, root.getChildFile("Projects"), {}, projects, &fallback),
        global);
    EXPECT_TRUE(fallback);
}

TEST_F(PatchSaveLocationTest, CustomUsesTheChosenFolderAndFallsBackWhenItIsGoneOrUnset) {
    const auto mine = root.getChildFile("MyPatches");
    mine.createDirectory();
    bool fallback = true;
    EXPECT_EQ(PatchSaveLocation::resolve(PatchSaveMode::Custom, bundle, mine, projects, &fallback), mine);
    EXPECT_FALSE(fallback);

    const auto global = projects.getChildFile("Patches");
    mine.deleteRecursively();
    EXPECT_EQ(PatchSaveLocation::resolve(PatchSaveMode::Custom, bundle, mine, projects, &fallback), global);
    EXPECT_TRUE(fallback);
    fallback = false;
    EXPECT_EQ(PatchSaveLocation::resolve(PatchSaveMode::Custom, bundle, {}, projects, &fallback), global);
    EXPECT_TRUE(fallback);
}

TEST_F(PatchSaveLocationTest, ModeStringsRoundTripAndUnknownMeansPerProject) {
    for (auto mode : {PatchSaveMode::PerProject, PatchSaveMode::Global, PatchSaveMode::Custom})
        EXPECT_EQ(PatchSaveLocation::modeFromString(PatchSaveLocation::modeToString(mode)), mode);
    EXPECT_EQ(PatchSaveLocation::modeFromString(""), PatchSaveMode::PerProject);
    EXPECT_EQ(PatchSaveLocation::modeFromString("bogus"), PatchSaveMode::PerProject);
}

TEST_F(PatchSaveLocationTest, ResolveFromSettingsReadsModeAndCustomFolder) {
    juce::PropertiesFile::Options options;
    options.applicationName = "PatchSaveLocationTest";
    options.filenameSuffix = "test";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    juce::ApplicationProperties props;
    props.setStorageParameters(options);
    auto* settings = props.getUserSettings();
    settings->clear();

    // Nothing stored: per project, i.e. today's Export Patch Only behaviour inside a bundle.
    EXPECT_EQ(PatchSaveLocation::resolveFromSettings(settings, bundle, projects), bundle.getChildFile("Patches"));
    EXPECT_EQ(PatchSaveLocation::resolveFromSettings(nullptr, bundle, projects), bundle.getChildFile("Patches"));

    settings->setValue(PatchSaveLocation::kModeKey, "global");
    EXPECT_EQ(PatchSaveLocation::resolveFromSettings(settings, bundle, projects), projects.getChildFile("Patches"));

    const auto mine = root.getChildFile("MyPatches");
    mine.createDirectory();
    settings->setValue(PatchSaveLocation::kModeKey, "custom");
    settings->setValue(PatchSaveLocation::kCustomDirKey, mine.getFullPathName());
    EXPECT_EQ(PatchSaveLocation::resolveFromSettings(settings, bundle, projects), mine);
    settings->clear();
}
