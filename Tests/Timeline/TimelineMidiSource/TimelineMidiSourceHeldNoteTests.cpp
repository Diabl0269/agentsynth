// TimelineMidiSource held-note hygiene tests: stop/discontinuity/bypass/track-disappear all flush active notes cleanly.

#include "TimelineMidiSourceTestHelpers.h"

// ============================================================================
// Held-note hygiene
// ============================================================================

TEST(TimelineMidiSourceTest, StopFlushesActiveNotes) {
    Doc doc;
    ASSERT_TRUE(doc.doc.addNote(doc.clipId, makeNote(1.0, 60, 8.0)).isValid()); // still held at stop
    auto snapshot = doc.snapshot();

    Harness h;
    ASSERT_TRUE(h.transport.play());
    h.renderBlocks(snapshot.get(), blockOfBeat(1.0) + 1);
    ASSERT_EQ(h.module.getActiveNoteCount(), 1);

    ASSERT_TRUE(h.transport.stop());
    const auto stopped = h.renderBlock(snapshot.get());
    ASSERT_EQ(stopped.size(), 1u) << "the stopped block must release the held note";
    EXPECT_FALSE(stopped[0].isNoteOn);
    EXPECT_EQ(stopped[0].sample, 0) << "the flush lands at sample 0 of the block the stop took effect in";
    EXPECT_EQ(stopped[0].pitch, 60);
    EXPECT_EQ(h.module.getActiveNoteCount(), 0);

    for (const auto& [block, events] : h.renderBlocks(snapshot.get(), 8))
        ADD_FAILURE() << "block " << block << " emitted " << events.size() << " events while stopped";
}

TEST(TimelineMidiSourceTest, LocateDiscontinuityFlushes) {
    Doc doc;
    ASSERT_TRUE(doc.doc.addNote(doc.clipId, makeNote(1.0, 60, 8.0)).isValid());
    ASSERT_TRUE(doc.doc.addNote(doc.clipId, makeNote(4.0, 72, 2.0)).isValid());
    auto snapshot = doc.snapshot();

    Harness h;
    ASSERT_TRUE(h.transport.play());
    h.renderBlocks(snapshot.get(), blockOfBeat(1.0) + 1);
    ASSERT_EQ(h.module.getActiveNoteCount(), 1);

    // Jump to exactly beat 4.0, where the second note starts: the same block must both release
    // what the old position was holding and start what the new one demands.
    ASSERT_TRUE(h.transport.locateBeat(4.0));
    const auto located = h.renderBlock(snapshot.get());
    ASSERT_EQ(located.size(), 2u);

    EXPECT_FALSE(located[0].isNoteOn) << "the release must come first";
    EXPECT_EQ(located[0].pitch, 60);
    EXPECT_EQ(located[0].sample, 0);

    EXPECT_TRUE(located[1].isNoteOn);
    EXPECT_EQ(located[1].pitch, 72);
    EXPECT_EQ(located[1].sample, 0);
    EXPECT_EQ(h.module.getActiveNoteCount(), 1);
}

TEST(TimelineMidiSourceTest, BypassTransitionEmitsNoteOffsOnceThenSilence) {
    Doc doc;
    ASSERT_TRUE(doc.doc.addNote(doc.clipId, makeNote(1.0, 60, 8.0)).isValid());
    auto snapshot = doc.snapshot();

    Harness h;
    ASSERT_TRUE(h.transport.play());
    h.renderBlocks(snapshot.get(), blockOfBeat(1.0) + 1);
    ASSERT_EQ(h.module.getActiveNoteCount(), 1);

    h.module.setBypassed(true);
    const auto first = h.renderBlock(snapshot.get());
    ASSERT_EQ(first.size(), 1u) << "the first bypassed block must release what was sounding";
    EXPECT_FALSE(first[0].isNoteOn);
    EXPECT_EQ(first[0].sample, 0);
    EXPECT_EQ(h.module.getActiveNoteCount(), 0);

    for (const auto& [block, events] : h.renderBlocks(snapshot.get(), 8))
        ADD_FAILURE() << "bypassed block " << block << " emitted " << events.size() << " events";

    // Un-bypassing resumes normally: the next note start is picked up from the live position.
    h.module.setBypassed(false);
    const auto resumed = h.renderBlocks(snapshot.get(), blockOfBeat(9.0));
    EXPECT_TRUE(resumed.empty()) << "the held note's start beat is long past — nothing may retrigger";
}

TEST(TimelineMidiSourceTest, TrackDisappearingFlushesActiveNotes) {
    Doc doc;
    ASSERT_TRUE(doc.doc.addNote(doc.clipId, makeNote(1.0, 60, 8.0)).isValid());
    auto snapshot = doc.snapshot();

    Harness h;
    ASSERT_TRUE(h.transport.play());
    h.renderBlocks(snapshot.get(), blockOfBeat(1.0) + 1);
    ASSERT_EQ(h.module.getActiveNoteCount(), 1);

    // The user unbinds (or deletes) the track while a note is down.
    ASSERT_TRUE(doc.doc.setTrackBinding(doc.trackId, {}));
    auto unbound = doc.snapshot();

    const auto after = h.renderBlock(unbound.get());
    ASSERT_EQ(after.size(), 1u);
    EXPECT_FALSE(after[0].isNoteOn);
    EXPECT_EQ(after[0].sample, 0);
    EXPECT_EQ(h.module.getActiveNoteCount(), 0);
}
