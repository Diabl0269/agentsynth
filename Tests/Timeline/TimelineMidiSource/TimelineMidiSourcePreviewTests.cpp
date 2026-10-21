// TimelineMidiSource clip-preview tests (startClipPreview): the drum machine's "Play clip" loops one clip's notes with
// the transport STOPPED, on a clock of its own at the transport's tempo. Everything below drives the module block by
// block, as the audio callback does, and checks the two promises that matter: the loop sounds right, and no note can
// be left hanging however the preview ends.

#include "TimelineMidiSourceTestHelpers.h"
#include <atomic>
#include <thread>

namespace {

// A document whose single clip is `clipLength` beats long, with the given notes added to it.
struct PreviewDoc : Doc {
    PreviewDoc(double clipLength, std::initializer_list<MidiNote> notes)
        : Doc(kMyUuid, clipLength) {
        for (const auto& note : notes)
            EXPECT_TRUE(doc.addNote(clipId, note).isValid());
    }
};

using TimedEvents = std::vector<std::pair<std::int64_t, Event>>;

// All events of `blocks` blocks, each tagged with the sample it landed at counted from block `firstBlock`.
TimedEvents render(Harness& h, const TimelineSnapshot* snapshot, int blocks, int firstBlock = 0) {
    TimedEvents out;
    for (int i = 0; i < blocks; ++i)
        for (const auto& e : h.renderBlock(snapshot))
            out.emplace_back((std::int64_t)(firstBlock + i) * kBlock + e.sample, e);
    return out;
}

std::vector<std::int64_t> onsOf(const TimedEvents& events, int pitch) {
    std::vector<std::int64_t> out;
    for (const auto& [sample, e] : events)
        if (e.isNoteOn && e.pitch == pitch)
            out.push_back(sample);
    return out;
}

} // namespace

TEST(TimelineMidiSourcePreview, ALoopWithTheTransportStoppedEmitsTheClipsNotesAtTheirSampleOffsets) {
    Harness h;
    PreviewDoc d(4.0, {makeNote(0.0, 36, 0.25), makeNote(1.0, 38, 0.25)});
    const auto snapshot = d.snapshot();
    ASSERT_FALSE(h.transport.getCurrentBlockInfo().playing);

    h.module.startClipPreview(0.0, 4.0);
    const auto events = render(h, snapshot.get(), blockOfBeat(1.5) + 2);
    ASSERT_EQ(onsOf(events, 36).size(), 1u);
    EXPECT_EQ(onsOf(events, 36)[0], 0) << "a note on the clip's first beat sounds in the first block, at its top";
    ASSERT_EQ(onsOf(events, 38).size(), 1u);
    EXPECT_EQ(onsOf(events, 38)[0], kSamplesPerBeat) << "beat 1 at 120 BPM and 48 kHz is sample 24000";
    EXPECT_EQ(h.module.getActiveNoteCount(), 0) << "both one-step notes have already ended";
}

TEST(TimelineMidiSourcePreview, TheLoopWrapsAtTheClipEndAndTheFirstNoteSoundsAgain) {
    Harness h;
    PreviewDoc d(2.0, {makeNote(0.0, 36, 0.25)});
    const auto snapshot = d.snapshot();
    h.module.startClipPreview(0.0, 2.0);

    const int blocks = 3 * kSamplesPerBeat * 2 / kBlock; // three passes
    const auto ons = onsOf(render(h, snapshot.get(), blocks), 36);
    ASSERT_GE(ons.size(), 3u);
    EXPECT_EQ(ons[0], 0);
    EXPECT_EQ(ons[1], 2 * kSamplesPerBeat) << "one clip length (two beats) later";
    EXPECT_EQ(ons[2], 4 * kSamplesPerBeat);
}

TEST(TimelineMidiSourcePreview, ANoteHeldAcrossTheClipEndIsReleasedAtTheWrapAndNothingHangs) {
    Harness h;
    PreviewDoc d(2.0, {makeNote(1.5, 40, 2.0)}); // runs past the clip end
    const auto snapshot = d.snapshot();
    h.module.startClipPreview(0.0, 2.0);

    HeldKeys held;
    for (int block = 0; block < 4 * kSamplesPerBeat * 2 / kBlock; ++block)
        for (const auto& e : h.renderBlock(snapshot.get()))
            held.apply(e, "block " + juce::String(block));
    EXPECT_TRUE(held.firstFailure.isEmpty()) << held.firstFailure;
    EXPECT_GE(held.ons, 3);
    EXPECT_GE(held.offs, held.ons - 1) << "every pass but the one in flight was released at its wrap";
}

TEST(TimelineMidiSourcePreview, AMutedPitchIsSilentAndEditsAreHeardOnTheNextPass) {
    Harness h;
    PreviewDoc d(2.0, {makeNote(0.0, 36, 0.25), makeNote(0.5, 42, 0.25)});
    ASSERT_TRUE(d.doc.setClipPitchMuted(d.clipId, 42, true));
    auto snapshot = d.snapshot();
    h.module.startClipPreview(0.0, 2.0);

    const int pass = 2 * kSamplesPerBeat / kBlock;
    const auto first = render(h, snapshot.get(), pass);
    EXPECT_EQ(onsOf(first, 36).size(), 1u);
    EXPECT_TRUE(onsOf(first, 42).empty()) << "the muted drum does not sound";

    ASSERT_TRUE(d.doc.setClipPitchMuted(d.clipId, 42, false));
    snapshot = d.snapshot();
    const auto second = render(h, snapshot.get(), pass + 2, pass);
    EXPECT_EQ(onsOf(second, 42).size(), 1u) << "unmuting is heard on the next pass";
}

TEST(TimelineMidiSourcePreview, AMutedTrackIsSilentButTheClockKeepsGoing) {
    Harness h;
    PreviewDoc d(2.0, {makeNote(0.0, 36, 0.25)});
    ASSERT_TRUE(d.doc.setTrackMuted(d.trackId, true));
    const auto snapshot = d.snapshot();
    h.module.startClipPreview(0.0, 2.0);
    EXPECT_TRUE(render(h, snapshot.get(), 2 * kSamplesPerBeat * 2 / kBlock).empty());
    EXPECT_TRUE(std::isfinite(h.module.clipPreviewBeat())) << "still running";
}

TEST(TimelineMidiSourcePreview, ThePreviewBeatFollowsTheClockAndIsNaNWhenOff) {
    Harness h;
    PreviewDoc d(4.0, {});
    const auto snapshot = d.snapshot();
    EXPECT_TRUE(std::isnan(h.module.clipPreviewBeat()));
    h.module.startClipPreview(8.0, 12.0);
    EXPECT_DOUBLE_EQ(h.module.clipPreviewBeat(), 8.0) << "the UI can show the loop before the first block runs";
    render(h, snapshot.get(), 10);
    EXPECT_NEAR(h.module.clipPreviewBeat(), 8.0 + 10.0 * kBlock / kSamplesPerBeat, 1e-9);
    h.module.stopClipPreview();
    EXPECT_TRUE(std::isnan(h.module.clipPreviewBeat()));
}

TEST(TimelineMidiSourcePreview, TheTransportPlayingEndsThePreviewAndReleasesItsNotes) {
    Harness h;
    PreviewDoc d(8.0, {makeNote(4.0, 40, 3.0), makeNote(6.0, 41, 0.5)});
    const auto snapshot = d.snapshot();
    h.module.startClipPreview(4.0, 8.0);
    render(h, snapshot.get(), 4);
    ASSERT_EQ(h.module.getActiveNoteCount(), 1) << "the long preview note is held";

    h.transport.play();
    const auto events = h.renderBlock(snapshot.get());
    bool released = false;
    for (const auto& e : events)
        released = released || (e.isNoteOff && e.pitch == 40);
    EXPECT_TRUE(released) << "the preview's note is released the block the song starts";
    EXPECT_EQ(h.module.getActiveNoteCount(), 0);
    EXPECT_TRUE(std::isnan(h.module.clipPreviewBeat())) << "the module cleared the preview itself";

    // The song then plays from beat 0, so nothing of the preview's range sounds until the song reaches it.
    const auto song = render(h, snapshot.get(), blockOfBeat(3.0), 1);
    EXPECT_TRUE(song.empty()) << "no preview notes during song playback";

    // And stopping the song does not bring the preview back.
    h.transport.stop();
    h.renderBlock(snapshot.get());
    EXPECT_TRUE(render(h, snapshot.get(), 8).empty());
    EXPECT_TRUE(std::isnan(h.module.clipPreviewBeat()));
}

TEST(TimelineMidiSourcePreview, AStartWhileTheSongPlaysIsDroppedAndNeverTurnsOnLater) {
    Harness h;
    PreviewDoc d(2.0, {makeNote(0.0, 36, 0.25)});
    const auto snapshot = d.snapshot();
    h.transport.play();
    h.renderBlock(snapshot.get());
    h.module.startClipPreview(0.0, 2.0);
    h.renderBlock(snapshot.get());
    EXPECT_TRUE(std::isnan(h.module.clipPreviewBeat()));
    h.transport.stop();
    h.renderBlock(snapshot.get());
    EXPECT_TRUE(render(h, snapshot.get(), 8).empty()) << "the dropped request does not wake up when the song stops";
}

TEST(TimelineMidiSourcePreview, StoppingThePreviewReleasesHeldNotesOnTheNextBlock) {
    Harness h;
    PreviewDoc d(4.0, {makeNote(0.0, 50, 3.0)});
    const auto snapshot = d.snapshot();
    h.module.startClipPreview(0.0, 4.0);
    render(h, snapshot.get(), 2);
    ASSERT_EQ(h.module.getActiveNoteCount(), 1);

    h.module.stopClipPreview();
    const auto events = h.renderBlock(snapshot.get());
    ASSERT_EQ(events.size(), 1u);
    EXPECT_TRUE(events[0].isNoteOff);
    EXPECT_EQ(events[0].pitch, 50);
    EXPECT_EQ(events[0].sample, 0);
    EXPECT_EQ(h.module.getActiveNoteCount(), 0);
    EXPECT_TRUE(render(h, snapshot.get(), 4).empty());
}

TEST(TimelineMidiSourcePreview, ARestartWithANewRangeReleasesTheOldPassFirst) {
    Harness h;
    PreviewDoc d(8.0, {makeNote(0.0, 50, 3.0), makeNote(4.0, 52, 0.25)});
    const auto snapshot = d.snapshot();
    h.module.startClipPreview(0.0, 4.0);
    render(h, snapshot.get(), 2);
    h.module.startClipPreview(4.0, 8.0);
    const auto events = h.renderBlock(snapshot.get());
    ASSERT_GE(events.size(), 2u);
    EXPECT_TRUE(events[0].isNoteOff && events[0].pitch == 50);
    EXPECT_TRUE(events[1].isNoteOn && events[1].pitch == 52) << "the new range starts from its first beat";
}

TEST(TimelineMidiSourcePreview, BypassEndsThePreviewWithoutStrandingNotes) {
    Harness h;
    PreviewDoc d(4.0, {makeNote(0.0, 50, 3.0)});
    const auto snapshot = d.snapshot();
    h.module.startClipPreview(0.0, 4.0);
    render(h, snapshot.get(), 2);
    h.module.setBypassed(true);
    const auto events = h.renderBlock(snapshot.get());
    ASSERT_EQ(events.size(), 1u);
    EXPECT_TRUE(events[0].isNoteOff);
    EXPECT_TRUE(std::isnan(h.module.clipPreviewBeat()));
    h.module.setBypassed(false);
    EXPECT_TRUE(render(h, snapshot.get(), 4).empty()) << "coming out of bypass does not resume the preview";
}

TEST(TimelineMidiSourcePreview, ALostTrackEndsThePreviewWithoutStrandingNotes) {
    Harness h;
    PreviewDoc d(4.0, {makeNote(0.0, 50, 3.0)});
    const auto snapshot = d.snapshot();
    h.module.startClipPreview(0.0, 4.0);
    render(h, snapshot.get(), 2);
    h.module.setNodeUuid("99999999-0000-0000-0000-000000000000"); // the snapshot no longer has a track for this node
    const auto events = h.renderBlock(snapshot.get());
    ASSERT_EQ(events.size(), 1u) << "the held note is released";
    EXPECT_TRUE(events[0].isNoteOff);
    EXPECT_TRUE(std::isnan(h.module.clipPreviewBeat()));
}

TEST(TimelineMidiSourcePreview, AClipShorterThanABlockStillFoldsInsideItsRange) {
    Harness h;
    PreviewDoc d(0.01, {makeNote(0.0, 36, 0.005)}); // 240 samples, under one block
    const auto snapshot = d.snapshot();
    h.module.startClipPreview(0.0, 0.01);
    HeldKeys held;
    for (int block = 0; block < 20; ++block) {
        for (const auto& e : h.renderBlock(snapshot.get()))
            held.apply(e, "block " + juce::String(block));
        const double beat = h.module.clipPreviewBeat();
        EXPECT_GE(beat, 0.0);
        EXPECT_LT(beat, 0.01);
    }
    EXPECT_TRUE(held.firstFailure.isEmpty()) << held.firstFailure;
}

// The audio thread renders on its own thread while the message thread starts, restarts, stops and reads the beat:
// this is the test ThreadSanitizer needs to have something to say about.
TEST(TimelineMidiSourcePreview, StartStopAndPollFromAnotherThreadLeavesNoStuckNotes) {
    Harness h;
    PreviewDoc d(2.0, {makeNote(0.0, 36, 1.5), makeNote(0.5, 38, 0.25), makeNote(1.5, 42, 1.0)});
    const auto snapshot = d.snapshot();
    std::atomic<bool> done{false};
    HeldKeys held;

    std::thread audio([&] {
        while (!done.load())
            for (const auto& e : h.renderBlock(snapshot.get()))
                held.apply(e, "running");
        for (int i = 0; i < 4; ++i) // the blocks after the final stop
            for (const auto& e : h.renderBlock(snapshot.get()))
                held.apply(e, "tail");
    });

    for (int i = 0; i < 4000; ++i) {
        if (i % 3 == 0)
            h.module.startClipPreview(0.0, 2.0 + (i % 5) * 0.25);
        else if (i % 3 == 1)
            h.module.stopClipPreview();
        const double beat = h.module.clipPreviewBeat();
        EXPECT_TRUE(std::isnan(beat) || (beat >= 0.0 && beat <= 3.0));
        if (i % 50 == 0)
            std::this_thread::yield();
    }
    h.module.stopClipPreview();
    done.store(true);
    audio.join();

    EXPECT_TRUE(held.firstFailure.isEmpty()) << held.firstFailure;
    EXPECT_EQ(held.numDown(), 0) << "nothing is left sounding after the final stop";
    EXPECT_EQ(h.module.getActiveNoteCount(), 0);
}
