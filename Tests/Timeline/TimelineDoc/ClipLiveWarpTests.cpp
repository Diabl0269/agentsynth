// A warped audio clip (Clip::sourceBpm): the setter's validation, how the tempo follows split, duplicate, trim and
// range copies, and that every beat-to-source advance uses the clip's own beat length instead of the song's.

#include "TimelineDocTestHelpers.h"

#include <juce_core/juce_core.h>

namespace {
constexpr double kSongSecondsPerBeat = 0.25; // 240 bpm
constexpr double kWarpedBpm = 120.0;         // one clip beat = 0.5 s of the asset

ClipId makeWarpedClip(TimelineDoc& doc, double sourceStart = 1.0) {
    const auto track = doc.addTrack(TrackKind::Audio, "A");
    const auto clip = doc.addClip(track, 0.0, 8.0, "loop");
    EXPECT_TRUE(doc.setClipAsset(clip, "Audio/loop.wav", sourceStart));
    EXPECT_TRUE(doc.setClipSourceBpm(clip, kWarpedBpm));
    return clip;
}
} // namespace

TEST_F(TimelineDocTest, SourceBpmAcceptsZeroAndTempiAndRejectsTheRest) {
    const auto clip = makeWarpedClip(doc);
    EXPECT_TRUE(doc.setClipSourceBpm(clip, 0.0));
    EXPECT_DOUBLE_EQ(doc.getClip(clip)->sourceBpm, 0.0);
    EXPECT_TRUE(doc.setClipSourceBpm(clip, 120.0));
    EXPECT_DOUBLE_EQ(doc.getClip(clip)->sourceBpm, 120.0);

    EXPECT_FALSE(doc.setClipSourceBpm(clip, 5.0));
    EXPECT_FALSE(doc.setClipSourceBpm(clip, 2000.0));
    EXPECT_FALSE(doc.setClipSourceBpm(clip, -1.0));
    EXPECT_FALSE(doc.setClipSourceBpm(clip, std::numeric_limits<double>::quiet_NaN()));
    EXPECT_FALSE(doc.setClipSourceBpm(clip, std::numeric_limits<double>::infinity()));
    EXPECT_FALSE(doc.setClipSourceBpm(ClipId{9999}, 120.0));
    EXPECT_DOUBLE_EQ(doc.getClip(clip)->sourceBpm, 120.0) << "a refused value leaves the clip alone";
}

TEST_F(TimelineDocTest, SettingTheSameSourceBpmDoesNotBumpTheRevision) {
    const auto clip = makeWarpedClip(doc);
    const auto revision = doc.getRevision();
    EXPECT_TRUE(doc.setClipSourceBpm(clip, kWarpedBpm));
    EXPECT_EQ(doc.getRevision(), revision);
    EXPECT_TRUE(doc.setClipSourceBpm(clip, 90.0));
    EXPECT_GT(doc.getRevision(), revision);
}

TEST_F(TimelineDocTest, PointingAWarpedClipAtAnotherAssetKeepsItsTempo) {
    const auto clip = makeWarpedClip(doc);
    ASSERT_TRUE(doc.setClipAsset(clip, "Audio/other.wav", 0.0));
    EXPECT_DOUBLE_EQ(doc.getClip(clip)->sourceBpm, kWarpedBpm);
}

TEST_F(TimelineDocTest, SplittingAWarpedClipAdvancesByItsOwnBeatAndKeepsTheTempo) {
    const auto clip = makeWarpedClip(doc);
    const auto right = doc.splitClipAtTempo(clip, 4.0, kSongSecondsPerBeat).second;
    ASSERT_TRUE(right.isValid());
    EXPECT_DOUBLE_EQ(doc.getClip(right)->sourceStartSeconds, 1.0 + 4.0 * 0.5);
    EXPECT_DOUBLE_EQ(doc.getClip(right)->sourceBpm, kWarpedBpm);
    EXPECT_DOUBLE_EQ(doc.getClip(clip)->sourceStartSeconds, 1.0);
}

TEST_F(TimelineDocTest, ATempoLessSplitStillAdvancesAWarpedClipButNotAnUnwarpedOne) {
    const auto warped = makeWarpedClip(doc);
    const auto warpedRight = doc.splitClip(warped, 2.0).second;
    ASSERT_TRUE(warpedRight.isValid());
    EXPECT_DOUBLE_EQ(doc.getClip(warpedRight)->sourceStartSeconds, 1.0 + 2.0 * 0.5);

    const auto plain = makeWarpedClip(doc);
    ASSERT_TRUE(doc.setClipSourceBpm(plain, 0.0));
    const auto plainRight = doc.splitClip(plain, 2.0).second;
    ASSERT_TRUE(plainRight.isValid());
    EXPECT_DOUBLE_EQ(doc.getClip(plainRight)->sourceStartSeconds, 1.0);
}

TEST_F(TimelineDocTest, AnUnwarpedSplitStillUsesTheSongTempo) {
    const auto clip = makeWarpedClip(doc);
    ASSERT_TRUE(doc.setClipSourceBpm(clip, 0.0));
    const auto right = doc.splitClipAtTempo(clip, 4.0, kSongSecondsPerBeat).second;
    ASSERT_TRUE(right.isValid());
    EXPECT_DOUBLE_EQ(doc.getClip(right)->sourceStartSeconds, 1.0 + 4.0 * kSongSecondsPerBeat);
    EXPECT_DOUBLE_EQ(doc.getClip(right)->sourceBpm, 0.0);
}

TEST_F(TimelineDocTest, TrimmingAWarpedClipsStartAdvancesByItsOwnBeat) {
    const auto clip = makeWarpedClip(doc);
    ASSERT_TRUE(doc.trimClipStart(clip, 3.0, kSongSecondsPerBeat));
    EXPECT_DOUBLE_EQ(doc.getClip(clip)->sourceStartSeconds, 1.0 + 3.0 * 0.5);
    EXPECT_DOUBLE_EQ(doc.getClip(clip)->sourceBpm, kWarpedBpm);
}

TEST_F(TimelineDocTest, ARangeFragmentOfAWarpedClipAdvancesByItsOwnBeat) {
    const auto clip = makeWarpedClip(doc);
    const auto fragment = TimelineDoc::clipToRange(*doc.getClip(clip), 2.0, 6.0, kSongSecondsPerBeat);
    ASSERT_TRUE(fragment.has_value());
    EXPECT_DOUBLE_EQ(fragment->sourceStartSeconds, 1.0 + 2.0 * 0.5);
    EXPECT_DOUBLE_EQ(fragment->sourceBpm, kWarpedBpm);
}

TEST_F(TimelineDocTest, DuplicatingAWarpedClipKeepsItsTempo) {
    const auto clip = makeWarpedClip(doc);
    const auto dup = doc.duplicateClip(clip);
    ASSERT_TRUE(dup.isValid());
    EXPECT_DOUBLE_EQ(doc.getClip(dup)->sourceBpm, kWarpedBpm);
}

TEST_F(TimelineDocTest, TheSourceBpmSurvivesSaveAndLoadAndIsWrittenOnlyWhenSet) {
    const auto clip = makeWarpedClip(doc);
    const auto text = juce::JSON::toString(doc.toVar());
    EXPECT_TRUE(text.contains("sourceBpm"));
    TimelineDoc loaded;
    ASSERT_TRUE(loaded.fromVar(juce::JSON::parse(text)));
    EXPECT_DOUBLE_EQ(loaded.getClip(clip)->sourceBpm, kWarpedBpm);

    ASSERT_TRUE(doc.setClipSourceBpm(clip, 0.0));
    const auto plainText = juce::JSON::toString(doc.toVar());
    EXPECT_FALSE(plainText.contains("sourceBpm"));
    TimelineDoc plain;
    ASSERT_TRUE(plain.fromVar(juce::JSON::parse(plainText)));
    EXPECT_DOUBLE_EQ(plain.getClip(clip)->sourceBpm, 0.0) << "absent loads as unwarped";
}

TEST_F(TimelineDocTest, AnIllegalSavedSourceBpmMakesTheProjectMalformed) {
    makeWarpedClip(doc);
    const auto good = juce::JSON::toString(doc.toVar());
    for (const char* bad : {"5.0", "-1.0", "2000.0", "\"fast\""}) {
        const auto text = good.replace("\"sourceBpm\": 120.0", juce::String("\"sourceBpm\": ") + bad)
                              .replace("\"sourceBpm\":120.0", juce::String("\"sourceBpm\":") + bad);
        ASSERT_NE(text, good) << "the fixture must have rewritten the value";
        TimelineDoc loaded;
        EXPECT_FALSE(loaded.fromVar(juce::JSON::parse(text))) << bad;
    }
}
