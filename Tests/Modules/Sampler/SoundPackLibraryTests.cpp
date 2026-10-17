// SoundPackLibraryTests.cpp -- pack folders on disk: listing, lookup, root order and malformed manifests.
#include "Modules/Sampler/SoundPackLibrary.h"
#include "SamplerTestWavs.h"
#include <gtest/gtest.h>

using namespace synth::sampler;
using sampler_test::TempDir;

namespace {

void writePack(const juce::File& root, const juce::String& folder, const juce::String& manifest,
               const juce::StringArray& sfzFiles = {"a.sfz"}) {
    const auto dir = root.getChildFile(folder);
    dir.createDirectory();
    for (const auto& sfz : sfzFiles)
        dir.getChildFile(sfz).getParentDirectory().createDirectory(),
            dir.getChildFile(sfz).replaceWithText("<region> sample=x.wav");
    dir.getChildFile("pack.json").replaceWithText(manifest);
}

const char* const kGoodManifest = R"({"id":"keys","name":"Keys","version":"1","licence":"CC0","credit":"Someone",
    "instruments":[{"id":"piano","name":"Grand","sfz":"a.sfz","category":"Keys"},
                   {"id":"noname","sfz":"a.sfz"}]})";

struct RootsGuard {
    explicit RootsGuard(std::vector<juce::File> roots) { SoundPackLibrary::setRootsForTests(std::move(roots)); }
    ~RootsGuard() { SoundPackLibrary::setRootsForTests(std::nullopt); }
};

} // namespace

TEST(SoundPackLibraryTest, ListsAndFindsInstrumentsOfAWellFormedPack) {
    TempDir dir;
    writePack(dir.path(), "keys", kGoodManifest);
    const RootsGuard guard({dir.path()});

    const auto all = SoundPackLibrary::listInstruments();
    ASSERT_EQ(all.size(), 2u);
    EXPECT_EQ(all[0].packId, "keys");
    EXPECT_EQ(all[0].packName, "Keys");
    EXPECT_EQ(all[0].instrumentId, "piano");
    EXPECT_EQ(all[0].name, "Grand");
    EXPECT_EQ(all[0].category, "Keys");
    EXPECT_EQ(all[0].licence, "CC0");
    EXPECT_EQ(all[0].sfz, dir.path().getChildFile("keys/a.sfz"));
    EXPECT_EQ(all[1].name, "noname") << "a missing name falls back to the id";

    const auto found = SoundPackLibrary::find("keys", "noname");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->instrumentId, "noname");
    EXPECT_FALSE(SoundPackLibrary::find("keys", "nope").has_value());
    EXPECT_FALSE(SoundPackLibrary::find("other", "piano").has_value());
}

TEST(SoundPackLibraryTest, MalformedPacksAndEntriesAreSkipped) {
    TempDir dir;
    writePack(dir.path(), "broken", "{ this is not json");
    writePack(dir.path(), "noid", R"({"name":"x","instruments":[]})");
    writePack(dir.path(), "escape", R"({"id":"esc","instruments":[{"id":"a","sfz":"../outside.sfz"},
        {"id":"b","sfz":"/etc/hosts"},{"id":"c","sfz":"missing.sfz"},{"id":"","sfz":"a.sfz"},{"id":"ok","sfz":"a.sfz"}]})");
    dir.path().getChildFile("plain").createDirectory(); // not a pack
    dir.path().getChildFile("outside.sfz").replaceWithText("x");
    const RootsGuard guard({dir.path()});

    const auto all = SoundPackLibrary::listInstruments();
    ASSERT_EQ(all.size(), 1u);
    EXPECT_EQ(all[0].instrumentId, "ok");
}

TEST(SoundPackLibraryTest, AnEarlierRootWinsForTheSamePackId) {
    TempDir bundled, user;
    writePack(bundled.path(), "keys", R"({"id":"keys","instruments":[{"id":"piano","name":"Bundled","sfz":"a.sfz"}]})");
    writePack(user.path(), "keys", R"({"id":"keys","instruments":[{"id":"piano","name":"User","sfz":"a.sfz"}]})");
    writePack(user.path(), "mine", R"({"id":"mine","instruments":[{"id":"x","sfz":"a.sfz"}]})");
    const RootsGuard guard({bundled.path(), user.path()});

    const auto all = SoundPackLibrary::listInstruments();
    ASSERT_EQ(all.size(), 2u);
    EXPECT_EQ(all[0].name, "Bundled");
    EXPECT_EQ(all[1].packId, "mine");
}

TEST(SoundPackLibraryTest, MissingRootsAreIgnoredAndTheDefaultRootsAreBundledThenUser) {
    {
        const RootsGuard guard({juce::File("/definitely/not/here")});
        EXPECT_TRUE(SoundPackLibrary::listInstruments().empty());
    }
    const auto roots = SoundPackLibrary::roots();
    ASSERT_EQ(roots.size(), 2u);
    EXPECT_EQ(roots[1].getFileName(), "SoundPacks");
    EXPECT_EQ(roots[1].getParentDirectory().getFileName(), "Agent Synth");
}
