// CoreSoundsTests.cpp -- the fixed list of core pack instruments the song planner may name, and how a plan keeps them.
#include "AI/SongSteps/SongSteps.h"
#include "CoreSoundTestPack.h"
#include "SoundPacks/CoreSounds.h"
#include <gtest/gtest.h>

using namespace synth::soundpacks;

TEST(CoreSounds, TheNineIdsAreKnownWithTheirStandIns) {
    const std::pair<const char*, const char*> expected[] = {
        {"core/upright-piano", "Mallet"},      {"core/string-section", "Wavetable"}, {"core/trumpet", "Wavetable"},
        {"core/french-horn", "Wavetable"},     {"core/flute", "Wavetable"},          {"core/clarinet", "Wavetable"},
        {"core/double-bass-plucked", "Pluck"}, {"core/electric-guitar", "Pluck"},    {"core/acoustic-kit", "Drum Kit"}};
    for (const auto& [id, standIn] : expected) {
        ASSERT_NE(findCoreSound(id), nullptr) << id;
        EXPECT_TRUE(coreSoundMatchesInstrument(id, standIn)) << id;
        EXPECT_FALSE(coreSoundMatchesInstrument(id, "Oscillator")) << id;
        EXPECT_EQ(juce::String(id), "core/" + coreInstrumentId(*findCoreSound(id)));
    }
    EXPECT_EQ(findCoreSound("core/theremin"), nullptr);
    EXPECT_EQ(findCoreSound(""), nullptr);
    EXPECT_EQ(findCoreSound("other/flute"), nullptr);
    EXPECT_TRUE(findCoreSound("core/acoustic-kit")->drumKit);
    EXPECT_FALSE(findCoreSound("core/flute")->drumKit);
}

TEST(CoreSounds, TheFiveAnimalSoundsAreInTheAnimalsPackWithAnOscillatorStandIn) {
    for (const char* id : {"animals/whale", "animals/crow", "animals/owl", "animals/frog", "animals/wolf"}) {
        const auto* sound = findCoreSound(id);
        ASSERT_NE(sound, nullptr) << id;
        EXPECT_EQ(packIdOf(*sound), "animals") << id;
        EXPECT_EQ(juce::String(id), "animals/" + coreInstrumentId(*sound));
        EXPECT_TRUE(coreSoundMatchesInstrument(id, "Oscillator")) << id;
        EXPECT_FALSE(sound->drumKit) << id;
    }
    EXPECT_EQ(packIdOf(*findCoreSound("core/flute")), "core");
    EXPECT_EQ(coreSoundNames({"animals/whale", "core/flute", "animals/wolf"}), "Whale call, Flute, Wolf howl");
}

TEST(CoreSounds, InstalledIsPerPack) {
    core_sound_test::FakeCorePack pack; // core only
    EXPECT_TRUE(coreSoundInstalled(*findCoreSound("core/flute")));
    EXPECT_FALSE(coreSoundInstalled(*findCoreSound("animals/whale")));
    pack.installAnimals();
    EXPECT_TRUE(coreSoundInstalled(*findCoreSound("animals/whale")));
}

TEST(CoreSounds, NamesAreListedOnceInOrderAndUnknownIdsAreLeftOut) {
    EXPECT_EQ(coreSoundNames({"core/upright-piano", "core/nope", "core/double-bass-plucked", "core/upright-piano",
                              "core/acoustic-kit"}),
              "Upright piano, Plucked double bass, Acoustic drum kit");
}

TEST(CoreSounds, InstalledFollowsTheSoundPackLibrary) {
    {
        core_sound_test::FakeCorePack missing(false);
        EXPECT_FALSE(coreSoundInstalled(*findCoreSound("core/flute")));
    }
    core_sound_test::FakeCorePack present;
    EXPECT_TRUE(coreSoundInstalled(*findCoreSound("core/flute")));
}

TEST(CoreSounds, APlanKeepsAKnownCoreSoundPerTrackAndDropsAnythingElse) {
    const auto plan = juce::JSON::parse(
        R"({"title":"T","bpm":100,"key":"C","special":"",
        "sections":[{"name":"A","bars":8,"energy":2,"chords":"C","description":"d"}],
        "tracks":[{"name":"Piano","role":"keys","instrument":"Mallet","sound":"s","coreSound":"core/upright-piano","sections":["A"]},
                  {"name":"Odd","role":"lead","instrument":"Wavetable","sound":"s","coreSound":"core/theremin","sections":["A"]},
                  {"name":"Plain","role":"bass","instrument":"Oscillator","sound":"s","sections":["A"]}],
        "lanes":[]})");
    synth::songsteps::SongPlan parsed;
    ASSERT_TRUE(synth::songsteps::parseSongPlan(plan, parsed));
    ASSERT_EQ(parsed.tracks.size(), 3u);
    EXPECT_EQ(parsed.tracks[0].coreSound, "core/upright-piano");
    EXPECT_EQ(parsed.tracks[1].coreSound, "");
    EXPECT_EQ(parsed.tracks[2].coreSound, "");
    EXPECT_EQ(parsed.coreSounds(), std::vector<juce::String>{"core/upright-piano"});
    EXPECT_TRUE(parsed.raw.getProperty("tracks", {}).getArray()->getReference(1).hasProperty("coreSound"))
        << "the plan goes back to the server exactly as received";
}
