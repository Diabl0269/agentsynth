// Concern: songPitchClassWeights, what Detect from notes reads from the song.
#include "Timeline/SongKeyNotes.h"
#include <gtest/gtest.h>

using namespace synth;

TEST(SongKeyNotesTest, SumsNoteLengthsPerPitchClassAndSkipsMutedAndSkippedTracks) {
    TimelineDoc doc;
    const auto keys = doc.addTrack(TrackKind::Midi, "Keys");
    const auto drums = doc.addTrack(TrackKind::Midi, "Drums");
    const auto clip = doc.addClip(keys, 0.0, 8.0, "a");
    const auto drumClip = doc.addClip(drums, 0.0, 8.0, "b");
    ASSERT_TRUE(clip.isValid());
    ASSERT_TRUE(drumClip.isValid());
    doc.addNote(clip, {{}, 0.0, 2.0, 60, 100, 1, false}); // C, 2 beats
    doc.addNote(clip, {{}, 2.0, 1.0, 72, 100, 1, false}); // C, 1 beat
    doc.addNote(clip, {{}, 3.0, 1.0, 64, 100, 1, true});  // muted E
    doc.addNote(drumClip, {{}, 0.0, 4.0, 38, 100, 1, false});

    const auto all = songPitchClassWeights(doc);
    EXPECT_DOUBLE_EQ(all[0], 3.0);
    EXPECT_DOUBLE_EQ(all[4], 0.0);
    EXPECT_DOUBLE_EQ(all[2], 4.0);

    const auto noDrums = songPitchClassWeights(doc, [drums](const Track& t) { return t.id == drums; });
    EXPECT_DOUBLE_EQ(noDrums[2], 0.0);
    EXPECT_DOUBLE_EQ(noDrums[0], 3.0);
}
