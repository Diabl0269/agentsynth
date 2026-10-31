// A stretched audio clip's origin (Clip::stretchOrigin): the window maths a second stretch reads, how the origin
// follows split, duplicate and trim, the save round trip, and the unused-asset walk that must keep the original.

#include "TimelineDocTestHelpers.h"

#include "Timeline/AssetManager.h"
#include "Timeline/TimelineDoc/ClipEdgeSizing.h"
#include <juce_core/juce_core.h>

namespace {
constexpr double kSecondsPerBeat = 0.5; // 120 bpm

// A clip showing the stretched copy "Audio/loop-stretch200.wav": 4 s of it (8 beats), which was the original's 2 s.
ClipId makeStretchedClip(TimelineDoc& doc) {
    const auto track = doc.addTrack(TrackKind::Audio, "A");
    const auto clip = doc.addClip(track, 0.0, 8.0, "loop");
    EXPECT_TRUE(doc.setClipAsset(clip, "Audio/loop-stretch200.wav", 0.0));
    EXPECT_TRUE(doc.setClipStretchOrigin(clip, Clip::StretchOrigin{"Audio/loop.wav", 0.0, 0.5}));
    return clip;
}
} // namespace

TEST_F(TimelineDocTest, ASecondStretchReadsTheOriginalWithTheCumulativeRatio) {
    const auto clip = makeStretchedClip(doc);

    // Stretching the 8-beat clip to 12 beats: the copy played 1.5x faster would be the old logic's ratio of 2/3, but
    // the original is read at 0.5 * (8 / 12).
    const auto window = synth::audioStretchWindow(*doc.getClip(clip), 12.0, kSecondsPerBeat);
    ASSERT_TRUE(window.has_value());
    EXPECT_EQ(window->sourceRef, "Audio/loop.wav");
    EXPECT_DOUBLE_EQ(window->startSeconds, 0.0);
    EXPECT_DOUBLE_EQ(window->lengthSeconds, 2.0); // the 4 s shown are 2 s of the original
    EXPECT_NEAR(window->tempoRatio, 0.5 * 8.0 / 12.0, 1e-12);
    EXPECT_FALSE(window->backToOriginal);
    ASSERT_TRUE(window->origin.has_value());
    EXPECT_EQ(window->origin->assetRef, "Audio/loop.wav");
    EXPECT_NEAR(window->origin->ratio, window->tempoRatio, 1e-12);

    const auto fromCopy = synth::audioStretchWindow(*doc.getClip(clip), 12.0, kSecondsPerBeat, false);
    ASSERT_TRUE(fromCopy.has_value());
    EXPECT_EQ(fromCopy->sourceRef, "Audio/loop-stretch200.wav") << "without the original the copy is the source";
    EXPECT_DOUBLE_EQ(fromCopy->tempoRatio, 8.0 / 12.0);
}

TEST_F(TimelineDocTest, StretchingBackToTheOriginalLengthIsTheOriginalWindowWithNoRender) {
    const auto clip = makeStretchedClip(doc);
    const auto window = synth::audioStretchWindow(*doc.getClip(clip), 4.0, kSecondsPerBeat);
    ASSERT_TRUE(window.has_value());
    EXPECT_TRUE(window->backToOriginal);
    EXPECT_EQ(window->sourceRef, "Audio/loop.wav");
    EXPECT_FALSE(window->origin.has_value());

    ASSERT_TRUE(
        synth::applyAudioClipStretch(doc, clip, 0.0, 4.0, window->sourceRef, window->startSeconds, window->origin));
    EXPECT_EQ(doc.getClip(clip)->assetRef, "Audio/loop.wav");
    EXPECT_FALSE(doc.getClip(clip)->stretchOrigin.has_value());
}

TEST_F(TimelineDocTest, ATrimmedStretchedClipMapsItsShownPartIntoTheOriginal) {
    const auto clip = makeStretchedClip(doc);
    ASSERT_TRUE(doc.trimClipStart(clip, 4.0, kSecondsPerBeat)); // drops the first 2 s of the copy
    ASSERT_DOUBLE_EQ(doc.getClip(clip)->sourceStartSeconds, 2.0);

    const auto window = synth::audioStretchWindow(*doc.getClip(clip), 8.0, kSecondsPerBeat);
    ASSERT_TRUE(window.has_value());
    EXPECT_DOUBLE_EQ(window->startSeconds, 1.0) << "2 s into the copy is 1 s into the original";
    EXPECT_DOUBLE_EQ(window->lengthSeconds, 1.0);
}

TEST_F(TimelineDocTest, SplitAndDuplicateCarryTheStretchOrigin) {
    const auto clip = makeStretchedClip(doc);
    const auto origin = *doc.getClip(clip)->stretchOrigin;

    const auto dup = doc.duplicateClip(clip);
    ASSERT_TRUE(dup.isValid());
    EXPECT_EQ(doc.getClip(dup)->stretchOrigin, origin);

    const auto right = doc.splitClipAtTempo(clip, 4.0, kSecondsPerBeat).second;
    ASSERT_TRUE(right.isValid());
    EXPECT_EQ(doc.getClip(right)->stretchOrigin, origin);
    EXPECT_EQ(doc.getClip(clip)->stretchOrigin, origin);
}

TEST_F(TimelineDocTest, PointingAClipAtAnotherAssetForgetsItsOrigin) {
    const auto clip = makeStretchedClip(doc);
    ASSERT_TRUE(doc.setClipAsset(clip, "Audio/loop-stretch200.wav", 1.0)) << "a slip within the same asset";
    EXPECT_TRUE(doc.getClip(clip)->stretchOrigin.has_value());
    ASSERT_TRUE(doc.setClipAsset(clip, "Audio/other.wav", 0.0));
    EXPECT_FALSE(doc.getClip(clip)->stretchOrigin.has_value());

    EXPECT_FALSE(doc.setClipStretchOrigin(clip, Clip::StretchOrigin{"/abs/loop.wav", 0.0, 0.5}));
    EXPECT_FALSE(doc.setClipStretchOrigin(clip, Clip::StretchOrigin{"Audio/loop.wav", 0.0, 0.0}));
    EXPECT_FALSE(doc.setClipStretchOrigin(clip, Clip::StretchOrigin{"Audio/loop.wav", -1.0, 1.0}));
}

TEST_F(TimelineDocTest, TheStretchOriginSurvivesSaveAndLoad) {
    const auto clip = makeStretchedClip(doc);
    TimelineDoc loaded;
    ASSERT_TRUE(loaded.fromVar(juce::JSON::parse(juce::JSON::toString(doc.toVar()))));
    const auto* c = loaded.getClip(clip);
    ASSERT_NE(c, nullptr);
    ASSERT_TRUE(c->stretchOrigin.has_value());
    EXPECT_EQ(c->stretchOrigin->assetRef, "Audio/loop.wav");
    EXPECT_DOUBLE_EQ(c->stretchOrigin->ratio, 0.5);
}

TEST_F(TimelineDocTest, AProjectWithoutStretchOriginsLoadsUnchanged) {
    const auto track = doc.addTrack(TrackKind::Audio, "A");
    const auto clip = doc.addClip(track, 0.0, 4.0, "plain");
    ASSERT_TRUE(doc.setClipAsset(clip, "Audio/plain.wav", 0.5));
    const auto text = juce::JSON::toString(doc.toVar());
    EXPECT_FALSE(text.contains("stretchOrigin")) << "written only when set";

    TimelineDoc loaded;
    ASSERT_TRUE(loaded.fromVar(juce::JSON::parse(text)));
    EXPECT_FALSE(loaded.getClip(clip)->stretchOrigin.has_value());
    EXPECT_EQ(loaded.getClip(clip)->assetRef, "Audio/plain.wav");
}

TEST_F(TimelineDocTest, AMalformedStretchOriginLoadsAsNoneNotAFailedLoad) {
    const auto clip = makeStretchedClip(doc);
    auto text = juce::JSON::toString(doc.toVar());
    text = text.replace("Audio/loop.wav", "../escape.wav");
    TimelineDoc loaded;
    ASSERT_TRUE(loaded.fromVar(juce::JSON::parse(text)));
    EXPECT_FALSE(loaded.getClip(clip)->stretchOrigin.has_value());
    EXPECT_EQ(loaded.getClip(clip)->assetRef, "Audio/loop-stretch200.wav");
}

TEST_F(TimelineDocTest, TheUnusedAssetWalkKeepsAStretchedClipsOriginal) {
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("stretch-origin-walk");
    dir.deleteRecursively();
    ASSERT_TRUE(dir.getChildFile("Audio").createDirectory());
    for (const char* name : {"loop.wav", "loop-stretch200.wav", "stray.wav"})
        ASSERT_TRUE(dir.getChildFile("Audio").getChildFile(name).replaceWithText("x"));
    makeStretchedClip(doc);

    const auto unused = synth::AssetManager::collectUnusedAssets(doc, dir);
    EXPECT_EQ(unused.size(), 1);
    EXPECT_TRUE(unused.contains("Audio/stray.wav"));
    dir.deleteRecursively();
}
