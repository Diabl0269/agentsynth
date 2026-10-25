// Concern: the song key's music theory in Music/SongKey.h: names, the saved form, the picker's query parse,
// relative keys and key detection.
#include "Music/SongKey.h"
#include <gtest/gtest.h>

using namespace synth;

namespace {
MusicalScale keyOf(int root, const char* scale) { return makeScale(root, scalePresetIndex(scale)); }
} // namespace

TEST(SongKeyTest, DisplayNameAndNotes) {
    EXPECT_EQ(keyDisplayName(keyOf(2, "Dorian")), "D Dorian");
    EXPECT_EQ(keyNoteNames(keyOf(2, "Dorian")), "D E F G A B C");
    EXPECT_EQ(keyDisplayName(makeScaleFromMask(1, 0x091)), "C# Custom");
}

TEST(SongKeyTest, SavedFormRoundTripsAndRejectsAnythingElse) {
    const auto key = keyOf(9, "Natural Minor");
    const auto back = keyFromVar(keyToVar(key));
    ASSERT_TRUE(back.has_value());
    EXPECT_TRUE(sameKey(*back, key));

    auto bad = [](int root, int mask) {
        auto* obj = new juce::DynamicObject();
        obj->setProperty("root", root);
        obj->setProperty("mask", mask);
        return keyFromVar(juce::var(obj));
    };
    EXPECT_FALSE(bad(12, 0xAB5).has_value());
    EXPECT_FALSE(bad(-1, 0xAB5).has_value());
    EXPECT_FALSE(bad(0, 0).has_value());
    EXPECT_FALSE(bad(0, 0x1000).has_value());
    EXPECT_TRUE(bad(0, 0xAB5).has_value()) << "a missing name is allowed";
    EXPECT_FALSE(keyFromVar(juce::var("C major")).has_value());
    EXPECT_FALSE(keyFromVar({}).has_value());
}

TEST(SongKeyTest, SameKeyComparesRootMaskAndName) {
    EXPECT_TRUE(sameKey(std::optional<MusicalScale>{}, std::optional<MusicalScale>{}));
    EXPECT_FALSE(sameKey(std::optional<MusicalScale>{keyOf(0, "Major")}, std::optional<MusicalScale>{}));
    EXPECT_FALSE(sameKey(keyOf(0, "Major"), keyOf(2, "Major")));
    EXPECT_FALSE(sameKey(keyOf(0, "Major"), keyOf(0, "Lydian")));
}

TEST(SongKeyTest, ParsesRootNames) {
    EXPECT_EQ(parseKeyRoot("d"), 2);
    EXPECT_EQ(parseKeyRoot("F#"), 6);
    EXPECT_EQ(parseKeyRoot("bb"), 10);
    EXPECT_EQ(parseKeyRoot("Eb"), 3);
    EXPECT_EQ(parseKeyRoot("cb"), 11);
    EXPECT_EQ(parseKeyRoot("b#"), 0);
    EXPECT_EQ(parseKeyRoot("h"), -1);
    EXPECT_EQ(parseKeyRoot("dor"), -1);
    EXPECT_EQ(parseKeyRoot(""), -1);
}

TEST(SongKeyTest, QuerySplitsARootFromTheScaleWords) {
    auto q = parseKeyQuery("d dor");
    EXPECT_EQ(q.root, 2);
    EXPECT_EQ(q.scaleWords, "dor");
    q = parseKeyQuery("  a   min ");
    EXPECT_EQ(q.root, 9);
    EXPECT_EQ(q.scaleWords, "min");
    q = parseKeyQuery("b");
    EXPECT_EQ(q.root, 11);
    EXPECT_TRUE(q.scaleWords.isEmpty());
    q = parseKeyQuery("blues");
    EXPECT_EQ(q.root, -1);
    EXPECT_EQ(q.scaleWords, "blues");
    q = parseKeyQuery("");
    EXPECT_EQ(q.root, -1);
    EXPECT_TRUE(q.scaleWords.isEmpty());
}

TEST(SongKeyTest, GroupsCoverEveryPresetButChromatic) {
    int count = 0;
    for (const auto& group : keyScaleGroups())
        for (const auto* name : group.scales) {
            EXPECT_GE(scalePresetIndex(name), 0) << name;
            EXPECT_STRNE(name, "Chromatic");
            ++count;
        }
    EXPECT_EQ(count, (int)builtInScalePresets().size() - 1);
}

TEST(SongKeyTest, RelativeKeys) {
    auto rel = relativeKey(keyOf(0, "Major"));
    ASSERT_TRUE(rel.has_value());
    EXPECT_EQ(keyDisplayName(*rel), "A Natural Minor");
    rel = relativeKey(keyOf(9, "Natural Minor"));
    ASSERT_TRUE(rel.has_value());
    EXPECT_EQ(keyDisplayName(*rel), "C Major");
    rel = relativeKey(keyOf(4, "Minor Pentatonic"));
    ASSERT_TRUE(rel.has_value());
    EXPECT_EQ(keyDisplayName(*rel), "G Major Pentatonic");
    EXPECT_FALSE(relativeKey(keyOf(2, "Dorian")).has_value());
}

TEST(SongKeyTest, DetectsTheKeyOfAHistogram) {
    EXPECT_FALSE(detectKey({}).has_value());

    std::array<double, 12> cMajor{};
    for (int pc : {0, 2, 4, 5, 7, 9, 11})
        cMajor[(size_t)pc] = 1.0;
    cMajor[0] = 4.0; // tonic held
    cMajor[7] = 2.0;
    auto key = detectKey(cMajor);
    ASSERT_TRUE(key.has_value());
    EXPECT_EQ(keyDisplayName(*key), "C Major");

    std::array<double, 12> aMinor{};
    for (int pc : {9, 11, 0, 2, 4, 5, 7})
        aMinor[(size_t)pc] = 1.0;
    aMinor[9] = 4.0;
    aMinor[4] = 2.0;
    aMinor[0] = 1.5;
    key = detectKey(aMinor);
    ASSERT_TRUE(key.has_value());
    EXPECT_EQ(keyDisplayName(*key), "A Natural Minor");
}

TEST(SongKeyTest, RangeFollowsTheNearestStepOfTheRoot) {
    EXPECT_EQ(rangeFollowingRoot(36, 48, 0, 2), (std::pair<int, int>{38, 50}));
    EXPECT_EQ(rangeFollowingRoot(36, 48, 0, 11), (std::pair<int, int>{35, 47})) << "B is one step down from C";
    EXPECT_EQ(rangeFollowingRoot(36, 48, 2, 2), (std::pair<int, int>{36, 48})) << "same root, same range";
}

TEST(SongKeyTest, RangeFollowingARootClampsInsideTheKeyboardKeepingItsWidth) {
    const auto [lo, hi] = rangeFollowingRoot(120, 127, 0, 6);
    EXPECT_GE(lo, 0);
    EXPECT_LE(hi, 127);
    EXPECT_EQ(hi - lo, 7);
    const auto [lowLo, lowHi] = rangeFollowingRoot(0, 12, 2, 0);
    EXPECT_EQ(lowLo, 0);
    EXPECT_EQ(lowHi - lowLo, 12);
}
