// TimelineMidiSource mute/solo tests: a muted or solo-suppressed track emits nothing, and suppression turning on
// flushes active notes.

#include "TimelineMidiSourceTestHelpers.h"

// ============================================================================
// Mute / solo
// ============================================================================

TEST(TimelineMidiSourceTest, MutedTrackEmitsNothing) {
    Doc doc;
    ASSERT_TRUE(doc.doc.addNote(doc.clipId, makeNote(1.0, 60, 1.0)).isValid());
    ASSERT_TRUE(doc.doc.setTrackMuted(doc.trackId, true));
    auto snapshot = doc.snapshot();

    Harness h;
    ASSERT_TRUE(h.transport.play());
    for (const auto& [block, events] : h.renderBlocks(snapshot.get(), blockOfBeat(2.0) + 1))
        ADD_FAILURE() << "muted track emitted " << events.size() << " events in block " << block;
}

TEST(TimelineMidiSourceTest, SoloOnOtherTrackSuppresses) {
    Doc doc;
    ASSERT_TRUE(doc.doc.addNote(doc.clipId, makeNote(1.0, 60, 1.0)).isValid());
    const auto otherTrack = doc.doc.addTrack(TrackKind::Midi, "Other");
    ASSERT_TRUE(otherTrack.isValid());
    ASSERT_TRUE(doc.doc.setTrackSoloed(otherTrack, true));
    auto snapshot = doc.snapshot();
    ASSERT_TRUE(snapshot->anySoloed);

    Harness h;
    ASSERT_TRUE(h.transport.play());
    for (const auto& [block, events] : h.renderBlocks(snapshot.get(), blockOfBeat(2.0) + 1))
        ADD_FAILURE() << "a solo elsewhere must silence this track — block " << block << " emitted " << events.size();
}

TEST(TimelineMidiSourceTest, SoloOnMyTrackPlays) {
    Doc doc;
    ASSERT_TRUE(doc.doc.addNote(doc.clipId, makeNote(1.0, 60, 1.0)).isValid());
    const auto otherTrack = doc.doc.addTrack(TrackKind::Midi, "Other");
    ASSERT_TRUE(otherTrack.isValid());
    ASSERT_TRUE(doc.doc.setTrackSoloed(doc.trackId, true));
    auto snapshot = doc.snapshot();
    ASSERT_TRUE(snapshot->anySoloed);

    Harness h;
    ASSERT_TRUE(h.transport.play());
    const auto blocks = h.renderBlocks(snapshot.get(), blockOfBeat(1.0) + 1);
    ASSERT_EQ(blocks.size(), 1u);
    EXPECT_EQ(blocks[0].first, 46);
    EXPECT_TRUE(blocks[0].second[0].isNoteOn);
}

// Solo (or mute) turning on while a note is down is a suppression edge, and must release it.
TEST(TimelineMidiSourceTest, SuppressionTurningOnFlushesActiveNotes) {
    Doc doc;
    ASSERT_TRUE(doc.doc.addNote(doc.clipId, makeNote(1.0, 60, 8.0)).isValid());
    auto playing = doc.snapshot();

    Harness h;
    ASSERT_TRUE(h.transport.play());
    h.renderBlocks(playing.get(), blockOfBeat(1.0) + 1);
    ASSERT_EQ(h.module.getActiveNoteCount(), 1);

    ASSERT_TRUE(doc.doc.setTrackMuted(doc.trackId, true));
    auto muted = doc.snapshot();

    const auto after = h.renderBlock(muted.get());
    ASSERT_EQ(after.size(), 1u);
    EXPECT_FALSE(after[0].isNoteOn);
    EXPECT_EQ(after[0].sample, 0);
    EXPECT_EQ(h.module.getActiveNoteCount(), 0);

    for (const auto& [block, events] : h.renderBlocks(muted.get(), 4))
        ADD_FAILURE() << "muted block " << block << " emitted " << events.size() << " events";
}
