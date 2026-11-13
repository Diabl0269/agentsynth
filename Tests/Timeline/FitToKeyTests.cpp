// Concern: synth::fitToKeyTargets and synth::fitNotesToKey, the shared "move notes to the nearest note of the key"
// step. Regression test for FRO1138: Fit to key runs only when chosen and moves ties down.
#include "Music/SongKey.h"
#include "Timeline/ClipKey.h"
#include "Timeline/FitToKey.h"
#include <gtest/gtest.h>

namespace {
using namespace synth;

MusicalScale keyOf(int root, const char* scale) { return makeScale(root, scalePresetIndex(scale)); }

struct FitFixture {
    TimelineDoc doc;
    ClipId clip;
    NoteId c, cSharp, d;

    FitFixture() {
        const auto track = doc.addTrack(TrackKind::Midi, "Track 1");
        clip = doc.addClip(track, 0.0, 8.0, "Clip");
        c = doc.addNote(clip, MidiNote{{}, 0.0, 1.0, 60});
        cSharp = doc.addNote(clip, MidiNote{{}, 1.0, 1.0, 61});
        d = doc.addNote(clip, MidiNote{{}, 2.0, 1.0, 62});
    }
    int pitchOf(NoteId id) const { return doc.getNote(id)->pitch; }
};
} // namespace

TEST(FitToKeyTest, TargetsFallBackToEveryNoteWhenNothingIsSelected) {
    FitFixture f;
    EXPECT_EQ(fitToKeyTargets(*f.doc.getClip(f.clip), {}).size(), 3u);
    const auto picked = fitToKeyTargets(*f.doc.getClip(f.clip), {f.d});
    ASSERT_EQ(picked.size(), 1u);
    EXPECT_EQ(picked.front(), f.d);
}

TEST(FitToKeyTest, ATieGoesDown) {
    FitFixture f;
    const auto ids = fitToKeyTargets(*f.doc.getClip(f.clip), {});
    EXPECT_TRUE(fitNotesToKey(f.doc, ids, keyOf(0, "Major")));
    EXPECT_EQ(f.pitchOf(f.c), 60);
    EXPECT_EQ(f.pitchOf(f.cSharp), 60) << "C# sits between C and D; the tie goes down";
    EXPECT_EQ(f.pitchOf(f.d), 62);
}

TEST(FitToKeyTest, ReturnsFalseAndChangesNothingWhenAllNotesAreInKey) {
    FitFixture f;
    EXPECT_FALSE(fitNotesToKey(f.doc, {f.c, f.d}, keyOf(0, "Major")));
    EXPECT_EQ(f.pitchOf(f.c), 60);
    EXPECT_EQ(f.pitchOf(f.d), 62);
}

TEST(FitToKeyTest, AnOwnKeyWinsOverTheSongKey) {
    FitFixture f;
    f.doc.setClipScale(f.clip, keyOf(0, "Major"), false);
    const auto key = resolveClipKey(*f.doc.getClip(f.clip), keyOf(1, "Major")).key;
    ASSERT_TRUE(key.has_value());
    EXPECT_TRUE(fitNotesToKey(f.doc, fitToKeyTargets(*f.doc.getClip(f.clip), {}), *key));
    EXPECT_EQ(f.pitchOf(f.cSharp), 60) << "fitted to C major, not the song's C# major";
}
