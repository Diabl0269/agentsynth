// PluginCardLayoutStoreTests.cpp
//
// Source/Plugin/Hosting/PluginCardLayoutStore.h. Every test points a store at its own temp
// directory, never the real settings folder, so runs cannot collide with a concurrent suite or a
// developer's own layouts. See docs/control/plugin-card-layout.md#persistence.

#include "Plugin/Hosting/PluginCardLayoutStore.h"
#include <gtest/gtest.h>

using synth::CardLayout;
using synth::CardSlot;
using synth::PluginCardLayoutStore;
using synth::PluginIdentity;

namespace {

PluginIdentity identityOf(const juce::String& name, int uid, const juce::String& format = "VST3") {
    PluginIdentity identity;
    identity.format = format;
    identity.name = name;
    identity.uid = uid;
    return identity;
}

CardLayout layoutWith(std::initializer_list<const char*> ids) {
    CardLayout layout;
    int index = 0;
    for (const char* id : ids) {
        CardSlot slot;
        slot.paramId = id;
        slot.indexHint = index++;
        layout.slots.push_back(slot);
    }
    return layout;
}

struct RecordingListener : PluginCardLayoutStore::Listener {
    void layoutChangedForPlugin(const PluginIdentity& identity) override { changed.push_back(identity); }
    std::vector<PluginIdentity> changed;
};

} // namespace

class PluginCardLayoutStoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                   .getChildFile("agentsynth-plugincardlayoutstore-tests-" + juce::Uuid().toString());
    }
    void TearDown() override { root.deleteRecursively(); }

    juce::File root;
};

TEST_F(PluginCardLayoutStoreTest, DefaultRoundTripsAndLivesInAPerPluginDirectory) {
    PluginCardLayoutStore store(root);
    const auto serum = identityOf("Serum", 1234);

    EXPECT_EQ(store.loadDefault(serum).status, PluginCardLayoutStore::LoadStatus::NotFound);

    ASSERT_TRUE(store.setDefault(serum, layoutWith({"cutoff", "res"})));

    EXPECT_TRUE(root.getChildFile("VST3-1234").getChildFile("default.json").existsAsFile());
    const auto loaded = store.loadDefault(serum);
    ASSERT_EQ(loaded.status, PluginCardLayoutStore::LoadStatus::Ok);
    EXPECT_EQ(loaded.layout, layoutWith({"cutoff", "res"}));
    EXPECT_EQ(loaded.pluginName, "Serum") << "the display name is stored inside the file";
}

TEST_F(PluginCardLayoutStoreTest, PluginsAreKeyedByFormatAndUidNotName) {
    PluginCardLayoutStore store(root);
    ASSERT_TRUE(store.setDefault(identityOf("Serum", 1), layoutWith({"a"})));

    EXPECT_EQ(store.loadDefault(identityOf("Serum", 2)).status, PluginCardLayoutStore::LoadStatus::NotFound);
    EXPECT_EQ(store.loadDefault(identityOf("Serum", 1, "AudioUnit")).status,
              PluginCardLayoutStore::LoadStatus::NotFound);
    EXPECT_EQ(store.loadDefault(identityOf("Renamed", 1)).status, PluginCardLayoutStore::LoadStatus::Ok)
        << "the uid is the identity; a renamed plugin keeps its layout";
}

TEST_F(PluginCardLayoutStoreTest, AUidOfZeroFallsBackToTheNameSoPluginsDoNotShareOne) {
    PluginCardLayoutStore store(root);
    ASSERT_TRUE(store.setDefault(identityOf("Alpha", 0), layoutWith({"a"})));

    EXPECT_EQ(store.loadDefault(identityOf("Beta", 0)).status, PluginCardLayoutStore::LoadStatus::NotFound);
    EXPECT_EQ(store.loadDefault(identityOf("Alpha", 0)).status, PluginCardLayoutStore::LoadStatus::Ok);
}

TEST_F(PluginCardLayoutStoreTest, PresetsAreSiblingFilesAndNeverIncludeTheDefault) {
    PluginCardLayoutStore store(root);
    const auto serum = identityOf("Serum", 1234);
    ASSERT_TRUE(store.setDefault(serum, layoutWith({"d"})));

    ASSERT_TRUE(store.savePreset(serum, "Wobble Bass", layoutWith({"a", "b"})));
    ASSERT_TRUE(store.savePreset(serum, "Init", layoutWith({"c"})));

    EXPECT_EQ(store.listPresets(serum), juce::StringArray({"Init", "Wobble Bass"}));
    const auto wobble = store.loadPreset(serum, "Wobble Bass");
    ASSERT_EQ(wobble.status, PluginCardLayoutStore::LoadStatus::Ok);
    EXPECT_EQ(wobble.layout, layoutWith({"a", "b"}));

    ASSERT_TRUE(store.deletePreset(serum, "Init"));
    EXPECT_EQ(store.listPresets(serum), juce::StringArray({"Wobble Bass"}));
    EXPECT_FALSE(store.deletePreset(serum, "Init")) << "already gone";
    EXPECT_EQ(store.loadDefault(serum).status, PluginCardLayoutStore::LoadStatus::Ok) << "presets never touch default";
}

TEST_F(PluginCardLayoutStoreTest, TheReservedNameAndEmptyNamesAreRefusedForPresets) {
    PluginCardLayoutStore store(root);
    const auto serum = identityOf("Serum", 1234);

    EXPECT_FALSE(store.savePreset(serum, "default", layoutWith({"a"})));
    EXPECT_FALSE(store.savePreset(serum, "Default", layoutWith({"a"}))) << "volumes are case-insensitive";
    EXPECT_FALSE(store.savePreset(serum, "   ", layoutWith({"a"})));
    EXPECT_FALSE(store.loadDefault(serum).status == PluginCardLayoutStore::LoadStatus::Ok);
}

TEST_F(PluginCardLayoutStoreTest, ListenerHearsDefaultChangesButNotPresetsOrNoOps) {
    PluginCardLayoutStore store(root);
    RecordingListener listener;
    store.addListener(&listener);
    const auto serum = identityOf("Serum", 1234);

    ASSERT_TRUE(store.setDefault(serum, layoutWith({"a"})));
    ASSERT_EQ(listener.changed.size(), 1u);
    EXPECT_EQ(listener.changed[0], serum);

    ASSERT_TRUE(store.savePreset(serum, "P", layoutWith({"b"})));
    ASSERT_TRUE(store.deletePreset(serum, "P"));
    EXPECT_EQ(listener.changed.size(), 1u) << "a preset is not the live layout";

    ASSERT_TRUE(store.clearDefault(serum));
    EXPECT_EQ(listener.changed.size(), 2u);
    ASSERT_TRUE(store.clearDefault(serum));
    EXPECT_EQ(listener.changed.size(), 2u) << "clearing nothing changes nothing";

    store.removeListener(&listener);
    ASSERT_TRUE(store.setDefault(serum, layoutWith({"a"})));
    EXPECT_EQ(listener.changed.size(), 2u);
}

TEST_F(PluginCardLayoutStoreTest, SetDefaultTouchesNothingButItsOwnFile) {
    PluginCardLayoutStore store(root);
    const auto serum = identityOf("Serum", 1234);
    const auto other = identityOf("Other", 99);
    ASSERT_TRUE(store.setDefault(other, layoutWith({"o"})));
    ASSERT_TRUE(store.savePreset(serum, "P", layoutWith({"p"})));

    ASSERT_TRUE(store.setDefault(serum, layoutWith({"d"})));

    EXPECT_EQ(store.loadDefault(other).layout, layoutWith({"o"}));
    EXPECT_EQ(store.listPresets(serum), juce::StringArray({"P"}));
}

TEST_F(PluginCardLayoutStoreTest, RefusesANewerVersionAndMalformedFilesVisiblyWithoutOverwritingThem) {
    PluginCardLayoutStore store(root);
    const auto serum = identityOf("Serum", 1234);
    const auto dir = store.getPluginDirectory(serum);
    ASSERT_TRUE(dir.createDirectory());

    const auto file = dir.getChildFile("default.json");
    ASSERT_TRUE(file.replaceWithText(R"({"version":2,"slots":[]})"));
    EXPECT_EQ(store.loadDefault(serum).status, PluginCardLayoutStore::LoadStatus::UnsupportedVersion);

    ASSERT_TRUE(file.replaceWithText("not json at all"));
    EXPECT_EQ(store.loadDefault(serum).status, PluginCardLayoutStore::LoadStatus::Malformed);
    EXPECT_EQ(file.loadFileAsString(), "not json at all") << "loading never rewrites the file";
}

TEST_F(PluginCardLayoutStoreTest, AnInvalidIdentityIsNeverWritten) {
    PluginCardLayoutStore store(root);
    EXPECT_FALSE(store.setDefault(PluginIdentity{}, layoutWith({"a"})));
    EXPECT_FALSE(root.exists());
}

TEST(PluginCardLayoutStorePathTest, DefaultRootIsBesideTheSettingsFileAndDoesNoIo) {
    const auto root = PluginCardLayoutStore::resolveDefaultRootDirectory();
    EXPECT_EQ(root.getFileName(), "PluginCardLayouts");
    EXPECT_EQ(PluginCardLayoutStore().getRootDirectory(), root);
}
