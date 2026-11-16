// StarterPackTests.cpp -- the starter sound pack that ships inside the app (assets/sound-packs/starter): it lists, its
// files are all there and load, it stays within the size budget and carries its licences, and it never takes the place
// of the downloadable core pack.
#include "Modules/Sampler/SfzReader.h"
#include "Modules/Sampler/SoundPackLibrary.h"
#include "SoundPacks/CoreSounds.h"
#include "TestPaths.h"
#include <gtest/gtest.h>

using namespace synth::sampler;

namespace {

constexpr juce::int64 kBudgetBytes = 8 * 1024 * 1024; // kit + bass + horn is ~6.3 MB

struct RootsGuard {
    RootsGuard() { SoundPackLibrary::setRootsForTests(std::vector<juce::File>{juce::File(BUNDLED_SOUND_PACKS_DIR)}); }
    ~RootsGuard() { SoundPackLibrary::setRootsForTests(std::nullopt); }
};

juce::File starterDir() { return juce::File(BUNDLED_SOUND_PACKS_DIR).getChildFile("starter"); }

} // namespace

TEST(StarterPack, ListsThreeInstrumentsUnderItsOwnPackIdWithACc0Licence) {
    const RootsGuard guard;
    const auto all = SoundPackLibrary::listInstruments();
    ASSERT_EQ(all.size(), 3u);
    for (const auto& inst : all) {
        EXPECT_EQ(inst.packId, "starter");
        EXPECT_EQ(inst.packName, "Starter instruments");
        EXPECT_TRUE(inst.licence.startsWith("CC0")) << inst.licence;
        EXPECT_TRUE(inst.sfz.existsAsFile()) << inst.sfz.getFullPathName();
    }
    EXPECT_TRUE(SoundPackLibrary::find("starter", "french-horn").has_value());
    EXPECT_TRUE(SoundPackLibrary::find("starter", "double-bass-plucked").has_value());
    EXPECT_TRUE(SoundPackLibrary::find("starter", "acoustic-kit").has_value());
}

TEST(StarterPack, EveryInstrumentLoadsWithItsRecordings) {
    const RootsGuard guard;
    for (const auto& inst : SoundPackLibrary::listInstruments()) {
        juce::StringArray errors;
        const auto instrument = loadSfzInstrument(inst.sfz, errors);
        ASSERT_NE(instrument, nullptr) << inst.instrumentId << ": " << errors.joinIntoString("; ");
        EXPECT_FALSE(instrument->regions.empty()) << inst.instrumentId;
        EXPECT_TRUE(errors.isEmpty()) << inst.instrumentId << ": " << errors.joinIntoString("; ");
    }
}

TEST(StarterPack, StaysWithinTheSizeBudgetAndCarriesItsLicences) {
    juce::int64 bytes = 0;
    for (const auto& file : starterDir().findChildFiles(juce::File::findFiles, true))
        bytes += file.getSize();
    EXPECT_GT(bytes, 0);
    EXPECT_LE(bytes, kBudgetBytes) << "the starter pack grew past the budget the app may carry";

    const auto licences = starterDir().getChildFile("LICENSES");
    EXPECT_TRUE(licences.getChildFile("SOURCES.md").existsAsFile());
    EXPECT_TRUE(licences.getChildFile("VSCO-2-CE-LICENSE-CC0.txt").existsAsFile());
    EXPECT_TRUE(licences.getChildFile("Virtuosity-Drums-LICENSE-CC0.txt").existsAsFile());
}

TEST(StarterPack, NeverTakesTheCorePacksIdsOrSwapsIn) {
    // The core ids in CoreSounds.cpp keep meaning the downloaded core pack: a plan naming core/french-horn still waits
    // for the download, and no starter id is a song-plan sound.
    EXPECT_NE(juce::String("starter"), juce::String(synth::soundpacks::kCorePackId));
    EXPECT_EQ(synth::soundpacks::findCoreSound("starter/french-horn"), nullptr);
    EXPECT_NE(synth::soundpacks::findCoreSound("core/french-horn"), nullptr);
}

TEST(StarterPack, TheBundledRootStaysFirstAfterAnOnlyRootThatKeepsIt) {
    const auto user = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("starter-pack-roots");
    SoundPackLibrary::setOnlyRoot(user, true);
    auto roots = SoundPackLibrary::roots();
    ASSERT_EQ(roots.size(), 2u);
    EXPECT_EQ(roots[0].getFileName(), "SoundPacks");
    EXPECT_EQ(roots[1], user);

    SoundPackLibrary::setOnlyRoot(user); // test mode's usual shape: only this folder
    roots = SoundPackLibrary::roots();
    ASSERT_EQ(roots.size(), 1u);
    EXPECT_EQ(roots[0], user);

    SoundPackLibrary::setOnlyRoot(juce::File()); // back to the normal roots
    EXPECT_EQ(SoundPackLibrary::roots().size(), 2u);
}
