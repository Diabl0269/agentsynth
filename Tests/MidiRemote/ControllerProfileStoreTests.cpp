// Tests for Source/MidiRemote/ControllerProfileStore.h (docs/midi_remote.md §7). Suite name
// deliberately contains "MidiRemote" so it matches the ship-task verification filter
// (--gtest_filter="*MidiRemote*"). Every test points a store at its own temp directory — never
// the real settings folder — so runs never collide with a concurrent test suite or a developer's
// own profiles.
#include "MidiRemote/ControllerProfileStore.h"
#include <gtest/gtest.h>

using synth::ControllerProfile;
using synth::ControllerProfileStore;

namespace {

ControllerProfile makeProfile(const juce::String& id, const juce::String& name) {
    ControllerProfile profile;
    profile.id = id;
    profile.name = name;
    profile.input.identifier = id + "-input";
    profile.input.name = name;
    profile.version = 1;
    return profile;
}

} // namespace

class MidiRemoteProfileStoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                   .getChildFile("agentsynth-controllerprofilestore-tests-" + juce::Uuid().toString());
        root.deleteRecursively();
    }
    void TearDown() override { root.deleteRecursively(); }

    juce::File root;
};

TEST_F(MidiRemoteProfileStoreTest, SaveThenLoadAllFindsTheProfile) {
    ControllerProfileStore store(root);
    EXPECT_TRUE(store.loadAll().profiles.empty()) << "a brand-new store has no profiles yet";

    const auto profile = makeProfile("profile-1", "Launchkey Mini MK3");
    ASSERT_TRUE(store.save(profile));

    const auto result = store.loadAll();
    EXPECT_TRUE(result.skippedFiles.empty());
    ASSERT_EQ(result.profiles.size(), 1u);
    EXPECT_EQ(result.profiles[0].id, profile.id);
    EXPECT_EQ(result.profiles[0].name, profile.name);
    EXPECT_TRUE(root.getChildFile(profile.id + ".json").existsAsFile());
}

TEST_F(MidiRemoteProfileStoreTest, SaveOverwritesAnExistingProfileWithTheSameId) {
    ControllerProfileStore store(root);
    auto profile = makeProfile("profile-1", "Original Name");
    ASSERT_TRUE(store.save(profile));

    profile.name = "Renamed";
    ASSERT_TRUE(store.save(profile));

    const auto result = store.loadAll();
    ASSERT_EQ(result.profiles.size(), 1u) << "the SAME id must overwrite, not duplicate";
    EXPECT_EQ(result.profiles[0].name, "Renamed");
}

TEST_F(MidiRemoteProfileStoreTest, SaveRejectsAnEmptyId) {
    ControllerProfileStore store(root);
    ControllerProfile profile = makeProfile("", "No Id");
    EXPECT_FALSE(store.save(profile));
}

TEST_F(MidiRemoteProfileStoreTest, LoadAllSkipsAMalformedFileButKeepsTheRest) {
    ControllerProfileStore store(root);
    ASSERT_TRUE(store.save(makeProfile("profile-good", "Good Controller")));

    root.createDirectory();
    root.getChildFile("corrupt.json").replaceWithText("{ this is not valid JSON");

    const auto result = store.loadAll();
    ASSERT_EQ(result.profiles.size(), 1u);
    EXPECT_EQ(result.profiles[0].id, "profile-good");
    ASSERT_EQ(result.skippedFiles.size(), 1u);
    EXPECT_EQ(result.skippedFiles[0], "corrupt.json");
}

TEST_F(MidiRemoteProfileStoreTest, ExportThenImportIntoAFreshStoreRoundTrips) {
    ControllerProfileStore sourceStore(root.getChildFile("source"));
    const auto profile = makeProfile("profile-export", "Exported Controller");
    ASSERT_TRUE(sourceStore.save(profile));

    const auto exportedFile = root.getChildFile("exported.json");
    ASSERT_TRUE(sourceStore.exportProfile(profile.id, exportedFile));
    ASSERT_TRUE(exportedFile.existsAsFile());

    ControllerProfileStore destStore(root.getChildFile("dest"));
    ControllerProfile imported;
    ASSERT_TRUE(destStore.importProfile(exportedFile, imported));
    EXPECT_EQ(imported.id, profile.id);
    EXPECT_EQ(imported.name, profile.name);

    const auto destResult = destStore.loadAll();
    ASSERT_EQ(destResult.profiles.size(), 1u);
    EXPECT_EQ(destResult.profiles[0].id, profile.id);
}

TEST_F(MidiRemoteProfileStoreTest, ExportFailsForAnUnknownProfileId) {
    ControllerProfileStore store(root);
    juce::File dest = root.getChildFile("nope.json");
    EXPECT_FALSE(store.exportProfile("no-such-profile", dest));
    EXPECT_FALSE(dest.existsAsFile());
}

TEST_F(MidiRemoteProfileStoreTest, ImportRefusesAnIdConflictWithAnExistingProfile) {
    ControllerProfileStore store(root);
    ASSERT_TRUE(store.save(makeProfile("profile-1", "Already Here")));

    // A DIFFERENT source file that happens to carry the SAME id — written into a SUBDIRECTORY of
    // the store's own root, so loadAll()'s non-recursive scan can never pick it up itself and skew
    // the assertion below (it must come from the import, or not at all).
    const auto conflicting = makeProfile("profile-1", "Different Controller, Same Id");
    const auto conflictingFile = root.getChildFile("sources").getChildFile("conflicting-source.json");
    conflictingFile.getParentDirectory().createDirectory();
    conflictingFile.replaceWithText(juce::JSON::toString(conflicting.toVar()));

    ControllerProfile imported;
    EXPECT_FALSE(store.importProfile(conflictingFile, imported))
        << "an id conflict must refuse the import, not silently overwrite";

    // The original profile is untouched.
    const auto result = store.loadAll();
    ASSERT_EQ(result.profiles.size(), 1u);
    EXPECT_EQ(result.profiles[0].name, "Already Here");
}

TEST_F(MidiRemoteProfileStoreTest, ImportFailsForAMalformedSourceFile) {
    ControllerProfileStore store(root);
    const auto badFile = root.getChildFile("bad.json");
    root.createDirectory();
    badFile.replaceWithText("not json at all");

    ControllerProfile imported;
    EXPECT_FALSE(store.importProfile(badFile, imported));
}

TEST_F(MidiRemoteProfileStoreTest, DefaultConstructorNeverThrowsAndResolvesUnderTheSettingsFolder) {
    // The default store must never construct/open a juce::PropertiesFile — this just checks it
    // resolves to a sane, non-empty path under the real settings folder, without touching disk.
    ControllerProfileStore store;
    const auto dir = store.getControllersDirectory();
    EXPECT_TRUE(dir.getFileName() == "Controllers");
    EXPECT_TRUE(dir.getParentDirectory().getFileName() == "MidiRemote");
}
