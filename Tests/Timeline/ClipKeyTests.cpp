// Concern: synth::resolveClipKey, the one rule for which key a clip uses: its own, else the song key it follows.
#include "Music/SongKey.h"
#include "Timeline/ClipKey.h"
#include <gtest/gtest.h>

namespace {
synth::MusicalScale keyOf(int root, const char* scale) {
    return synth::makeScale(root, synth::scalePresetIndex(scale));
}
} // namespace

TEST(ClipKeyTest, AnOwnKeyWinsOverTheSongKey) {
    const auto resolved = synth::resolveClipKey(keyOf(9, "Phrygian"), keyOf(2, "Dorian"));
    EXPECT_FALSE(resolved.following);
    ASSERT_TRUE(resolved.key.has_value());
    EXPECT_EQ(resolved.key->rootPitchClass, 9);
    EXPECT_EQ(juce::String(resolved.key->name), "Phrygian");
}

TEST(ClipKeyTest, AnOwnKeyWinsEvenWhenTheSongHasNoKey) {
    const auto resolved = synth::resolveClipKey(keyOf(0, "Major"), std::nullopt);
    EXPECT_FALSE(resolved.following);
    EXPECT_TRUE(resolved.key.has_value());
}

TEST(ClipKeyTest, NoOwnKeyFollowsTheSongKey) {
    const auto resolved = synth::resolveClipKey(std::nullopt, keyOf(2, "Dorian"));
    EXPECT_TRUE(resolved.following);
    ASSERT_TRUE(resolved.key.has_value());
    EXPECT_EQ(resolved.key->rootPitchClass, 2);
}

TEST(ClipKeyTest, FollowingASongWithNoKeyResolvesToNoKey) {
    const auto resolved = synth::resolveClipKey(std::nullopt, std::nullopt);
    EXPECT_TRUE(resolved.following);
    EXPECT_FALSE(resolved.key.has_value());
}

TEST(ClipKeyTest, AClipResolvesThroughItsScale) {
    synth::Clip clip;
    EXPECT_TRUE(synth::resolveClipKey(clip, keyOf(5, "Lydian")).following);
    clip.scale = keyOf(7, "Blues");
    const auto resolved = synth::resolveClipKey(clip, keyOf(5, "Lydian"));
    EXPECT_FALSE(resolved.following);
    EXPECT_EQ(resolved.key->rootPitchClass, 7);
}
