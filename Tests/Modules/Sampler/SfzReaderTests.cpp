// SfzReaderTests.cpp -- the SFZ subset: inheritance, value rules, note names, loops, triggers, warnings.
#include "Modules/Sampler/SfzReader.h"
#include "SamplerTestWavs.h"
#include <gtest/gtest.h>

using namespace synth::sampler;
using namespace sampler_test;

namespace {

const juce::File kDir("/packs/piano");

SfzParseResult parse(const juce::String& text) { return parseSfz(text, kDir); }

bool anyWarningContains(const SfzParseResult& r, const juce::String& needle) {
    for (const auto& w : r.warnings)
        if (w.contains(needle))
            return true;
    return false;
}

} // namespace

TEST(SfzReaderTest, OpcodesInheritDownTheHierarchyAndGroupsReset) {
    const auto r = parse("<global> volume=-3 lovel=10\n"
                         "<master> hivel=100\n"
                         "<group> lokey=c4 hikey=c5\n"
                         "<region> sample=a.wav\n"
                         "<region> sample=b.wav hikey=70\n"
                         "<group> key=40\n"
                         "<region> sample=c.wav\n"
                         "<master>\n"
                         "<region> sample=d.wav\n");
    ASSERT_EQ(r.regions.size(), 4u);
    EXPECT_EQ(r.regions[0].loKey, 60);
    EXPECT_EQ(r.regions[0].hiKey, 72);
    EXPECT_EQ(r.regions[0].loVel, 10);
    EXPECT_EQ(r.regions[0].hiVel, 100);
    EXPECT_FLOAT_EQ(r.regions[0].volumeDb, -3.0f);
    EXPECT_EQ(r.regions[1].hiKey, 70);
    EXPECT_EQ(r.regions[2].loKey, 40);
    EXPECT_EQ(r.regions[2].hiKey, 40);
    EXPECT_EQ(r.regions[2].rootKey, 40);
    EXPECT_EQ(r.regions[2].hiVel, 100);
    // A new master drops the master and group opcodes but keeps global.
    EXPECT_EQ(r.regions[3].hiVel, 127);
    EXPECT_EQ(r.regions[3].loKey, 0);
    EXPECT_FLOAT_EQ(r.regions[3].volumeDb, -3.0f);
}

TEST(SfzReaderTest, SamplePathsKeepSpacesAndFlipBackslashes) {
    const auto r = parse("<region> sample=Piano C4 soft.wav lokey=60 hikey=62\n"
                         "<region> sample=sub\\dir\\x.wav key=61\n");
    ASSERT_EQ(r.regions.size(), 2u);
    EXPECT_EQ(r.regions[0].samplePath, kDir.getChildFile("Piano C4 soft.wav").getFullPathName());
    EXPECT_EQ(r.regions[0].loKey, 60);
    EXPECT_EQ(r.regions[0].hiKey, 62);
    EXPECT_EQ(r.regions[1].samplePath, kDir.getChildFile("sub/dir/x.wav").getFullPathName());
}

TEST(SfzReaderTest, DefaultPathPrefixesSamples) {
    const auto r = parse("<control> default_path=Samples\\\n<region> sample=a.wav\n");
    ASSERT_EQ(r.regions.size(), 1u);
    EXPECT_EQ(r.regions[0].samplePath, kDir.getChildFile("Samples/a.wav").getFullPathName());
}

TEST(SfzReaderTest, NoteNamesUseOctaveFourForMiddleC) {
    const auto r = parse("<region> sample=a.wav lokey=C4 hikey=B4 pitch_keycenter=c#4\n"
                         "<region> sample=b.wav key=Bb3\n"
                         "<region> sample=c.wav\n");
    ASSERT_EQ(r.regions.size(), 3u);
    EXPECT_EQ(r.regions[0].loKey, 60);
    EXPECT_EQ(r.regions[0].hiKey, 71);
    EXPECT_EQ(r.regions[0].rootKey, 61);
    EXPECT_EQ(r.regions[1].rootKey, 58);
    EXPECT_EQ(r.regions[2].rootKey, 60);
}

TEST(SfzReaderTest, DefinesSubstituteIntoLaterValues) {
    const auto r = parse("#define $LO 36\n#define $NAME kick one\n<region> sample=$NAME.wav lokey=$LO key=50\n");
    ASSERT_EQ(r.regions.size(), 1u);
    EXPECT_TRUE(r.regions[0].samplePath.endsWith("kick one.wav"));
    EXPECT_EQ(r.regions[0].loKey, 36);
}

TEST(SfzReaderTest, UnknownOpcodesWarnOnceWithDistinctNames) {
    const auto r = parse("<region> sample=a.wav cutoff=100 resonance=2\n<region> sample=b.wav cutoff=50\n");
    EXPECT_EQ(r.regions.size(), 2u);
    int count = 0;
    for (const auto& w : r.warnings)
        if (w.contains("cutoff")) {
            ++count;
            EXPECT_TRUE(w.contains("resonance"));
            EXPECT_EQ(w.indexOf("cutoff"), w.lastIndexOf("cutoff"));
        }
    EXPECT_EQ(count, 1);
}

TEST(SfzReaderTest, LoopModesLoopPointsAndTuning) {
    const auto r = parse("<region> sample=a.wav loop_mode=loop_continuous loop_start=100 loop_end=900 tune=-12 "
                         "transpose=2 ampeg_release=0.5\n"
                         "<region> sample=b.wav loop_mode=one_shot\n"
                         "<region> sample=c.wav loop_mode=loop_sustain loopstart=5 loopend=50\n"
                         "<region> sample=d.wav loop_mode=no_loop\n");
    ASSERT_EQ(r.regions.size(), 4u);
    EXPECT_EQ(r.regions[0].loop, RegionSpec::Loop::Continuous);
    EXPECT_EQ(r.regions[0].loopStart, 100);
    EXPECT_EQ(r.regions[0].loopEnd, 900);
    EXPECT_FLOAT_EQ(r.regions[0].tuneCents, 188.0f);
    EXPECT_FLOAT_EQ(r.regions[0].ampReleaseSeconds, 0.5f);
    EXPECT_EQ(r.regions[1].loop, RegionSpec::Loop::OneShot);
    EXPECT_EQ(r.regions[2].loop, RegionSpec::Loop::Sustain);
    EXPECT_EQ(r.regions[2].loopStart, 5);
    EXPECT_EQ(r.regions[2].loopEnd, 50);
    EXPECT_EQ(r.regions[3].loop, RegionSpec::Loop::None);
}

TEST(SfzReaderTest, TriggerAndRoundRobinOpcodes) {
    const auto r = parse("<region> sample=a.wav trigger=release\n"
                         "<region> sample=b.wav trigger=attack seq_length=3 seq_position=2\n"
                         "<region> sample=c.wav trigger=legato\n");
    ASSERT_EQ(r.regions.size(), 3u);
    EXPECT_TRUE(r.regions[0].releaseTrigger);
    EXPECT_FALSE(r.regions[1].releaseTrigger);
    EXPECT_EQ(r.regions[1].seqLength, 3);
    EXPECT_EQ(r.regions[1].seqPosition, 2);
    EXPECT_FALSE(r.regions[2].releaseTrigger);
}

TEST(SfzReaderTest, CommentsAndIgnoredSections) {
    const auto r = parse("// header\n<region> sample=a.wav // trailing\n/* block\n sample=zzz.wav */\n"
                         "<curve> curve_index=1\n<effect> type=reverb\n<effect> type=delay\n"
                         "<region> sample=b.wav\n#include \"other.sfz\"\n");
    ASSERT_EQ(r.regions.size(), 2u);
    EXPECT_TRUE(r.regions[0].samplePath.endsWith("a.wav"));
    EXPECT_TRUE(anyWarningContains(r, "curve"));
    EXPECT_TRUE(anyWarningContains(r, "effect"));
    EXPECT_TRUE(anyWarningContains(r, "#include"));
    int effectWarnings = 0;
    for (const auto& w : r.warnings)
        effectWarnings += w.contains("effect") ? 1 : 0;
    EXPECT_EQ(effectWarnings, 1);
}

TEST(SfzReaderTest, RegionWithoutSampleIsDroppedWithAWarning) {
    const auto r = parse("<region> lokey=60\n<region> sample=a.wav\n");
    ASSERT_EQ(r.regions.size(), 1u);
    EXPECT_TRUE(anyWarningContains(r, "without a sample"));
}

TEST(SfzReaderTest, LoadsAnInstrumentFromDisk) {
    TempDir dir;
    ASSERT_TRUE(writeWav(dir.file("Samples/low.wav"), 800, 1, 44100.0, [](int, int) { return 0.1f; }));
    ASSERT_TRUE(writeWav(dir.file("Samples/high.wav"), 600, 2, 44100.0, [](int, int) { return 0.2f; }));
    const juce::String sfz = "<control> default_path=Samples/\n"
                             "<group> loop_mode=one_shot\n"
                             "<region> sample=low.wav lokey=0 hikey=59 pitch_keycenter=48\n"
                             "<region> sample=high.wav lokey=60 hikey=127 pitch_keycenter=72\n";
    ASSERT_TRUE(dir.file("My Piano.sfz").replaceWithText(sfz));

    juce::StringArray errors;
    auto inst = loadSfzInstrument(dir.file("My Piano.sfz"), errors);
    ASSERT_NE(inst, nullptr) << errors.joinIntoString(", ");
    EXPECT_EQ(inst->name, "My Piano");
    ASSERT_EQ(inst->regions.size(), 2u);
    EXPECT_EQ(inst->regions[0].rootKey, 48);
    EXPECT_EQ(inst->regions[1].rootKey, 72);
    EXPECT_EQ(inst->regions[1].loop, RegionSpec::Loop::OneShot);
    EXPECT_EQ(inst->regions[0].sample->audio.getNumSamples(), 800);
    EXPECT_EQ(inst->regions[1].sample->audio.getNumChannels(), 2);
    EXPECT_TRUE(errors.isEmpty());

    EXPECT_EQ(loadSfzInstrument(dir.file("nope.sfz"), errors), nullptr);
    EXPECT_FALSE(errors.isEmpty());
}
