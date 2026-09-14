// TimelineMidiSource emission tests: the note grid maps onto exact sample offsets.

#include "TimelineMidiSourceTestHelpers.h"

// ============================================================================
// Emission: the note grid maps onto exact sample offsets
// ============================================================================

TEST(TimelineMidiSourceTest, NoteOnAtExactSampleOffset) {
    Doc doc;
    ASSERT_TRUE(doc.doc.addNote(doc.clipId, makeNote(1.0, 64, 1.0, 111, 3)).isValid());
    auto snapshot = doc.snapshot();

    Harness h;
    ASSERT_TRUE(h.transport.play());

    const auto blocks = h.renderBlocks(snapshot.get(), blockOfBeat(1.0) + 1);
    ASSERT_EQ(blocks.size(), 1u) << "exactly one block in [0, beat 1] may carry an event";
    EXPECT_EQ(blocks[0].first, 46);

    ASSERT_EQ(blocks[0].second.size(), 1u);
    const Event& on = blocks[0].second[0];
    EXPECT_TRUE(on.isNoteOn);
    EXPECT_EQ(on.sample, 448) << "beat 1.0 is sample 24000, i.e. offset 448 of block 46";
    EXPECT_EQ(on.sample, offsetOfBeat(1.0));
    EXPECT_EQ(on.pitch, 64);
    EXPECT_EQ(on.velocity, 111);
    EXPECT_EQ(on.channel, 3);
}

TEST(TimelineMidiSourceTest, NoteOffAtEndBeat) {
    Doc doc;
    // Beat 1.0 -> 2.0: on in block 46, off in block 93, nothing in between.
    ASSERT_TRUE(doc.doc.addNote(doc.clipId, makeNote(1.0, 60, 1.0)).isValid());
    auto snapshot = doc.snapshot();

    Harness h;
    ASSERT_TRUE(h.transport.play());

    const auto blocks = h.renderBlocks(snapshot.get(), blockOfBeat(2.0) + 1);
    ASSERT_EQ(blocks.size(), 2u) << "a note spanning many blocks must emit in exactly two of them";

    EXPECT_EQ(blocks[0].first, 46);
    ASSERT_EQ(blocks[0].second.size(), 1u);
    EXPECT_TRUE(blocks[0].second[0].isNoteOn);

    EXPECT_EQ(blocks[1].first, 93);
    ASSERT_EQ(blocks[1].second.size(), 1u);
    EXPECT_FALSE(blocks[1].second[0].isNoteOn);
    EXPECT_EQ(blocks[1].second[0].sample, offsetOfBeat(2.0));
    EXPECT_EQ(blocks[1].second[0].pitch, 60);
    EXPECT_EQ(h.module.getActiveNoteCount(), 0);
}

// A note that starts AND ends inside one block still emits both edges, in order.
TEST(TimelineMidiSourceTest, ShortNoteEmitsBothEdgesInOneBlock) {
    Doc doc;
    // 50 samples: beat 1.0 is offset 448 of block 46, so the end still lands in the same block.
    const double length = 50.0 / kSamplesPerBeat;
    ASSERT_TRUE(doc.doc.addNote(doc.clipId, makeNote(1.0, 60, length)).isValid());
    auto snapshot = doc.snapshot();

    Harness h;
    ASSERT_TRUE(h.transport.play());

    const auto blocks = h.renderBlocks(snapshot.get(), blockOfBeat(1.0) + 3);
    ASSERT_EQ(blocks.size(), 1u);
    ASSERT_EQ(blocks[0].second.size(), 2u);
    EXPECT_TRUE(blocks[0].second[0].isNoteOn);
    EXPECT_EQ(blocks[0].second[0].sample, 448);
    EXPECT_FALSE(blocks[0].second[1].isNoteOn);
    EXPECT_EQ(blocks[0].second[1].sample, 498);
    EXPECT_EQ(h.module.getActiveNoteCount(), 0);
}

TEST(TimelineMidiSourceTest, ChordSameOffset) {
    Doc doc;
    for (int pitch : {60, 64, 67})
        ASSERT_TRUE(doc.doc.addNote(doc.clipId, makeNote(1.0, pitch, 1.0)).isValid());
    auto snapshot = doc.snapshot();

    Harness h;
    ASSERT_TRUE(h.transport.play());

    const auto blocks = h.renderBlocks(snapshot.get(), blockOfBeat(1.0) + 1);
    ASSERT_EQ(blocks.size(), 1u);
    ASSERT_EQ(blocks[0].second.size(), 3u);

    std::vector<int> pitches;
    for (const auto& e : blocks[0].second) {
        EXPECT_TRUE(e.isNoteOn);
        EXPECT_EQ(e.sample, 448) << "every note of a chord shares one sample offset";
        pitches.push_back(e.pitch);
    }
    std::sort(pitches.begin(), pitches.end());
    EXPECT_EQ(pitches, (std::vector<int>{60, 64, 67}));
    EXPECT_EQ(h.module.getActiveNoteCount(), 3);
}
