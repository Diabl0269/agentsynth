#include "../Source/Auth/DeviceIdStore.h"
#include <gtest/gtest.h>
#include <juce_core/juce_core.h>

namespace {

class DeviceIdStoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        parentDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                        .getChildFile("DeviceIdStoreTest")
                        .getNonexistentChildFile("run", "", false);
        parentDir.createDirectory();
    }

    void TearDown() override { parentDir.deleteRecursively(); }

    juce::File idFile() const { return parentDir.getChildFile("device_id"); }

    juce::File parentDir;
};

} // namespace

TEST_F(DeviceIdStoreTest, FreshLocationProducesAValidNonEmptyId) {
    synth::DeviceIdStore store{idFile()};

    const auto id = store.getDeviceId();
    EXPECT_TRUE(id.isNotEmpty());
    EXPECT_GE(id.length(), 32);

    // Persisted for the next instance/process to pick up.
    EXPECT_TRUE(idFile().existsAsFile());
    EXPECT_EQ(idFile().loadFileAsString().trim(), id);
}

TEST_F(DeviceIdStoreTest, TwoInstancesAtTheSameFileReturnTheSameId) {
    synth::DeviceIdStore first{idFile()};
    const auto firstId = first.getDeviceId();

    synth::DeviceIdStore second{idFile()};
    const auto secondId = second.getDeviceId();

    EXPECT_EQ(firstId, secondId);
    EXPECT_TRUE(firstId.isNotEmpty());
}

TEST_F(DeviceIdStoreTest, CorruptedExistingFileIsRecoveredFromWithAFreshId) {
    idFile().getParentDirectory().createDirectory();
    idFile().replaceWithText("!!! not a device id, just garbage bytes !!!");

    synth::DeviceIdStore store{idFile()};
    const auto id = store.getDeviceId();

    EXPECT_TRUE(id.isNotEmpty());
    EXPECT_NE(id, juce::String("!!! not a device id, just garbage bytes !!!"));
    // The corrupted file is overwritten with the freshly generated id, not left as-is.
    EXPECT_EQ(idFile().loadFileAsString().trim(), id);
}

TEST_F(DeviceIdStoreTest, EmptyExistingFileIsRecoveredFromWithAFreshId) {
    idFile().getParentDirectory().createDirectory();
    idFile().replaceWithText("");

    synth::DeviceIdStore store{idFile()};
    const auto id = store.getDeviceId();

    EXPECT_TRUE(id.isNotEmpty());
}

TEST_F(DeviceIdStoreTest, MissingParentDirectoryIsCreated) {
    // idFile()'s parent (parentDir) exists from SetUp(), but point at a location nested one level
    // deeper that has never been created, to exercise the parent-directory-creation path.
    const auto nestedFile = parentDir.getChildFile("nested").getChildFile("device_id");

    synth::DeviceIdStore store{nestedFile};

    EXPECT_TRUE(store.getDeviceId().isNotEmpty());
    EXPECT_TRUE(nestedFile.existsAsFile());
}
