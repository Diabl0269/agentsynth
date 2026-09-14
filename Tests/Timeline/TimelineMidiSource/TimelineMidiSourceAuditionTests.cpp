// TimelineMidiSource audition tests (pushAuditionNote) — the piano roll's note-click preview. The ONE thing pushed INTO
// this module: it works with the transport STOPPED, and it can never strand a note.

#include "TimelineMidiSourceTestHelpers.h"

// ================================================================================================
// AUDITION (pushAuditionNote) — the piano roll's note-click preview.
//
// The ONE thing pushed INTO this module. It emits from this node, which is what makes the preview
// reach exactly the destination modules the track's clips play through (destinations are graph
// connections whose SOURCE is this node's MIDI output). Everything below is about the two properties
// that matter: it works with the transport STOPPED, and it can never strand a note.
// ================================================================================================

TEST(TimelineMidiSourceAudition, PushedNoteOnAndOffEmitAtSampleZeroWithTheTransportStopped) {
    Harness h;
    Doc d;
    const auto snapshot = d.snapshot();
    // Deliberately NOT playing: a preview is a monitor path, not playback.
    ASSERT_FALSE(h.transport.getCurrentBlockInfo().playing);

    EXPECT_TRUE(h.module.pushAuditionNote(64, 96, /*noteOn=*/true));
    auto events = h.renderBlock(snapshot.get());
    ASSERT_EQ(events.size(), 1u);
    EXPECT_TRUE(events[0].isNoteOn);
    EXPECT_EQ(events[0].pitch, 64);
    EXPECT_EQ(events[0].velocity, 96);
    EXPECT_EQ(events[0].channel, 1);
    EXPECT_EQ(events[0].sample, 0) << "a gesture made between two callbacks lands at the top of the block";
    EXPECT_EQ(h.module.getAuditionNoteCount(), 1);
    EXPECT_EQ(h.module.getActiveNoteCount(), 1);

    // Nothing further is emitted while it is simply held.
    EXPECT_TRUE(h.renderBlock(snapshot.get()).empty());
    EXPECT_EQ(h.module.getAuditionNoteCount(), 1);

    EXPECT_TRUE(h.module.pushAuditionNote(64, 0, /*noteOn=*/false));
    events = h.renderBlock(snapshot.get());
    ASSERT_EQ(events.size(), 1u);
    EXPECT_TRUE(events[0].isNoteOff);
    EXPECT_EQ(events[0].pitch, 64);
    EXPECT_EQ(events[0].sample, 0);
    EXPECT_EQ(h.module.getAuditionNoteCount(), 0);
    EXPECT_EQ(h.module.getActiveNoteCount(), 0);
}

// A note-on for a pitch already being auditioned (a drag retrigger the caller sequenced without a
// release, or a lost note-off) releases the old one FIRST — off-then-on, and the held count cannot
// drift.
TEST(TimelineMidiSourceAudition, RepeatedNoteOnForTheSamePitchReleasesTheOldOneFirst) {
    Harness h;
    Doc d;
    const auto snapshot = d.snapshot();

    h.module.pushAuditionNote(60, 100, true);
    ASSERT_EQ(h.renderBlock(snapshot.get()).size(), 1u);

    h.module.pushAuditionNote(60, 100, true);
    const auto events = h.renderBlock(snapshot.get());
    ASSERT_EQ(events.size(), 2u);
    EXPECT_TRUE(events[0].isNoteOff) << "the old note is released before the new one starts";
    EXPECT_TRUE(events[1].isNoteOn);
    EXPECT_EQ(h.module.getAuditionNoteCount(), 1) << "still exactly one held, never two";
}

// A note-off for a pitch that is NOT being auditioned emits NOTHING. This guard is load-bearing: a
// stray note-off would cut a TIMELINE note of the same pitch short.
TEST(TimelineMidiSourceAudition, NoteOffForAPitchThatIsNotAuditionedEmitsNothingAndSparesTimelineNotes) {
    Harness h;
    Doc d;
    ASSERT_TRUE(d.doc.addNote(d.clipId, makeNote(1.0, 60, 4.0)).isValid());
    const auto snapshot = d.snapshot();

    // Never auditioned at all: the off is inert.
    h.module.pushAuditionNote(60, 0, false);
    EXPECT_TRUE(h.renderBlock(snapshot.get()).empty());

    // Now play the timeline note, then push an audition OFF for the same pitch.
    h.transport.play();
    bool sawTimelineNoteOn = false;
    for (const auto& [block, events] : h.renderBlocks(snapshot.get(), blockOfBeat(1.0) + 2))
        for (const auto& e : events)
            if (e.isNoteOn && e.pitch == 60)
                sawTimelineNoteOn = true;
    ASSERT_TRUE(sawTimelineNoteOn);
    ASSERT_EQ(h.module.getActiveNoteCount(), 1);
    ASSERT_EQ(h.module.getAuditionNoteCount(), 0) << "a timeline note is not an audition note";

    h.module.pushAuditionNote(60, 0, false);
    const auto events = h.renderBlock(snapshot.get());
    for (const auto& e : events)
        EXPECT_FALSE(e.isNoteOff && e.pitch == 60) << "the timeline note must not be cut short";
    EXPECT_EQ(h.module.getActiveNoteCount(), 1) << "the timeline note is still held";
}

// STOP does not end a preview: audition is not on the transport's clock, and cutting it because the
// user pressed Stop would break the monitor path it exists to be. The timeline's own notes ARE
// flushed at the same boundary, which is what makes this a real distinction rather than an omission.
TEST(TimelineMidiSourceAudition, StopFlushesTimelineNotesButLeavesAHeldPreviewSounding) {
    Harness h;
    Doc d;
    ASSERT_TRUE(d.doc.addNote(d.clipId, makeNote(1.0, 55, 8.0)).isValid());
    const auto snapshot = d.snapshot();

    h.transport.play();
    h.renderBlocks(snapshot.get(), blockOfBeat(1.0) + 2);
    ASSERT_EQ(h.module.getActiveNoteCount(), 1) << "the timeline note is sounding";

    h.module.pushAuditionNote(72, 100, true);
    h.renderBlock(snapshot.get());
    ASSERT_EQ(h.module.getAuditionNoteCount(), 1);
    ASSERT_EQ(h.module.getActiveNoteCount(), 2);

    h.transport.stop();
    const auto events = h.renderBlock(snapshot.get());
    bool flushed55 = false, flushed72 = false;
    for (const auto& e : events) {
        if (e.isNoteOff && e.pitch == 55)
            flushed55 = true;
        if (e.isNoteOff && e.pitch == 72)
            flushed72 = true;
    }
    EXPECT_TRUE(flushed55) << "the timeline note is released at the stop, as it always was";
    EXPECT_FALSE(flushed72) << "the preview the user is still holding survives the stop";
    EXPECT_EQ(h.module.getAuditionNoteCount(), 1);
    EXPECT_EQ(h.module.getActiveNoteCount(), 1);
}

// BYPASS is the one thing that DOES take a preview with it — a bypassed source emits nothing, so a
// queued note-off would never be delivered and the note would be stranded. The first bypassed block
// releases it, and anything queued while bypassed is discarded rather than replayed on resume.
TEST(TimelineMidiSourceAudition, BypassReleasesAHeldPreviewAndDiscardsWhatArrivesWhileBypassed) {
    Harness h;
    Doc d;
    const auto snapshot = d.snapshot();

    h.module.pushAuditionNote(60, 100, true);
    ASSERT_EQ(h.renderBlock(snapshot.get()).size(), 1u);
    ASSERT_EQ(h.module.getAuditionNoteCount(), 1);

    h.module.setBypassed(true);
    const auto events = h.renderBlock(snapshot.get());
    ASSERT_EQ(events.size(), 1u);
    EXPECT_TRUE(events[0].isNoteOff);
    EXPECT_EQ(events[0].pitch, 60);
    EXPECT_EQ(h.module.getAuditionNoteCount(), 0);

    // Pushed while bypassed: dropped, never queued for the un-bypass.
    h.module.pushAuditionNote(67, 100, true);
    EXPECT_TRUE(h.renderBlock(snapshot.get()).empty());
    h.module.setBypassed(false);
    EXPECT_TRUE(h.renderBlock(snapshot.get()).empty()) << "a stale click must not sound on resume";
    EXPECT_EQ(h.module.getAuditionNoteCount(), 0);
}

// A muted track still previews: mute is about the timeline's output, and the editor's preview is a
// monitor path (the same way clicking a key on a MIDI Keyboard module is).
TEST(TimelineMidiSourceAudition, AMutedTrackStillPreviews) {
    Harness h;
    Doc d;
    d.doc.setTrackMuted(d.trackId, true);
    ASSERT_TRUE(d.doc.addNote(d.clipId, makeNote(1.0, 60, 2.0)).isValid());
    const auto snapshot = d.snapshot();

    h.transport.play();
    h.module.pushAuditionNote(64, 90, true);
    const auto events = h.renderBlock(snapshot.get());
    ASSERT_EQ(events.size(), 1u);
    EXPECT_TRUE(events[0].isNoteOn);
    EXPECT_EQ(events[0].pitch, 64);
    EXPECT_EQ(h.module.getAuditionNoteCount(), 1);
}

// The FIFO is fixed-capacity and NON-blocking: past its depth a push reports false and drops the
// event rather than waiting on the audio thread. Dropping is survivable by construction — a dropped
// note-on is silence, and a dropped note-off cannot strand anything because bypass flushes.
TEST(TimelineMidiSourceAudition, AFullFifoDropsRatherThanBlocking) {
    Harness h;
    Doc d;
    const auto snapshot = d.snapshot();

    int accepted = 0;
    for (int i = 0; i < TimelineMidiSourceModule::kAuditionFifoCapacity * 2; ++i)
        if (h.module.pushAuditionNote(60 + (i % 12), 100, /*noteOn=*/true))
            ++accepted;
    EXPECT_LT(accepted, TimelineMidiSourceModule::kAuditionFifoCapacity * 2) << "the FIFO reported full at some point";
    EXPECT_GT(accepted, 0);

    // Whatever WAS accepted drains in one block and leaves the held table consistent (12 pitches,
    // each repeated on-without-off, so exactly 12 are held — see the retrigger rule above).
    h.renderBlock(snapshot.get());
    EXPECT_EQ(h.module.getAuditionNoteCount(), 12);
    EXPECT_LE(h.module.getActiveNoteCount(), TimelineMidiSourceModule::kMaxActiveNotes);
}

// THE stuck-note bug, in the one place a note-off can actually be LOST.
//
// A preview is deliberately spared by every positional flush (stop, locate, loop wrap), so nothing
// that happens later on the transport can clean one up. That makes a dropped note-OFF terminal: the
// note is held for the life of the process, and Stop — the one thing a user reaches for when a note
// hangs — provably cannot clear it. pushAuditionNote therefore raises a panic when it has to drop an
// off, and the next block releases every preview.
TEST(TimelineMidiSourceAudition, ADroppedNoteOffPanicsAndReleasesEveryPreview) {
    Harness h;
    Doc d;
    const auto snapshot = d.snapshot();

    // One preview sounding, cleanly.
    ASSERT_TRUE(h.module.pushAuditionNote(60, 100, /*noteOn=*/true));
    h.renderBlock(snapshot.get());
    ASSERT_EQ(h.module.getAuditionNoteCount(), 1);

    // Now fill the FIFO so the note-off cannot be queued, and confirm it really was refused. The
    // filler is note-OFFs for a pitch nothing is auditioning: they drain to nothing (see
    // NoteOffForAPitchThatIsNotAuditionedEmitsNothing...), so the only thing this block can change
    // is the stranded note itself.
    while (h.module.pushAuditionNote(72, 0, /*noteOn=*/false))
        ;
    EXPECT_FALSE(h.module.pushAuditionNote(60, 0, /*noteOn=*/false)) << "precondition: the off was dropped";

    // The very next block releases the stranded preview. Before the panic path existed this note
    // stayed held forever and no stop/locate/wrap could touch it.
    const auto events = h.renderBlock(snapshot.get());
    EXPECT_EQ(h.module.getAuditionNoteCount(), 0) << "a dropped note-off must not strand a preview";

    bool sawOffFor60 = false;
    for (const auto& e : events)
        if (e.isNoteOff && e.pitch == 60)
            sawOffFor60 = true;
    EXPECT_TRUE(sawOffFor60) << "and the release must be a real note-off, not just a forgotten entry";
}

// The panic is armed only by a dropped OFF. A dropped note-ON owes nothing — nothing sounded — and
// must not tear down previews that are sounding perfectly well.
TEST(TimelineMidiSourceAudition, ADroppedNoteOnDoesNotDisturbHeldPreviews) {
    Harness h;
    Doc d;
    const auto snapshot = d.snapshot();

    ASSERT_TRUE(h.module.pushAuditionNote(60, 100, /*noteOn=*/true));
    h.renderBlock(snapshot.get());
    ASSERT_EQ(h.module.getAuditionNoteCount(), 1);

    while (h.module.pushAuditionNote(72, 100, /*noteOn=*/true))
        ;                          // fill to capacity
    h.renderBlock(snapshot.get()); // drains whatever fitted
    h.renderBlock(snapshot.get()); // and a second block, so a stray panic would have fired
    EXPECT_GE(h.module.getAuditionNoteCount(), 1) << "a dropped note-ON must not release anything";
}

// The deliberate half of the contract, pinned so the panic path above cannot be "simplified" into
// releasing previews on stop: a HEALTHY preview survives a stop, a locate and a loop wrap.
TEST(TimelineMidiSourceAudition, AHealthyPreviewSurvivesStopAndLocate) {
    Doc doc;
    ASSERT_TRUE(doc.doc.addNote(doc.clipId, makeNote(1.0, 60, 8.0)).isValid());
    auto snapshot = doc.snapshot();

    Harness h;
    ASSERT_TRUE(h.transport.play());
    h.renderBlocks(snapshot.get(), blockOfBeat(1.0) + 1);
    ASSERT_EQ(h.module.getActiveNoteCount(), 1) << "the timeline note is sounding";

    ASSERT_TRUE(h.module.pushAuditionNote(72, 100, /*noteOn=*/true));
    h.renderBlock(snapshot.get());
    ASSERT_EQ(h.module.getAuditionNoteCount(), 1);
    ASSERT_EQ(h.module.getActiveNoteCount(), 2);

    // STOP: the timeline note goes, the preview stays.
    ASSERT_TRUE(h.transport.stop());
    h.renderBlock(snapshot.get());
    EXPECT_EQ(h.module.getActiveNoteCount(), 1) << "the timeline note must be released";
    EXPECT_EQ(h.module.getAuditionNoteCount(), 1) << "the preview must NOT be (a monitor path)";

    // LOCATE: same rule.
    ASSERT_TRUE(h.transport.locateBeat(32.0));
    h.renderBlock(snapshot.get());
    EXPECT_EQ(h.module.getAuditionNoteCount(), 1);

    // Its own note-off is what ends it.
    ASSERT_TRUE(h.module.pushAuditionNote(72, 0, /*noteOn=*/false));
    h.renderBlock(snapshot.get());
    EXPECT_EQ(h.module.getAuditionNoteCount(), 0);
    EXPECT_EQ(h.module.getActiveNoteCount(), 0);
}
