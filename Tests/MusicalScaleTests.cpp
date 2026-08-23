// Tests for the musical-scale engine: membership, nearest-pitch snapping, built-in presets,
// user-scale JSON persistence, and scale-constrained random note generation.

#include "../Source/Timeline/MusicalScale.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <set>

using synth::generateRandomNotes;
using synth::makeScale;
using synth::MusicalScale;
using synth::parseUserScales;
using synth::ScalePreset;
using synth::serializeUserScales;
using synth::UserScale;

namespace {

// C Major: 0,2,4,5,7,9,11
MusicalScale cMajor() {
    return makeScale(0, 0); // index 0 == "Major"
}

int presetIndexByName(const char* name) {
    const auto& presets = synth::builtInScalePresets();
    for (size_t i = 0; i < presets.size(); ++i)
        if (juce::String(presets[i].name) == juce::String(name))
            return (int)i;
    return -1;
}

} // namespace

//==============================================================================
// contains()
//==============================================================================

TEST(MusicalScaleTest, ContainsAcrossOctaves) {
    auto scale = cMajor();
    // C4=60, D4=62, E4=64 ... within scale across multiple octaves.
    EXPECT_TRUE(scale.contains(60));  // C4
    EXPECT_TRUE(scale.contains(48));  // C3
    EXPECT_TRUE(scale.contains(72));  // C5
    EXPECT_TRUE(scale.contains(62));  // D4
    EXPECT_TRUE(scale.contains(64));  // E4
    EXPECT_TRUE(scale.contains(65));  // F4
    EXPECT_TRUE(scale.contains(67));  // G4
    EXPECT_TRUE(scale.contains(69));  // A4
    EXPECT_TRUE(scale.contains(71));  // B4
    EXPECT_FALSE(scale.contains(61)); // C#4
    EXPECT_FALSE(scale.contains(66)); // F#4
    EXPECT_FALSE(scale.contains(70)); // A#4
}

TEST(MusicalScaleTest, ContainsAtRangeEdges) {
    auto scale = cMajor();
    EXPECT_TRUE(scale.contains(0));   // C0 - in scale
    EXPECT_FALSE(scale.contains(1));  // C#0 - not in scale
    EXPECT_TRUE(scale.contains(127)); // 127 % 12 == 7 (G) - in scale
}

TEST(MusicalScaleTest, DegenerateMaskContainsNothing) {
    MusicalScale scale;
    scale.mask = 0;
    for (int p = 0; p <= 127; p += 13)
        EXPECT_FALSE(scale.contains(p));
}

TEST(MusicalScaleTest, ChromaticContainsEverything) {
    MusicalScale scale = makeScale(0, presetIndexByName("Chromatic"));
    EXPECT_TRUE(scale.isChromatic());
    for (int p = 0; p <= 127; ++p)
        EXPECT_TRUE(scale.contains(p));
}

//==============================================================================
// snapPitch()
//==============================================================================

TEST(MusicalScaleTest, SnapPitchReturnsSameWhenAlreadyInScale) {
    auto scale = cMajor();
    EXPECT_EQ(scale.snapPitch(60), 60);
    EXPECT_EQ(scale.snapPitch(67), 67);
}

TEST(MusicalScaleTest, SnapPitchNearestNeighbour) {
    auto scale = cMajor();
    // C#4 (61) is 1 semitone from both C4 (60, below) and D4 (62, above) -> tie -> lower.
    EXPECT_EQ(scale.snapPitch(61), 60);
    // F#4 (66) is 1 semitone from F4 (65, below) and G4 (67, above) -> tie -> lower.
    EXPECT_EQ(scale.snapPitch(66), 65);
}

TEST(MusicalScaleTest, SnapPitchClosestWhenNotTied) {
    // Minor pentatonic root C: 0,3,5,7,10. Pitch 1 is distance 1 from 0, distance 2 from 3 -> 0.
    MusicalScale scale = makeScale(0, presetIndexByName("Minor Pentatonic"));
    EXPECT_EQ(scale.snapPitch(1), 0);
    // Pitch 4 is distance 1 from 3, distance 1 from 5 -> tie -> lower (3).
    EXPECT_EQ(scale.snapPitch(4), 3);
}

TEST(MusicalScaleTest, SnapPitchBoundaryClampsWithSparseScale) {
    // A sparse scale (single bit, root only) still must resolve at both range extremes.
    MusicalScale scale;
    scale.rootPitchClass = 0;
    scale.mask = 0x0001; // only the root pitch class (0, 12, 24 ... 120) is in scale
    EXPECT_EQ(scale.snapPitch(0), 0);
    EXPECT_EQ(scale.snapPitch(127), 120); // nearest in-scale pitch <= 127 is 120
}

TEST(MusicalScaleTest, SnapPitchMaskZeroPassesThroughUnchanged) {
    MusicalScale scale;
    scale.mask = 0;
    EXPECT_EQ(scale.snapPitch(0), 0);
    EXPECT_EQ(scale.snapPitch(60), 60);
    EXPECT_EQ(scale.snapPitch(127), 127);
}

//==============================================================================
// Built-in presets
//==============================================================================

TEST(MusicalScaleTest, CMajorContainsExpectedPitchClasses) {
    auto scale = cMajor();
    const std::set<int> inScale = {0, 2, 4, 5, 7, 9, 11};
    for (int pc = 0; pc < 12; ++pc) {
        bool expected = inScale.count(pc) > 0;
        EXPECT_EQ(scale.contains(pc), expected) << "pitch class " << pc;
    }
    EXPECT_TRUE(scale.contains(0));  // C
    EXPECT_TRUE(scale.contains(2));  // D
    EXPECT_TRUE(scale.contains(4));  // E
    EXPECT_TRUE(scale.contains(5));  // F
    EXPECT_TRUE(scale.contains(7));  // G
    EXPECT_TRUE(scale.contains(9));  // A
    EXPECT_TRUE(scale.contains(11)); // B
    EXPECT_FALSE(scale.contains(1)); // C#
}

TEST(MusicalScaleTest, ANaturalMinorSharesPitchClassesWithCMajor) {
    // A Natural Minor is the relative minor of C Major: same absolute pitch-class set, different
    // root. Root-relative masks differ, but membership over the full pitch-class range matches.
    auto major = cMajor();
    MusicalScale aMinor = makeScale(9 /* A */, presetIndexByName("Natural Minor"));
    for (int pc = 0; pc < 12; ++pc)
        EXPECT_EQ(major.contains(pc), aMinor.contains(pc)) << "pitch class " << pc;
}

TEST(MusicalScaleTest, BluesMaskIsExact) {
    MusicalScale scale = makeScale(0, presetIndexByName("Blues"));
    const std::set<int> expected = {0, 3, 5, 6, 7, 10};
    for (int pc = 0; pc < 12; ++pc)
        EXPECT_EQ(scale.contains(pc), expected.count(pc) > 0) << "pitch class " << pc;
}

//==============================================================================
// User scale JSON persistence
//==============================================================================

TEST(MusicalScaleTest, UserScaleRoundTrip) {
    std::vector<UserScale> scales;
    scales.push_back({"My Scale", 0x0AB5});
    scales.push_back({"Another", 0x0555});

    juce::String json = serializeUserScales(scales);
    auto parsed = parseUserScales(json);

    ASSERT_EQ(parsed.size(), 2u);
    EXPECT_EQ(parsed[0].name, "My Scale");
    EXPECT_EQ(parsed[0].mask, 0x0AB5);
    EXPECT_EQ(parsed[1].name, "Another");
    EXPECT_EQ(parsed[1].mask, 0x0555);
}

TEST(MusicalScaleTest, ParseEmptyStringYieldsEmpty) {
    EXPECT_TRUE(parseUserScales("").empty());
    EXPECT_TRUE(parseUserScales("   ").empty());
}

TEST(MusicalScaleTest, ParseGarbageYieldsEmpty) {
    EXPECT_TRUE(parseUserScales("not json at all {{{").empty());
    EXPECT_TRUE(parseUserScales("42").empty());
    EXPECT_TRUE(parseUserScales("\"a string\"").empty());
}

TEST(MusicalScaleTest, ParseSkipsMalformedEntriesButKeepsValidOnes) {
    juce::String json = "[ {\"name\":\"Good\",\"mask\":100}, "
                        "{\"name\":\"MissingMask\"}, "
                        "{\"mask\":50}, "
                        "\"not an object\", "
                        "{\"name\":\"BadMask\",\"mask\":9999}, "
                        "{\"name\":\"Good2\",\"mask\":200} ]";
    auto parsed = parseUserScales(json);
    ASSERT_EQ(parsed.size(), 2u);
    EXPECT_EQ(parsed[0].name, "Good");
    EXPECT_EQ(parsed[0].mask, 100);
    EXPECT_EQ(parsed[1].name, "Good2");
    EXPECT_EQ(parsed[1].mask, 200);
}

//==============================================================================
// generateRandomNotes()
//==============================================================================

TEST(MusicalScaleTest, GenerateRandomNotesIsDeterministicWithFixedSeed) {
    auto scale = cMajor();
    juce::Random rngA(12345);
    juce::Random rngB(12345);

    auto notesA = generateRandomNotes(4.0, 0.25, 48, 72, &scale, rngA);
    auto notesB = generateRandomNotes(4.0, 0.25, 48, 72, &scale, rngB);

    ASSERT_EQ(notesA.size(), notesB.size());
    for (size_t i = 0; i < notesA.size(); ++i) {
        EXPECT_EQ(notesA[i].pitch, notesB[i].pitch);
        EXPECT_DOUBLE_EQ(notesA[i].startBeat, notesB[i].startBeat);
        EXPECT_DOUBLE_EQ(notesA[i].lengthBeats, notesB[i].lengthBeats);
    }
}

TEST(MusicalScaleTest, GenerateRandomNotesEveryNoteInRangeAndInScale) {
    auto scale = cMajor();
    juce::Random rng(999);
    auto notes = generateRandomNotes(8.0, 0.5, 60, 72, &scale, rng);

    ASSERT_GT(notes.size(), 0u);
    for (auto& note : notes) {
        EXPECT_GE(note.pitch, 60);
        EXPECT_LE(note.pitch, 72);
        EXPECT_TRUE(scale.contains(note.pitch));
        EXPECT_EQ(note.velocity, 100);
        EXPECT_EQ(note.channel, 1);
        EXPECT_FALSE(note.muted);
    }
}

TEST(MusicalScaleTest, GenerateRandomNotesStartsAreExactGridMultiples) {
    juce::Random rng(1);
    auto notes = generateRandomNotes(3.0, 0.5, 40, 80, nullptr, rng);
    ASSERT_EQ(notes.size(), 6u); // 3.0 / 0.5
    for (size_t i = 0; i < notes.size(); ++i)
        EXPECT_DOUBLE_EQ(notes[i].startBeat, (double)i * 0.5);
}

TEST(MusicalScaleTest, GenerateRandomNotesLastNoteLengthClampsAtClipEnd) {
    juce::Random rng(2);
    // Clip length 1.75 beats, grid 0.5 beats -> starts at 0, 0.5, 1.0, 1.5; last note's natural
    // length (0.5) would run past the clip end (1.75), so it must clamp to 0.25.
    auto notes = generateRandomNotes(1.75, 0.5, 40, 80, nullptr, rng);
    ASSERT_EQ(notes.size(), 4u);
    EXPECT_DOUBLE_EQ(notes[0].lengthBeats, 0.5);
    EXPECT_DOUBLE_EQ(notes[1].lengthBeats, 0.5);
    EXPECT_DOUBLE_EQ(notes[2].lengthBeats, 0.5);
    EXPECT_DOUBLE_EQ(notes[3].lengthBeats, 0.25);
}

TEST(MusicalScaleTest, GenerateRandomNotesEmptyWhenNoCandidatePitchInRange) {
    // Minor pentatonic root C (0,3,5,7,10) has no member in [1,2].
    MusicalScale scale = makeScale(0, presetIndexByName("Minor Pentatonic"));
    juce::Random rng(3);
    auto notes = generateRandomNotes(4.0, 0.25, 1, 2, &scale, rng);
    EXPECT_TRUE(notes.empty());
}

TEST(MusicalScaleTest, GenerateRandomNotesChromaticWhenScaleIsNull) {
    juce::Random rng(4);
    auto notes = generateRandomNotes(2.0, 0.25, 60, 61, nullptr, rng);
    ASSERT_GT(notes.size(), 0u);
    for (auto& note : notes)
        EXPECT_TRUE(note.pitch == 60 || note.pitch == 61);
}

TEST(MusicalScaleTest, GenerateRandomNotesEmptyWhenGridBeatsNotPositive) {
    juce::Random rng(5);
    EXPECT_TRUE(generateRandomNotes(4.0, 0.0, 40, 80, nullptr, rng).empty());
    EXPECT_TRUE(generateRandomNotes(4.0, -1.0, 40, 80, nullptr, rng).empty());
}

TEST(MusicalScaleTest, GenerateRandomNotesSwapsReversedRange) {
    juce::Random rngA(6);
    juce::Random rngB(6);
    auto notesA = generateRandomNotes(2.0, 0.5, 72, 60, nullptr, rngA); // reversed
    auto notesB = generateRandomNotes(2.0, 0.5, 60, 72, nullptr, rngB); // normal order
    ASSERT_EQ(notesA.size(), notesB.size());
    for (size_t i = 0; i < notesA.size(); ++i)
        EXPECT_EQ(notesA[i].pitch, notesB[i].pitch);
}
