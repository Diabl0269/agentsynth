// ModuleCardLayoutStoreTests.cpp
//
// Source/UI/Graph/CardBody/ModuleCardLayoutStore.h: the per-module-type root of the shared card
// layout store (default.json + sibling presets under <root>/<ModuleType>/). Every test points a
// store at its own temp directory. The plugin root's own suite is PluginCardLayoutStoreTests.cpp.

#include "UI/Graph/CardBody/ModuleCardLayoutStore.h"
#include <gtest/gtest.h>

using synth::CardLayout;
using synth::CardParamItem;
using synth::CardSection;
using synth::CardWidget;
using synth::ModuleCardLayoutStore;

namespace {

// A v2 layout (hidden ids), so the files exercise the new writer, not just the v1 one.
CardLayout layoutHiding(const juce::String& hiddenId) {
    CardParamItem item;
    item.paramId = "cutoff";
    item.widget = CardWidget::KnobLarge;
    CardSection section;
    section.id = "main";
    section.items.emplace_back(item);

    CardLayout layout;
    layout.sections = {section};
    layout.hidden.add(hiddenId);
    return layout;
}

struct RecordingListener : ModuleCardLayoutStore::Listener {
    void layoutChangedForModuleType(const juce::String& type) override { changed.add(type); }
    juce::StringArray changed;
};

} // namespace

class ModuleCardLayoutStoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                   .getChildFile("agentsynth-modulecardlayoutstore-tests-" + juce::Uuid().toString());
    }
    void TearDown() override { root.deleteRecursively(); }

    juce::File root;
};

TEST_F(ModuleCardLayoutStoreTest, DefaultRoundTripsAndLivesInAPerTypeDirectory) {
    ModuleCardLayoutStore store(root);
    EXPECT_EQ(store.loadDefault("Filter").status, ModuleCardLayoutStore::LoadStatus::NotFound);

    ASSERT_TRUE(store.setDefault("Filter", layoutHiding("drive")));

    EXPECT_TRUE(root.getChildFile("Filter").getChildFile("default.json").existsAsFile());
    const auto loaded = store.loadDefault("Filter");
    ASSERT_EQ(loaded.status, ModuleCardLayoutStore::LoadStatus::Ok);
    EXPECT_EQ(loaded.layout, layoutHiding("drive"));
    EXPECT_EQ(loaded.file.getDynamicObject()->getProperty("moduleType").toString(), "Filter");
    EXPECT_EQ(store.loadDefault("Oscillator").status, ModuleCardLayoutStore::LoadStatus::NotFound)
        << "types do not share a file";
}

TEST_F(ModuleCardLayoutStoreTest, TypeNamesWithUnsafeCharactersMapToALegalDirectory) {
    ModuleCardLayoutStore store(root);
    ASSERT_TRUE(store.setDefault("Sample & Hold", layoutHiding("a")));
    ASSERT_TRUE(store.setDefault("A/B", layoutHiding("b")));

    EXPECT_EQ(store.loadDefault("Sample & Hold").layout, layoutHiding("a"));
    EXPECT_EQ(store.loadDefault("A/B").layout, layoutHiding("b"));
    EXPECT_FALSE(store.setDefault("  ", layoutHiding("c"))) << "an empty type has no directory";
}

TEST_F(ModuleCardLayoutStoreTest, PresetsAreSiblingFilesAndNeverIncludeTheDefault) {
    ModuleCardLayoutStore store(root);
    ASSERT_TRUE(store.setDefault("Filter", layoutHiding("d")));

    ASSERT_TRUE(store.savePreset("Filter", "Minimal", layoutHiding("a")));
    ASSERT_TRUE(store.savePreset("Filter", "Big knobs", layoutHiding("b")));

    EXPECT_TRUE(root.getChildFile("Filter").getChildFile("Minimal.json").existsAsFile());
    EXPECT_EQ(store.listPresets("Filter"), juce::StringArray({"Big knobs", "Minimal"}));
    EXPECT_TRUE(store.listPresets("Oscillator").isEmpty());
    const auto minimal = store.loadPreset("Filter", "Minimal");
    ASSERT_EQ(minimal.status, ModuleCardLayoutStore::LoadStatus::Ok);
    EXPECT_EQ(minimal.layout, layoutHiding("a"));

    ASSERT_TRUE(store.deletePreset("Filter", "Minimal"));
    EXPECT_EQ(store.listPresets("Filter"), juce::StringArray({"Big knobs"}));
    EXPECT_FALSE(store.deletePreset("Filter", "Minimal")) << "already gone";
    EXPECT_EQ(store.loadDefault("Filter").status, ModuleCardLayoutStore::LoadStatus::Ok);
}

TEST_F(ModuleCardLayoutStoreTest, ReservedAndEmptyPresetNamesAreRefused) {
    ModuleCardLayoutStore store(root);
    EXPECT_FALSE(store.savePreset("Filter", "default", layoutHiding("a")));
    EXPECT_FALSE(store.savePreset("Filter", "Default", layoutHiding("a")));
    EXPECT_FALSE(store.savePreset("Filter", "   ", layoutHiding("a")));
    EXPECT_FALSE(root.getChildFile("Filter").exists());
}

TEST_F(ModuleCardLayoutStoreTest, ListenerHearsDefaultChangesButNotPresetsOrNoOps) {
    ModuleCardLayoutStore store(root);
    RecordingListener listener;
    store.addListener(&listener);

    ASSERT_TRUE(store.setDefault("Filter", layoutHiding("a")));
    ASSERT_TRUE(store.savePreset("Filter", "P", layoutHiding("b")));
    EXPECT_EQ(listener.changed, juce::StringArray({"Filter"}));

    ASSERT_TRUE(store.clearDefault("Filter"));
    ASSERT_TRUE(store.clearDefault("Filter"));
    EXPECT_EQ(listener.changed, juce::StringArray({"Filter", "Filter"})) << "clearing nothing changes nothing";
    EXPECT_EQ(store.loadDefault("Filter").status, ModuleCardLayoutStore::LoadStatus::NotFound);
    EXPECT_EQ(store.listPresets("Filter"), juce::StringArray({"P"})) << "clearing the default keeps presets";
}

TEST_F(ModuleCardLayoutStoreTest, RefusesANewerVersionAndMalformedFilesWithoutOverwritingThem) {
    ModuleCardLayoutStore store(root);
    const auto dir = store.getTypeDirectory("Filter");
    ASSERT_TRUE(dir.createDirectory());
    const auto file = dir.getChildFile("default.json");

    ASSERT_TRUE(file.replaceWithText(R"({"version":3,"sections":[]})"));
    EXPECT_EQ(store.loadDefault("Filter").status, ModuleCardLayoutStore::LoadStatus::UnsupportedVersion);

    ASSERT_TRUE(file.replaceWithText("not json at all"));
    EXPECT_EQ(store.loadDefault("Filter").status, ModuleCardLayoutStore::LoadStatus::Malformed);
    EXPECT_EQ(file.loadFileAsString(), "not json at all");
}

TEST_F(ModuleCardLayoutStoreTest, ThePluginAndModuleRootsAreSeparateDirectories) {
    EXPECT_NE(ModuleCardLayoutStore::resolveDefaultRootDirectory().getFileName(), juce::String("PluginCardLayouts"));
    EXPECT_EQ(ModuleCardLayoutStore::resolveDefaultRootDirectory().getFileName(), "ModuleCardLayouts");
}
