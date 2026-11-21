// SoundPackSourceListTests.cpp -- reading a pack's LICENSES/SOURCES.md into the lines Settings > Sound packs lists.
#include "SoundPacks/SoundPackSourceList.h"
#include <gtest/gtest.h>

using namespace synth::soundpacks;

namespace {

// A short copy of assets/sound-packs/starter/LICENSES/SOURCES.md, plus a section that is compared but not used.
const juce::String kStarter =
    juce::String("# Sources for the Starter instruments pack\n\nAll recordings are public domain.\n\n"
                 "Licence texts: `VSCO-2-CE-LICENSE-CC0.txt`.\n\n"
                 "## French horn (`french-horn`)\n\n"
                 "- Source pack: VSCO 2 Community Edition (Versilian Studios)\n"
                 "- URL: https://github.com/sgossner/VSCO-2-CE\n"
                 "- Source folder(s): `Brass/F Horn/sus`\n"
                 "- Files used (18): MOHorn_sus_A#1_v1_1.wav, MOHorn_sus_A#1_v3_1.wav\n"
                 "- Changes: sustain articulation, mono.\n\n"
                 "## Plucked double bass (`double-bass-plucked`)\n\n"
                 "- Source pack: VSCO 2 Community Edition (Versilian Studios)\n"
                 "- URL: https://github.com/sgossner/VSCO-2-CE\n"
                 "- Files used (10): BKCtbss_Pizz_A#0_v1_rr1.wav\n\n"
                 "## Acoustic drum kit (`acoustic-kit`)\n\n"
                 "- Source pack: Virtuosity Drums (Versilian Studios)\n"
                 "- URL: https://github.com/sfzinstruments/virtuosity_drums\n"
                 "- Files used (38): mid_crash_crash_vl2_rr1.flac\n\n"
                 "## Compared but not used: Upright Piano KW (FreePats)\n\n"
                 "- Reason: too noisy.\n");

} // namespace

TEST(SoundPackSourceList, ReadsEachInstrumentWithItsSourceUrlAndFileCount) {
    const auto entries = parseSoundPackSources(kStarter);
    ASSERT_EQ(entries.size(), 3u);
    EXPECT_EQ(entries[0].instrument, "French horn");
    EXPECT_EQ(entries[0].sourcePack, "VSCO 2 Community Edition (Versilian Studios)");
    EXPECT_EQ(entries[0].url, "https://github.com/sgossner/VSCO-2-CE");
    EXPECT_EQ(entries[0].fileCount, 18);
    EXPECT_EQ(entries[1].instrument, "Plucked double bass");
    EXPECT_EQ(entries[1].fileCount, 10);
    EXPECT_EQ(entries[2].instrument, "Acoustic drum kit");
    EXPECT_EQ(entries[2].sourcePack, "Virtuosity Drums (Versilian Studios)");
    EXPECT_EQ(entries[2].fileCount, 38);
}

TEST(SoundPackSourceList, ASectionWithoutASourcePackIsSkipped) {
    for (const auto& entry : parseSoundPackSources(kStarter))
        EXPECT_FALSE(entry.instrument.contains("Compared")) << entry.instrument;
}

TEST(SoundPackSourceList, OneLinePerInstrumentWithoutTheScheme) {
    const auto lines = soundPackSourceLines(kStarter);
    ASSERT_EQ(lines.size(), 3);
    EXPECT_EQ(lines[0], "French horn: 18 recordings from VSCO 2 Community Edition (Versilian Studios), "
                        "github.com/sgossner/VSCO-2-CE");
}

TEST(SoundPackSourceList, SingularZeroAndMissingUrlFormats) {
    const auto lines = soundPackSourceLines("## One (`one`)\n- Source pack: A\n- URL: https://a.test/x\n"
                                            "- Files used (1): a.wav\n\n"
                                            "## None (`none`)\n- Source pack: B\n- URL: https://b.test\n\n"
                                            "## Bare (`bare`)\n- Source pack: C\n- Files used (2): a.wav, b.wav\n");
    ASSERT_EQ(lines.size(), 3);
    EXPECT_EQ(lines[0], "One: 1 recording from A, a.test/x");
    EXPECT_EQ(lines[1], "None: from B, b.test");
    EXPECT_EQ(lines[2], "Bare: 2 recordings from C");
}

TEST(SoundPackSourceList, AFileWithoutSectionsFallsBackToItsFirstLinesAtMostTwelve) {
    juce::String text = "# Sources\n\n";
    for (int i = 1; i <= 15; ++i)
        text << "- Made by `person " << i << "`\n";
    const auto lines = soundPackSourceLines(text);
    ASSERT_EQ(lines.size(), 12);
    EXPECT_EQ(lines[0], "Made by person 1");
    EXPECT_TRUE(soundPackSourceLines("").isEmpty());
}
