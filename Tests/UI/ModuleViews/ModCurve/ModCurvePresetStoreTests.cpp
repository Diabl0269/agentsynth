// The curve preset store: built-ins first, user presets after them, a round trip through the file, and a corrupt file
// that leaves the built-ins. Each test works in a folder of its own.

#include "../../../UniqueTempDir.h"
#include "UI/ModuleViews/ModCurve/ModCurvePresetStore.h"
#include "UserSettings.h"
#include <gtest/gtest.h>

namespace {
using synth::ModCurve;
using synth::ui::ModCurvePresetStore;

struct Folder {
    juce::File dir = synth::test::makeUniqueTempDir("curvestore");
    ~Folder() { dir.deleteRecursively(); }
};
} // namespace

TEST(ModCurvePresetStore, ListsTheFourBuiltInsInOrderWhenNothingIsSaved) {
    Folder f;
    const auto list = ModCurvePresetStore(f.dir).list();
    ASSERT_EQ(list.size(), 4u);
    const char* names[] = {"Linear", "Exp", "Log", "S-curve"};
    for (size_t i = 0; i < 4; ++i) {
        EXPECT_EQ(list[i].name, names[i]);
        EXPECT_TRUE(list[i].builtIn);
    }
    EXPECT_TRUE(list[0].curve.isLinear());
}

TEST(ModCurvePresetStore, SavedPresetsRoundTripAndComeAfterTheBuiltIns) {
    Folder f;
    ModCurvePresetStore store(f.dir);
    auto bent = ModCurve::preset(ModCurve::Preset::SCurve);
    bent.rangeMin = 0.2f;
    bent.rangeMax = 0.8f;
    ASSERT_TRUE(store.save("Mine", bent));
    ASSERT_TRUE(store.save("Second", ModCurve::preset(ModCurve::Preset::Exp)));

    const auto list = ModCurvePresetStore(f.dir).list(); // a fresh object: read from the file
    ASSERT_EQ(list.size(), 6u);
    EXPECT_EQ(list[4].name, "Mine");
    EXPECT_FALSE(list[4].builtIn);
    EXPECT_EQ(list[4].curve, bent);
    EXPECT_EQ(list[5].name, "Second");

    ASSERT_TRUE(store.save("Mine", ModCurve::linear())) << "the same name replaces";
    EXPECT_EQ(store.userPresets().size(), 2u);
    EXPECT_TRUE(store.userPresets()[0].curve.isLinear());
    EXPECT_TRUE(store.remove("Mine"));
    EXPECT_EQ(store.userPresets().size(), 1u);
}

TEST(ModCurvePresetStore, RefusesAnEmptyOrBuiltInName) {
    Folder f;
    ModCurvePresetStore store(f.dir);
    EXPECT_FALSE(store.save("   ", ModCurve::linear()));
    EXPECT_FALSE(store.save("exp", ModCurve::linear()));
    EXPECT_TRUE(store.userPresets().empty());
}

TEST(ModCurvePresetStore, ACorruptFileGivesTheBuiltInsOnly) {
    Folder f;
    ModCurvePresetStore store(f.dir);
    for (const char* junk : {"not json {{{", "{\"presets\": 7}", "[1,2,3]", "{\"presets\":[{\"name\":\"X\"}]}", ""}) {
        store.file().replaceWithText(junk);
        EXPECT_EQ(store.list().size(), 4u) << junk;
    }
    EXPECT_TRUE(store.save("After", ModCurve::linear())) << "a save over a corrupt file recovers";
    EXPECT_EQ(store.list().size(), 5u);
}

TEST(ModCurvePresetStore, TheAppStoreLivesUnderTheSettingsDirectory) {
    EXPECT_TRUE(ModCurvePresetStore::app().file().isAChildOf(synth::userSettingsRootDirectory()));
}
