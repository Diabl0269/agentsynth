// SamplerNoteNamesTests.cpp -- note-name parsing and where dropped files land on the keyboard.
#include "Modules/Sampler/SamplerNoteNames.h"
#include <gtest/gtest.h>

using namespace synth::sampler;

TEST(SamplerNoteNamesTest, ParsesNamesWithTheGivenMiddleCOctave) {
    EXPECT_EQ(parseNoteName("C4", 4), 60);
    EXPECT_EQ(parseNoteName("C3", 3), 60);
    EXPECT_EQ(parseNoteName("c#3", 3), 61);
    EXPECT_EQ(parseNoteName("Db3", 3), 61);
    EXPECT_EQ(parseNoteName("Bb-1", 4), 10);
    EXPECT_EQ(parseNoteName("e5", 4), 76);
    EXPECT_EQ(parseNoteName("bb3", 3), 70);
    EXPECT_EQ(parseNoteName("b3", 3), 71);
    EXPECT_EQ(parseNoteName("C4  ", 4), 60);
}

TEST(SamplerNoteNamesTest, ParsesPlainIntegersAndRejectsGarbage) {
    EXPECT_EQ(parseNoteName("60", 4), 60);
    EXPECT_EQ(parseNoteName(" 0 ", 4), 0);
    EXPECT_EQ(parseNoteName("127", 4), 127);
    EXPECT_FALSE(parseNoteName("128", 4).has_value());
    EXPECT_FALSE(parseNoteName("-3", 4).has_value());
    EXPECT_FALSE(parseNoteName("", 4).has_value());
    EXPECT_FALSE(parseNoteName("H4", 4).has_value());
    EXPECT_FALSE(parseNoteName("C", 4).has_value());
    EXPECT_FALSE(parseNoteName("Bb", 4).has_value());
    EXPECT_FALSE(parseNoteName("C10", 4).has_value());
}

TEST(SamplerNoteNamesTest, FileNameHintsFindNoteAndLoudness) {
    auto h = hintsFromFileName("kalimba_C3_soft");
    EXPECT_EQ(h.note, 60);
    EXPECT_EQ(h.layer, -1);

    h = hintsFromFileName("Piano-f-E3");
    EXPECT_EQ(h.note, 64);
    EXPECT_EQ(h.layer, 1);

    h = hintsFromFileName("kalimbaC2");
    EXPECT_EQ(h.note, 48);
    EXPECT_EQ(h.layer, 0);

    h = hintsFromFileName("a_vel3_F#2");
    EXPECT_EQ(h.note, 54);
    EXPECT_EQ(h.layer, 1);

    h = hintsFromFileName("hit_PP_G3");
    EXPECT_EQ(h.layer, -1);
}

TEST(SamplerNoteNamesTest, FileNameHintsPreferTheLastNoteAndSkipLookalikes) {
    EXPECT_EQ(hintsFromFileName("C3_to_D3").note, 62);
    EXPECT_FALSE(hintsFromFileName("Take A").note.has_value());
    EXPECT_FALSE(hintsFromFileName("snare_hit").note.has_value());
    EXPECT_EQ(hintsFromFileName("Take A").layer, 0);
    EXPECT_FALSE(hintsFromFileName("Mic2").note.has_value());
    EXPECT_EQ(hintsFromFileName("Piano_C3").layer, -1);
    EXPECT_EQ(hintsFromFileName("pianoforte_C3").layer, 0);
}

TEST(SamplerNoteNamesTest, PlacementSplitsKeysHalfwayBetweenRoots) {
    const auto specs = placeDroppedFiles({"/x/s_C3.wav", "/x/s_E3.wav", "/x/s_G3.wav"});
    ASSERT_EQ(specs.size(), 3u);
    EXPECT_EQ(specs[0].rootKey, 60);
    EXPECT_EQ(specs[0].loKey, 0);
    EXPECT_EQ(specs[0].hiKey, 62);
    EXPECT_EQ(specs[1].loKey, 63);
    EXPECT_EQ(specs[1].hiKey, 65);
    EXPECT_EQ(specs[2].loKey, 66);
    EXPECT_EQ(specs[2].hiKey, 127);
    for (const auto& s : specs) {
        EXPECT_EQ(s.loVel, 1);
        EXPECT_EQ(s.hiVel, 127);
    }
}

TEST(SamplerNoteNamesTest, PlacementPutsSoftAndLoudOnTheirOwnVelocityHalves) {
    const auto specs = placeDroppedFiles({"/x/a_C3_loud.wav", "/x/a_C3_soft.wav"});
    ASSERT_EQ(specs.size(), 2u);
    for (const auto& s : specs) {
        const bool soft = s.samplePath.contains("soft");
        EXPECT_EQ(s.loVel, soft ? 1 : 64) << s.samplePath;
        EXPECT_EQ(s.hiVel, soft ? 63 : 127) << s.samplePath;
        EXPECT_EQ(s.loKey, 0);
        EXPECT_EQ(s.hiKey, 127);
    }
}

TEST(SamplerNoteNamesTest, PlacementTreatsUnknownAsTheOtherHalf) {
    const auto specs = placeDroppedFiles({"/x/a_C3.wav", "/x/a_C3_soft.wav"});
    ASSERT_EQ(specs.size(), 2u);
    for (const auto& s : specs)
        EXPECT_EQ(s.loVel, s.samplePath.contains("soft") ? 1 : 64);
}

TEST(SamplerNoteNamesTest, PlacementSplitsManyLayersEvenlyInNameOrder) {
    const auto specs = placeDroppedFiles({"/x/a_C3.wav", "/x/b_C3.wav", "/x/c_C3.wav"});
    ASSERT_EQ(specs.size(), 3u);
    EXPECT_EQ(specs[0].loVel, 1);
    EXPECT_EQ(specs[0].hiVel, 42);
    EXPECT_EQ(specs[1].loVel, 43);
    EXPECT_EQ(specs[1].hiVel, 84);
    EXPECT_EQ(specs[2].loVel, 85);
    EXPECT_EQ(specs[2].hiVel, 127);
}

TEST(SamplerNoteNamesTest, FilesWithoutNotesGetOneKeyEach) {
    auto specs = placeDroppedFiles({"/x/kick.wav", "/x/snare.wav", "/x/hat.wav"});
    ASSERT_EQ(specs.size(), 3u);
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(specs[(size_t)i].rootKey, 60 + i);
        EXPECT_EQ(specs[(size_t)i].loKey, 60 + i);
        EXPECT_EQ(specs[(size_t)i].hiKey, 60 + i);
    }

    specs = placeDroppedFiles({"/x/s_C3.wav", "/x/noise.wav", "/x/noise2.wav"});
    ASSERT_EQ(specs.size(), 3u);
    EXPECT_EQ(specs[0].rootKey, 60);
    EXPECT_EQ(specs[1].rootKey, 61);
    EXPECT_EQ(specs[2].rootKey, 62);
    EXPECT_EQ(specs[1].loKey, 61);
    EXPECT_EQ(specs[1].hiKey, 61);
}

TEST(SamplerNoteNamesTest, SingleFileCoversTheKeyboard) {
    auto specs = placeDroppedFiles({"/x/pad.wav"});
    ASSERT_EQ(specs.size(), 1u);
    EXPECT_EQ(specs[0].loKey, 0);
    EXPECT_EQ(specs[0].hiKey, 127);
    EXPECT_EQ(specs[0].rootKey, 60);

    specs = placeDroppedFiles({"/x/pad_A2.wav"});
    ASSERT_EQ(specs.size(), 1u);
    EXPECT_EQ(specs[0].rootKey, 57);
    EXPECT_EQ(specs[0].loKey, 0);
    EXPECT_EQ(specs[0].hiKey, 127);
}
