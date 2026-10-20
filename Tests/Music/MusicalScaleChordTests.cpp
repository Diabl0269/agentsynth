// Tests for the shared scale engine's chord helpers: in-scale chords, chord names and roman numerals.

#include "Music/MusicalScale.h"
#include <gtest/gtest.h>
#include <string>
#include <vector>

using synth::chordFromIntervals;
using synth::chordInScale;
using synth::chordName;
using synth::makeScale;
using synth::MusicalScale;
using synth::romanNumeral;
using synth::ScaleChord;
using synth::ScaleChordKind;

namespace {

MusicalScale cMajor() { return makeScale(0, 0); }
MusicalScale cMinor() { return makeScale(0, 1); }

std::vector<int> notesOf(const ScaleChord& chord) {
    return std::vector<int>(chord.notes.begin(), chord.notes.begin() + chord.count);
}

std::string name(const ScaleChord& chord) { return chordName(chord).toStdString(); }

std::string roman(const MusicalScale& scale, const ScaleChord& chord) {
    return romanNumeral(scale, chord).toStdString();
}

} // namespace

TEST(MusicalScaleChordTest, CMajorTriadsOnEveryDegree) {
    auto scale = cMajor();
    const std::vector<std::vector<int>> expected = {{60, 64, 67}, {62, 65, 69}, {64, 67, 71}, {65, 69, 72},
                                                    {67, 71, 74}, {69, 72, 76}, {71, 74, 77}};
    const int roots[7] = {60, 62, 64, 65, 67, 69, 71};
    for (int i = 0; i < 7; ++i)
        EXPECT_EQ(notesOf(chordInScale(scale, roots[i], ScaleChordKind::Triad)), expected[(size_t)i]) << i;
}

TEST(MusicalScaleChordTest, CMajorSeventhNames) {
    auto scale = cMajor();
    const int roots[7] = {60, 62, 64, 65, 67, 69, 71};
    const char* names[7] = {"Cmaj7", "Dm7", "Em7", "Fmaj7", "G7", "Am7", "Bm7b5"};
    for (int i = 0; i < 7; ++i) {
        auto chord = chordInScale(scale, roots[i], ScaleChordKind::Seventh);
        EXPECT_EQ(chord.count, 4);
        EXPECT_EQ(name(chord), names[i]);
    }
    EXPECT_EQ(notesOf(chordInScale(scale, 67, ScaleChordKind::Seventh)), (std::vector<int>{67, 71, 74, 77}));
}

TEST(MusicalScaleChordTest, CMajorTriadNames) {
    auto scale = cMajor();
    const int roots[7] = {60, 62, 64, 65, 67, 69, 71};
    const char* names[7] = {"C", "Dm", "Em", "F", "G", "Am", "Bdim"};
    for (int i = 0; i < 7; ++i)
        EXPECT_EQ(name(chordInScale(scale, roots[i], ScaleChordKind::Triad)), names[i]);
}

TEST(MusicalScaleChordTest, CNaturalMinorTriads) {
    auto scale = cMinor();
    const int roots[7] = {60, 62, 63, 65, 67, 68, 70};
    const char* names[7] = {"Cm", "Ddim", "D#", "Fm", "Gm", "G#", "A#"};
    for (int i = 0; i < 7; ++i)
        EXPECT_EQ(name(chordInScale(scale, roots[i], ScaleChordKind::Triad)), names[i]) << i;
}

TEST(MusicalScaleChordTest, SusAddAndSixthOnC) {
    auto scale = cMajor();
    EXPECT_EQ(name(chordInScale(scale, 60, ScaleChordKind::Sus2)), "Csus2");
    EXPECT_EQ(name(chordInScale(scale, 60, ScaleChordKind::Sus4)), "Csus4");
    EXPECT_EQ(name(chordInScale(scale, 60, ScaleChordKind::Add9)), "Cadd9");
    EXPECT_EQ(name(chordInScale(scale, 60, ScaleChordKind::Sixth)), "C6");
    EXPECT_EQ(notesOf(chordInScale(scale, 60, ScaleChordKind::Add9)), (std::vector<int>{60, 64, 67, 74}));
}

TEST(MusicalScaleChordTest, RootIsSnappedFirst) {
    auto chord = chordInScale(cMajor(), 61, ScaleChordKind::Triad); // C# snaps to C
    EXPECT_EQ(chord.rootPitch, 60);
    EXPECT_EQ(name(chord), "C");
}

TEST(MusicalScaleChordTest, NoScaleFallbackIsMajor) {
    auto chromatic = makeScale(0, 14);
    EXPECT_EQ(name(chordInScale(chromatic, 62, ScaleChordKind::Triad)), "D");
    EXPECT_EQ(name(chordInScale(chromatic, 62, ScaleChordKind::Seventh)), "Dmaj7");
    EXPECT_EQ(name(chordInScale(chromatic, 62, ScaleChordKind::Sus2)), "Dsus2");
    EXPECT_EQ(name(chordInScale(chromatic, 62, ScaleChordKind::Add9)), "Dadd9");
    MusicalScale empty = synth::makeScaleFromMask(0, 0);
    EXPECT_EQ(notesOf(chordInScale(empty, 60, ScaleChordKind::Triad)), (std::vector<int>{60, 64, 67}));
}

TEST(MusicalScaleChordTest, NotesAbove127AreDropped) {
    auto scale = cMajor();
    auto chord = chordInScale(scale, 124, ScaleChordKind::Seventh); // E at 124: E G B ... (124 % 12 == 4)
    EXPECT_EQ(chord.rootPitch, 124);
    EXPECT_EQ(notesOf(chord), (std::vector<int>{124, 127}));
    auto top = chordInScale(scale, 127, ScaleChordKind::Triad);
    EXPECT_EQ(notesOf(top), (std::vector<int>{127}));
}

TEST(MusicalScaleChordTest, ChordFromIntervalsDropsOutOfRange) {
    auto chord = chordFromIntervals(125, {0, 4, 7, 10});
    EXPECT_EQ(notesOf(chord), (std::vector<int>{125}));
    EXPECT_EQ(name(chordFromIntervals(60, std::vector<int>{0, 4, 7, 10})), "C7");
}

TEST(MusicalScaleChordTest, ChordNameTable) {
    struct Row {
        std::vector<int> intervals;
        const char* suffix;
    };
    const std::vector<Row> rows = {
        {{0, 4, 7}, ""},
        {{0, 3, 7}, "m"},
        {{0, 3, 6}, "dim"},
        {{0, 4, 8}, "aug"},
        {{0, 2, 7}, "sus2"},
        {{0, 5, 7}, "sus4"},
        {{0, 4, 7, 11}, "maj7"},
        {{0, 4, 7, 10}, "7"},
        {{0, 3, 7, 10}, "m7"},
        {{0, 3, 6, 10}, "m7b5"},
        {{0, 3, 6, 9}, "dim7"},
        {{0, 3, 7, 11}, "mMaj7"},
        {{0, 4, 8, 11}, "maj7#5"},
        {{0, 4, 7, 14}, "add9"},
        {{0, 3, 7, 14}, "madd9"},
        {{0, 4, 7, 9}, "6"},
        {{0, 3, 7, 9}, "m6"},
        {{0}, ""},
        {{0, 7}, "5"},
        {{0, 12}, ""},
    };
    for (auto& row : rows)
        EXPECT_EQ(name(chordFromIntervals(61, row.intervals)), std::string("C#") + row.suffix);
    EXPECT_EQ(name(chordFromIntervals(60, {0, 1, 2})), "C(0,1,2)");
}

TEST(MusicalScaleChordTest, RomanNumeralsInCMajor) {
    auto scale = cMajor();
    EXPECT_EQ(roman(scale, chordInScale(scale, 60, ScaleChordKind::Triad)), "I");
    EXPECT_EQ(roman(scale, chordInScale(scale, 62, ScaleChordKind::Triad)), "ii");
    EXPECT_EQ(roman(scale, chordInScale(scale, 71, ScaleChordKind::Triad)),
              juce::String::fromUTF8("vii\xC2\xB0").toStdString());
    EXPECT_EQ(roman(scale, chordInScale(scale, 60, ScaleChordKind::Seventh)), "Imaj7");
    EXPECT_EQ(roman(scale, chordInScale(scale, 67, ScaleChordKind::Seventh)), "V7");
    EXPECT_EQ(roman(scale, chordInScale(scale, 62, ScaleChordKind::Seventh)), "ii7");
    EXPECT_EQ(roman(scale, chordInScale(scale, 71, ScaleChordKind::Seventh)),
              (juce::String::fromUTF8("vii\xC3\xB8") + "7").toStdString());
    EXPECT_EQ(roman(scale, chordInScale(scale, 60, ScaleChordKind::Sus4)), "Isus4");
}

TEST(MusicalScaleChordTest, RomanNumeralsInCNaturalMinor) {
    auto scale = cMinor();
    EXPECT_EQ(roman(scale, chordInScale(scale, 60, ScaleChordKind::Triad)), "i");
    EXPECT_EQ(roman(scale, chordInScale(scale, 62, ScaleChordKind::Triad)),
              juce::String::fromUTF8("ii\xC2\xB0").toStdString());
    EXPECT_EQ(roman(scale, chordInScale(scale, 63, ScaleChordKind::Triad)), "III");
    EXPECT_EQ(roman(scale, chordInScale(scale, 67, ScaleChordKind::Triad)), "v");
}

TEST(MusicalScaleChordTest, RomanNumeralsOutsideTheKey) {
    auto scale = cMajor();
    EXPECT_EQ(roman(scale, chordFromIntervals(60, {0, 4, 7, 10})), "I7");
    EXPECT_EQ(roman(scale, chordFromIntervals(70, {0, 4, 7})), "bVII");
    EXPECT_EQ(roman(scale, chordFromIntervals(70, {0, 3, 7})), "bvii");
    EXPECT_EQ(roman(scale, chordFromIntervals(66, {0, 4, 7})), "#IV");
    EXPECT_EQ(roman(scale, chordFromIntervals(64, {0, 4, 8})), "III+");
}

TEST(MusicalScaleChordTest, RomanNumeralsInChromaticScaleUseSemitoneDistance) {
    auto chromatic = makeScale(0, 14);
    EXPECT_EQ(roman(chromatic, chordFromIntervals(62, {0, 3, 7})), "ii");
    EXPECT_EQ(roman(chromatic, chordFromIntervals(63, {0, 4, 7})), "bIII");
}
