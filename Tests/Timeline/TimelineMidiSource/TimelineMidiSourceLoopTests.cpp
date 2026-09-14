// TimelineMidiSource loop-wrap tests. Loop [0, 2) is sample 48000, which falls in block 93 (93 * 512 = 47616) at offset
// 384. Loop [0, 1) is sample 24000: block 46 at offset 448.

#include "TimelineMidiSourceTestHelpers.h"

// ============================================================================
// Loop wrap
//
// Loop [0, 2) is sample 48000, which falls in block 93 (93 * 512 = 47616) at offset 384. Loop
// [0, 1) is sample 24000: block 46 at offset 448.
// ============================================================================

// The wrapping block is TWO ranges. Everything below happens inside block 93:
//   offset 144 — note A ends (beat 1.99), inside the primary range
//   offset 384 — the wrap: note C (beat 1.5 -> 3.0) crosses the loop end and is released there
//   offset 384 — note B (beat 0.0) starts, from the wrapped range, AFTER C's release
TEST(TimelineMidiSourceTest, BlockStraddlingLoopBoundarySplitsIntoTwoRanges) {
    Doc doc;
    ASSERT_TRUE(doc.doc.addNote(doc.clipId, makeNote(0.0, 62, 0.5)).isValid());  // B: starts at the loop start
    ASSERT_TRUE(doc.doc.addNote(doc.clipId, makeNote(1.0, 60, 0.99)).isValid()); // A: ends at 1.99, pre-wrap
    ASSERT_TRUE(doc.doc.addNote(doc.clipId, makeNote(1.5, 64, 1.5)).isValid());  // C: ends at 3.0, past the loop
    auto snapshot = doc.snapshot();

    Harness h;
    ASSERT_TRUE(h.transport.setLoop(0.0, 2.0, true));
    ASSERT_TRUE(h.transport.play());

    h.renderBlocks(snapshot.get(), 93); // blocks 0..92: the first pass, up to the boundary
    const auto wrapBlock = h.renderBlock(snapshot.get());
    ASSERT_EQ(h.loopWrapSample(), 384) << "beat 2.0 is sample 48000, i.e. offset 384 of block 93";

    ASSERT_EQ(wrapBlock.size(), 3u) << "one block must carry the pre-wrap release, the wrap release and the "
                                       "post-wrap note-on";

    EXPECT_TRUE(wrapBlock[0].isNoteOff);
    EXPECT_EQ(wrapBlock[0].pitch, 60);
    EXPECT_EQ(wrapBlock[0].sample, 144) << "beat 1.99 is sample 47760, offset 144 of block 93";

    EXPECT_TRUE(wrapBlock[1].isNoteOff) << "the release must come before the re-articulation at the same offset";
    EXPECT_EQ(wrapBlock[1].pitch, 64);
    EXPECT_EQ(wrapBlock[1].sample, 384) << "a note crossing the loop end is released AT the wrap";

    EXPECT_TRUE(wrapBlock[2].isNoteOn);
    EXPECT_EQ(wrapBlock[2].pitch, 62);
    EXPECT_EQ(wrapBlock[2].sample, 384) << "and the wrapped range starts the next pass from the same sample";

    EXPECT_EQ(h.module.getActiveNoteCount(), 1) << "only B survives the block";

    // The block after a wrap must NOT read as a locate: B is still inside its [0, 0.5) span and
    // nothing may touch it.
    const auto afterWrap = h.renderBlock(snapshot.get());
    EXPECT_TRUE(afterWrap.empty()) << "the wrap is not a discontinuity — the note the wrapped range started stays down";
    EXPECT_EQ(h.module.getActiveNoteCount(), 1);
}

// A note whose end beat is outside the loop can never reach its own note-off, so the wrap emits it.
TEST(TimelineMidiSourceTest, NoteSpanningLoopEndIsReleasedAtWrap) {
    Doc doc;
    ASSERT_TRUE(doc.doc.addNote(doc.clipId, makeNote(1.5, 60, 1.5)).isValid()); // [1.5, 3.0) in a [0, 2) loop
    auto snapshot = doc.snapshot();

    Harness h;
    ASSERT_TRUE(h.transport.setLoop(0.0, 2.0, true));
    ASSERT_TRUE(h.transport.play());

    // Two complete passes: 94 blocks reaches the first wrap, and the second pass runs one block
    // shorter because it starts 128 samples in.
    const auto events = renderCollect(h, snapshot.get(), 94 * 2);
    ASSERT_EQ(events.size(), 4u) << "one on and one off per pass — the note re-arms every iteration";

    for (int pass = 0; pass < 2; ++pass) {
        const BlockEvent& on = events[(std::size_t)(pass * 2)];
        const BlockEvent& off = events[(std::size_t)(pass * 2 + 1)];

        EXPECT_TRUE(on.event.isNoteOn) << "pass " << pass;
        EXPECT_EQ(on.absSample(), 36000) << "pass " << pass << ": beat 1.5 is sample 36000 of every pass";

        EXPECT_TRUE(off.event.isNoteOff) << "pass " << pass;
        EXPECT_EQ(off.event.sample, off.wrapSample)
            << "pass " << pass << ": the release lands exactly on the block's loopWrapSample";
        EXPECT_EQ(off.absSample(), 48000) << "pass " << pass << ": which is the loop end, sample 48000";
    }
}

// Three complete passes of a one-beat loop: the note fires three times, at the same loop-relative
// samples every time, with nothing left over.
TEST(TimelineMidiSourceTest, LoopIterationRearticulates) {
    Doc doc;
    ASSERT_TRUE(doc.doc.addNote(doc.clipId, makeNote(0.25, 67, 0.25)).isValid()); // [0.25, 0.5) => 6000..12000
    auto snapshot = doc.snapshot();

    Harness h;
    ASSERT_TRUE(h.transport.setLoop(0.0, 1.0, true));
    ASSERT_TRUE(h.transport.play());

    // Each pass is 47 blocks (24000 samples of loop, 512-sample blocks), so 141 blocks is exactly
    // three passes ending on the third wrap.
    const auto events = renderCollect(h, snapshot.get(), 141);
    ASSERT_EQ(events.size(), 6u) << "exactly three on/off pairs — one per loop pass";

    int wraps = 0;
    for (const auto& e : events)
        if (e.wrapSample >= 0)
            ++wraps;
    EXPECT_EQ(wraps, 0) << "this note is nowhere near the boundary; no edge may land in a wrapping block";

    for (int pass = 0; pass < 3; ++pass) {
        EXPECT_TRUE(events[(std::size_t)(pass * 2)].event.isNoteOn) << "pass " << pass;
        EXPECT_EQ(events[(std::size_t)(pass * 2)].absSample(), 6000) << "pass " << pass << ": beat 0.25";
        EXPECT_TRUE(events[(std::size_t)(pass * 2 + 1)].event.isNoteOff) << "pass " << pass;
        EXPECT_EQ(events[(std::size_t)(pass * 2 + 1)].absSample(), 12000) << "pass " << pass << ": beat 0.5";
    }
    EXPECT_EQ(h.module.getActiveNoteCount(), 0);
}

// The negative that keeps a shared MIDI cable safe: whatever happens, this source emits note-ons and
// note-offs and NOTHING else — never CC 123 / all-notes-off, which would silence notes another
// source on the same cable is holding. And every off it does emit belongs to a pitch it started.
TEST(TimelineMidiSourceTest, WrapReleasesOnlyThisSourcesNotes) {
    Doc doc;
    ASSERT_TRUE(doc.doc.addNote(doc.clipId, makeNote(0.0, 60, 0.75, 100, 1)).isValid());
    ASSERT_TRUE(doc.doc.addNote(doc.clipId, makeNote(0.5, 67, 2.5, 100, 2)).isValid()); // crosses the loop end
    ASSERT_TRUE(doc.doc.addNote(doc.clipId, makeNote(1.5, 72, 3.0, 100, 3)).isValid()); // crosses it too
    auto snapshot = doc.snapshot();

    Harness h;
    ASSERT_TRUE(h.transport.setLoop(0.0, 2.0, true));
    ASSERT_TRUE(h.transport.play());

    HeldKeys model;
    for (const auto& e : renderCollect(h, snapshot.get(), 94 * 3))
        model.apply(e.event, "block " + juce::String(e.blockIndex));

    ASSERT_TRUE(h.transport.stop());
    for (const auto& e : h.renderBlock(snapshot.get()))
        model.apply(e, "the stopped block");

    EXPECT_TRUE(model.firstFailure.isEmpty()) << model.firstFailure;
    EXPECT_EQ(model.numDown(), 0) << "the stop must have released everything";
    EXPECT_EQ(model.ons, model.offs);
    EXPECT_GT(model.ons, 6) << "three loop passes of three notes — the run has to have played something";
    EXPECT_EQ(h.module.getActiveNoteCount(), 0);
}

// A loop SHORTER than the block. BlockTimeInfo reports only the first wrap, so the repetitions in
// between are not emitted (documented bound) — but hygiene is non-negotiable: no note may be left
// hanging, whatever gets skipped.
TEST(TimelineMidiSourceTest, TinyLoopMultiWrapNeverSticksNotes) {
    constexpr int kBigBlock = 4800;
    const double loopLengthBeats = synth::TransportService::kMinLoopLengthBeats; // 1/16 beat
    const int loopLengthSamples = (int)(loopLengthBeats * kSamplesPerBeat);      // 1500 at 48k/120bpm
    ASSERT_EQ(loopLengthSamples, 1500);
    ASSERT_GT(kBigBlock, loopLengthSamples) << "the point of this test is a block longer than the whole loop";

    Doc doc;
    ASSERT_TRUE(doc.doc.addNote(doc.clipId, makeNote(0.0, 60, 0.01)).isValid());  // fits inside the loop
    ASSERT_TRUE(doc.doc.addNote(doc.clipId, makeNote(0.02, 62, 0.05)).isValid()); // ends past the loop end
    ASSERT_TRUE(doc.doc.addNote(doc.clipId, makeNote(4.0, 64, 1.0)).isValid());   // never reached
    auto snapshot = doc.snapshot();

    Harness h;
    ASSERT_TRUE(h.transport.setLoop(0.0, loopLengthBeats, true));
    ASSERT_TRUE(h.transport.play());

    HeldKeys model;
    int wrappingBlocks = 0;
    for (const auto& e : renderCollect(h, snapshot.get(), 24, kBigBlock))
        model.apply(e.event, "block " + juce::String(e.blockIndex));

    // Every block of a loop this small has to wrap — render a few more and confirm.
    for (int block = 0; block < 4; ++block) {
        for (const auto& e : h.renderBlock(snapshot.get(), kBigBlock))
            model.apply(e, "extra block " + juce::String(block));
        if (h.loopWrapSample() >= 0)
            ++wrappingBlocks;
    }
    EXPECT_EQ(wrappingBlocks, 4) << "a 1500-sample loop under a 4800-sample block wraps every single block";

    ASSERT_TRUE(h.transport.stop());
    for (const auto& e : h.renderBlock(snapshot.get(), kBigBlock))
        model.apply(e, "the stopped block");

    EXPECT_TRUE(model.firstFailure.isEmpty()) << model.firstFailure;
    EXPECT_EQ(model.numDown(), 0) << "nothing may be left sounding once the transport stops";
    EXPECT_EQ(model.ons, model.offs) << "every note-on owes exactly one note-off";
    EXPECT_GT(model.ons, 20) << "the loop repeated many times — it must actually have played";
    EXPECT_EQ(h.module.getActiveNoteCount(), 0);
}

// Adjacent notes of the SAME pitch: the release of the first has to be inserted before the
// re-articulation of the second at the shared offset, or Poly MIDI's same-pitch retrigger contract
// sees an on/off pair in the wrong order and drops the gate edge.
TEST(TimelineMidiSourceTest, OffAndOnAtSameOffsetOrdering) {
    Doc doc;
    ASSERT_TRUE(doc.doc.addNote(doc.clipId, makeNote(0.0, 60, 1.0)).isValid());
    ASSERT_TRUE(doc.doc.addNote(doc.clipId, makeNote(1.0, 60, 1.0)).isValid());
    auto snapshot = doc.snapshot();

    Harness h;
    ASSERT_TRUE(h.transport.play());

    const auto blocks = h.renderBlocks(snapshot.get(), blockOfBeat(1.0) + 1);
    ASSERT_EQ(blocks.size(), 2u);
    EXPECT_EQ(blocks[0].first, 0); // the first note's on, at sample 0

    const auto& boundary = blocks[1].second;
    EXPECT_EQ(blocks[1].first, 46);
    ASSERT_EQ(boundary.size(), 2u);
    EXPECT_EQ(boundary[0].sample, 448);
    EXPECT_EQ(boundary[1].sample, 448) << "both edges land on the same sample";
    EXPECT_TRUE(boundary[0].isNoteOff) << "and the buffer must hand the release out first";
    EXPECT_TRUE(boundary[1].isNoteOn);
    EXPECT_EQ(boundary[0].pitch, 60);
    EXPECT_EQ(boundary[1].pitch, 60);
}

// The catch-all: 1000 random transport operations against a polyphonic, multi-channel track. Any
// combination of play/stop/locate/loop/tempo must keep the one promise this module makes — a
// note-on is always followed by exactly one note-off, and nothing sounds twice without a release in
// between. Seeded, so a failure is reproducible.
TEST(TimelineMidiSourceTest, TransportFuzz1000OpsNoStuckNotes) {
    // An 8-beat pattern of overlapping notes on three channels, repeated across the whole range a
    // random locate can land in, so the fuzz spends its time playing rather than sitting in silence.
    Doc doc(kMyUuid, 72.0);
    for (int bar = 0; bar < 8; ++bar) {
        const double at = (double)bar * 8.0;
        // A rising line on channel 1 — the notes overlap each other, never themselves.
        for (int i = 0; i < 8; ++i)
            ASSERT_TRUE(doc.doc.addNote(doc.clipId, makeNote(at + (double)i, 60 + i, 1.5, 100, 1)).isValid());
        // A slower pad on channel 2, four distinct pitches back to back.
        for (int i = 0; i < 4; ++i)
            ASSERT_TRUE(doc.doc.addNote(doc.clipId, makeNote(at + (double)i * 2.0, 48 + i * 3, 2.0, 90, 2)).isValid());
        // The same pitch retriggered on channel 3, with gaps — the case a sloppy off would corrupt.
        for (int i = 0; i < 4; ++i)
            ASSERT_TRUE(doc.doc.addNote(doc.clipId, makeNote(at + (double)i * 2.0, 36, 1.0, 80, 3)).isValid());
    }
    auto snapshot = doc.snapshot();

    Harness h;
    HeldKeys model;
    juce::Random rng(0x71E1F022);
    int wrappingBlocks = 0;

    // Mirrors the loop the transport was last told to use, so one of the operations can drop the
    // playhead INSIDE it. Without that the playhead almost never reaches a loop end at this block
    // granularity and the whole wrap path — the thing this fuzz exists to protect — goes untested.
    double loopStart = 0.0;
    double loopLength = 4.0;

    for (int iteration = 0; iteration < 1000 && model.firstFailure.isEmpty(); ++iteration) {
        switch (rng.nextInt(7)) {
        case 0:
            h.transport.play();
            break;
        case 1:
            h.transport.stop();
            break;
        case 2:
            h.transport.locateBeat(rng.nextDouble() * 64.0); // a wide jump, usually out of the loop
            break;
        case 3:
            h.transport.locateBeat(loopStart + rng.nextDouble() * loopLength); // and one into it
            break;
        case 4: {
            loopStart = rng.nextDouble() * 8.0;
            // Powers of two from 1/8 to 8 beats. A LINEAR draw over that span makes short loops
            // vanishingly rare, and short loops are where the interesting wraps live (1/8 beat is
            // 3000 samples — six blocks). Exact binary values, so the run is bit-identical anywhere.
            loopLength = 0.125 * (double)(1 << rng.nextInt(7));
            h.transport.setLoop(loopStart, loopStart + loopLength, rng.nextBool());
            break;
        }
        case 5:
            h.transport.setBpm(40.0 + rng.nextDouble() * 200.0);
            break;
        default:
            break; // no-op: a plain run of blocks with nothing changing
        }

        const int blocks = 1 + rng.nextInt(4);
        for (int block = 0; block < blocks; ++block) {
            for (const auto& e : h.renderBlock(snapshot.get()))
                model.apply(e, "iteration " + juce::String(iteration));
            if (h.loopWrapSample() >= 0)
                ++wrappingBlocks;
        }
    }

    ASSERT_TRUE(model.firstFailure.isEmpty()) << model.firstFailure;

    ASSERT_TRUE(h.transport.stop());
    for (const auto& e : h.renderBlock(snapshot.get()))
        model.apply(e, "the final stopped block");

    EXPECT_TRUE(model.firstFailure.isEmpty()) << model.firstFailure;
    EXPECT_EQ(model.numDown(), 0) << "the fuzz left notes sounding after a stop";
    EXPECT_EQ(model.ons, model.offs) << "every note-on owes exactly one note-off";
    EXPECT_EQ(h.module.getActiveNoteCount(), 0);
    EXPECT_GT(model.ons, 20) << "the fuzz has to have actually played something to be worth anything";
    EXPECT_GT(wrappingBlocks, 5) << "and it has to have crossed the loop boundary, which is the point";
}
