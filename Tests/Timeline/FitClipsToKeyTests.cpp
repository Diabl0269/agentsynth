// Concern: synth::fitClipsToTheirKeys and synth::fitProjectToSongKey, the many-clip halves of Fit to key, and the
// message a project fit shows. Regression test for FRO1237: a clip with its own key must not change when the project is
// fitted.
#include "Music/SongKey.h"
#include "Timeline/FitClipsToKey.h"
#include <gtest/gtest.h>

namespace {
using namespace synth;

MusicalScale keyOf(int root, const char* scale) { return makeScale(root, scalePresetIndex(scale)); }

struct ProjectFixture {
    TimelineDoc doc;
    TrackId lead = doc.addTrack(TrackKind::Midi, "Lead");
    TrackId drums = doc.addTrack(TrackKind::Midi, "Drums");
    TrackId audioTrack = doc.addTrack(TrackKind::Audio, "Audio");
    ClipId followA = doc.addClip(lead, 0.0, 4.0, "A");
    ClipId followB = doc.addClip(lead, 4.0, 4.0, "B");
    ClipId own = doc.addClip(lead, 8.0, 4.0, "Own");
    ClipId kit = doc.addClip(drums, 0.0, 4.0, "Kit");
    ClipId audio = doc.addClip(audioTrack, 0.0, 4.0, "Take");

    ProjectFixture() {
        for (const auto id : {followA, followB, own, kit})
            doc.addNote(id, MidiNote{{}, 0.0, 1.0, 61}); // C#, out of C major
        doc.setClipScale(own, keyOf(1, "Major"), false); // C# major: 61 is in its key
        doc.setClipAsset(audio, "Audio/take.wav", 0.0);
    }
    int pitch(ClipId id) const { return doc.getClip(id)->notes.front().pitch; }
    bool isKit(const Track& track) const { return track.id == drums; }
    FitProjectResult fit() {
        return fitProjectToSongKey(doc, keyOf(0, "Major"), [this](const Track& t) { return isKit(t); });
    }
};
} // namespace

TEST(FitClipsToKeyTest, ProjectFitMovesClipsThatFollowAndLeavesTheRestAlone) {
    ProjectFixture f;
    const auto result = f.fit();
    EXPECT_EQ(f.pitch(f.followA), 60);
    EXPECT_EQ(f.pitch(f.followB), 60);
    EXPECT_EQ(f.pitch(f.own), 61) << "a clip with its own key never changes";
    EXPECT_EQ(f.pitch(f.kit), 61) << "a drum kit is never fitted";
    EXPECT_EQ(f.doc.getClip(f.audio)->assetRef, "Audio/take.wav");
    EXPECT_EQ(result.clipsMoved, 2);
    EXPECT_EQ(result.skippedOwnKey, 1);
    EXPECT_EQ(result.audioClips, 1);
    EXPECT_TRUE(result.moved());
}

TEST(FitClipsToKeyTest, ProjectFitWithEverythingInKeyMovesNothing) {
    ProjectFixture f;
    f.fit();
    const auto again = f.fit();
    EXPECT_FALSE(again.moved());
    EXPECT_EQ(again.clipsFollowing, 2);
    EXPECT_EQ(describeProjectFit(again, "C Major"),
              "Nothing moved: every note in the clips that follow the song key is already in C Major. Skipped 1 with "
              "their own key. Audio clips were left alone.");
}

TEST(FitClipsToKeyTest, ALinkedGroupWithAnOwnKeyClipIsSkippedWhole) {
    ProjectFixture f;
    const auto sibling = f.doc.duplicateClipLinked(f.followA);
    ASSERT_TRUE(sibling.isValid());
    f.doc.setClipScale(sibling, keyOf(7, "Major"), false); // the sibling shares notes but has its own key
    const auto result = f.fit();
    EXPECT_EQ(f.pitch(f.followA), 61) << "linked clips share their notes, so fitting one would change the other";
    EXPECT_EQ(f.pitch(sibling), 61);
    EXPECT_EQ(result.skippedOwnKey, 3);
}

TEST(FitClipsToKeyTest, ALinkedGroupThatAllFollowsIsFittedOnceAndStaysShared) {
    ProjectFixture f;
    const auto sibling = f.doc.duplicateClipLinked(f.followA);
    ASSERT_TRUE(sibling.isValid());
    const auto result = f.fit();
    EXPECT_EQ(f.pitch(f.followA), 60);
    EXPECT_EQ(f.pitch(sibling), 60);
    EXPECT_EQ(result.clipsMoved, 3);
}

TEST(FitClipsToKeyTest, TheMessageNamesWhatHappenedAndOmitsZeroClauses) {
    EXPECT_EQ(describeProjectFit({5, 5, 2, 1}, "D Dorian"),
              "Fitted 5 clips to D Dorian. Skipped 2 with their own key. Audio clips were left alone.");
    EXPECT_EQ(describeProjectFit({1, 1, 0, 0}, "D Dorian"), "Fitted 1 clip to D Dorian.");
    EXPECT_EQ(describeProjectFit({0, 0, 0, 0}, "D Dorian"), "Nothing moved: no MIDI clip follows the song key.");
}

TEST(FitClipsToKeyTest, EachClipFitsToItsOwnKeyAndAudioAndKeylessClipsAreSkipped) {
    ProjectFixture f;
    f.doc.setClipScale(f.own, keyOf(0, "Major"), false);
    EXPECT_TRUE(fitClipsToTheirKeys(f.doc, {f.followA, f.own, f.audio}, std::nullopt));
    EXPECT_EQ(f.pitch(f.followA), 61) << "no own key and no song key: skipped";
    EXPECT_EQ(f.pitch(f.own), 60);
    EXPECT_FALSE(fitClipsToTheirKeys(f.doc, {f.audio}, keyOf(0, "Major")));
}
