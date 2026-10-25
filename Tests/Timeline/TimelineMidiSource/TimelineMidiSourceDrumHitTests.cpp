// Track In and the per-hit drum settings: a note-on is gated by TimelineSnapshot::notePlays with a pass counter (the
// completed clip wraps of a preview, the loop wraps of the transport), a skipped hit owes no note-off, and the ticket's
// per-drum length reaches the output as note-ons on the right steps.

#include "TimelineMidiSourceTestHelpers.h"

namespace {

constexpr double kStep = 0.25;

struct DrumDoc : Doc {
    explicit DrumDoc(double clipLength = 4.0)
        : Doc(kMyUuid, clipLength) {}
    synth::NoteId add(int cell, int pitch, int velocity = 100) {
        return doc.addNote(clipId, makeNote(cell * kStep, pitch, 0.1, velocity));
    }
};

using Ons = std::vector<std::int64_t>;

// Sample positions (counted from the first block) of every note-on of `pitch` over `blocks` blocks.
Ons onsOf(Harness& h, const TimelineSnapshot* snapshot, int pitch, int blocks, HeldKeys* held = nullptr) {
    Ons out;
    for (int i = 0; i < blocks; ++i)
        for (const auto& e : h.renderBlock(snapshot)) {
            if (held != nullptr)
                held->apply(e, "block " + juce::String(i));
            if (e.isNoteOn && e.pitch == pitch)
                out.push_back(static_cast<std::int64_t>(i) * kBlock + e.sample);
        }
    return out;
}

int blocksForBeats(double beats) { return static_cast<int>(beats * kSamplesPerBeat / kBlock) + 2; }

} // namespace

TEST(TimelineMidiSourceDrumHit, ATwoOfTwoConditionPlaysOnTheSecondPreviewPassOnly) {
    Harness h;
    DrumDoc d(2.0);
    d.add(0, 38);
    ASSERT_TRUE(d.doc.setNoteCondition(d.doc.getClip(d.clipId)->notes.front().id, 2, 2));
    const auto snapshot = d.snapshot();
    h.module.startClipPreview(0.0, 2.0);

    const int pass = 2 * kSamplesPerBeat / kBlock; // blocks per pass (46.875, so passes drift by a partial block)
    HeldKeys held;
    const Ons ons = onsOf(h, snapshot.get(), 38, 4 * pass, &held);
    EXPECT_TRUE(held.firstFailure.isEmpty()) << held.firstFailure;
    ASSERT_EQ(ons.size(), 2u) << "four passes: only the 2nd and 4th play";
    EXPECT_NEAR(static_cast<double>(ons[0]), 2.0 * kSamplesPerBeat, kBlock);
    EXPECT_NEAR(static_cast<double>(ons[1]), 6.0 * kSamplesPerBeat, kBlock);
}

TEST(TimelineMidiSourceDrumHit, ARestartedPreviewStartsAtPassZeroAgain) {
    Harness h;
    DrumDoc d(2.0);
    d.add(0, 38);
    ASSERT_TRUE(d.doc.setNoteCondition(d.doc.getClip(d.clipId)->notes.front().id, 2, 2));
    const auto snapshot = d.snapshot();
    h.module.startClipPreview(0.0, 2.0);
    EXPECT_TRUE(onsOf(h, snapshot.get(), 38, blocksForBeats(1.5)).empty()) << "pass 1 of 2 is silent";
    h.module.startClipPreview(0.0, 2.0); // restart
    EXPECT_TRUE(onsOf(h, snapshot.get(), 38, blocksForBeats(1.5)).empty()) << "still pass 1 of 2 after a restart";
}

TEST(TimelineMidiSourceDrumHit, ASkippedHitOwesNoNoteOffAndNothingHangs) {
    Harness h;
    DrumDoc d(2.0);
    d.add(0, 38);
    d.add(2, 42);
    ASSERT_TRUE(d.doc.setNoteChance(d.doc.getClip(d.clipId)->notes.front().id, 0));
    const auto snapshot = d.snapshot();
    h.module.startClipPreview(0.0, 2.0);
    HeldKeys held;
    int sawOffForSkipped = 0;
    for (int i = 0; i < 4 * blocksForBeats(2.0); ++i)
        for (const auto& e : h.renderBlock(snapshot.get())) {
            held.apply(e, "block " + juce::String(i));
            sawOffForSkipped += e.pitch == 38 ? 1 : 0;
        }
    EXPECT_TRUE(held.firstFailure.isEmpty()) << held.firstFailure;
    EXPECT_EQ(sawOffForSkipped, 0) << "a 0% hit emits neither an on nor an off";
    EXPECT_GE(held.ons, 3);
    EXPECT_LE(h.module.getActiveNoteCount(), 1);
}

TEST(TimelineMidiSourceDrumHit, TheTransportLoopCountsItsWrapsAsPasses) {
    Harness h;
    DrumDoc d(2.0);
    d.add(0, 38);
    ASSERT_TRUE(d.doc.setNoteCondition(d.doc.getClip(d.clipId)->notes.front().id, 2, 2));
    const auto snapshot = d.snapshot();
    ASSERT_TRUE(h.transport.setLoop(0.0, 2.0, true));
    ASSERT_TRUE(h.transport.play());
    const Ons ons = onsOf(h, snapshot.get(), 38, blocksForBeats(2.0 * 4));
    ASSERT_EQ(ons.size(), 2u) << "four loop passes: the 2nd and 4th";
    EXPECT_EQ(ons[0], 2 * kSamplesPerBeat);
    EXPECT_EQ(ons[1], 6 * kSamplesPerBeat);
}

TEST(TimelineMidiSourceDrumHit, ALocateStartsTheLoopPassesOver) {
    Harness h;
    DrumDoc d(2.0);
    d.add(0, 38);
    ASSERT_TRUE(d.doc.setNoteCondition(d.doc.getClip(d.clipId)->notes.front().id, 2, 2));
    const auto snapshot = d.snapshot();
    ASSERT_TRUE(h.transport.setLoop(0.0, 2.0, true));
    ASSERT_TRUE(h.transport.play());
    onsOf(h, snapshot.get(), 38, blocksForBeats(2.5)); // into pass 2
    h.transport.stop();
    ASSERT_TRUE(h.transport.play());
    EXPECT_TRUE(onsOf(h, snapshot.get(), 38, blocksForBeats(1.5)).empty()) << "pass 1 of 2 again after play";
}

// Audio-unheard guard: the ticket's own check, heard at the output. Closed hat (42) with a row length of 12 in a
// one-bar 16-step clip: steps 1..4 also sound at 13..16, and the note stored at step 14 never sounds.
TEST(TimelineMidiSourceDrumHit, AClosedHatOfLength12SoundsStepsOneToFourAgainAtThirteenToSixteen) {
    Harness h;
    DrumDoc d(4.0);
    for (int cell = 0; cell < 4; ++cell)
        d.add(cell, 42, 101 + cell);
    d.add(13, 42, 77);
    ASSERT_TRUE(d.doc.setClipRowLength(d.clipId, 42, 12));
    const auto snapshot = d.snapshot();
    ASSERT_TRUE(h.transport.play());

    std::vector<int> velocityAtStep(16, 0);
    std::vector<int> countAtStep(16, 0);
    for (int i = 0; i < blocksForBeats(4.0); ++i)
        for (const auto& e : h.renderBlock(snapshot.get())) {
            if (!e.isNoteOn || e.pitch != 42)
                continue;
            const double beat = static_cast<double>(i * kBlock + e.sample) / kSamplesPerBeat;
            const int step = static_cast<int>(std::lround(beat / kStep));
            if (step < 0 || step > 15)
                continue;
            ++countAtStep[static_cast<std::size_t>(step)];
            velocityAtStep[static_cast<std::size_t>(step)] = e.velocity;
        }
    for (int step = 0; step < 16; ++step) {
        const bool plays = step < 4 || step >= 12;
        EXPECT_EQ(countAtStep[static_cast<std::size_t>(step)], plays ? 1 : 0) << "step " << step + 1;
    }
    for (int i = 0; i < 4; ++i)
        EXPECT_EQ(velocityAtStep[static_cast<std::size_t>(12 + i)], 101 + i);
    EXPECT_NE(velocityAtStep[13], 77) << "step 14 plays the copy of step 2, not the stored note";
}
