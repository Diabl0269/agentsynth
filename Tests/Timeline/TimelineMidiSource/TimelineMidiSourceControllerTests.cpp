// TimelineMidiSource CC-lane playback: a clip's CC lanes leave "Track In" as MIDI CC messages on
// the clip's note channels, only when the 7-bit value changes, with chase on start/locate/loop wrap.
// See docs/timeline/piano-roll-lanes.md#playback. Timing arithmetic as in the helpers header.

#include "TimelineMidiSourceTestHelpers.h"

namespace {

struct Cc {
    int block = 0;
    int sample = 0;
    int channel = 0;
    int cc = 0;
    int value = 0;
};

std::vector<Cc> ccsOf(const juce::MidiBuffer& buffer, int block) {
    std::vector<Cc> out;
    for (const auto metadata : buffer) {
        const auto message = metadata.getMessage();
        if (message.isController())
            out.push_back({block, metadata.samplePosition, message.getChannel(), message.getControllerNumber(),
                           message.getControllerValue()});
    }
    return out;
}

std::vector<Cc> renderCcs(Harness& h, const TimelineSnapshot* snapshot, int numBlocks, int firstBlock = 0) {
    std::vector<Cc> out;
    for (int i = 0; i < numBlocks; ++i) {
        h.renderBlock(snapshot);
        const auto events = ccsOf(h.midi, firstBlock + i);
        out.insert(out.end(), events.begin(), events.end());
    }
    return out;
}

synth::ControllerPoint cp(double beat, double value, int curve = 1) { return {beat, value, curve}; }

} // namespace

TEST(TimelineMidiSourceControllerTest, HoldLaneEmitsEachChangeOnceAtItsSample) {
    Doc doc;
    ASSERT_TRUE(doc.doc.setControllerLanePoints(doc.clipId, 1, {cp(0.0, 10.0, 0), cp(1.0, 90.0, 0)}));
    auto snapshot = doc.snapshot();

    Harness h;
    ASSERT_TRUE(h.transport.play());
    const auto ccs = renderCcs(h, snapshot.get(), blockOfBeat(2.0));
    ASSERT_EQ(ccs.size(), 2u) << "a Hold lane changes value exactly twice";
    EXPECT_EQ(ccs[0].block, 0);
    EXPECT_EQ(ccs[0].sample, 0);
    EXPECT_EQ(ccs[0].value, 10);
    EXPECT_EQ(ccs[0].cc, 1);
    EXPECT_EQ(ccs[0].channel, 1);
    EXPECT_EQ(ccs[1].block, blockOfBeat(1.0));
    EXPECT_EQ(ccs[1].sample, offsetOfBeat(1.0));
    EXPECT_EQ(ccs[1].value, 90);
    EXPECT_EQ(h.module.getLastSentControllerValue(1, 1), 90);
}

TEST(TimelineMidiSourceControllerTest, LinearRampStepsMonotonicallyAndNeverRepeatsAValue) {
    Doc doc;
    ASSERT_TRUE(doc.doc.setControllerLanePoints(doc.clipId, 11, {cp(0.0, 0.0), cp(1.0, 127.0)}));
    auto snapshot = doc.snapshot();

    Harness h;
    ASSERT_TRUE(h.transport.play());
    const auto ccs = renderCcs(h, snapshot.get(), blockOfBeat(1.5));
    ASSERT_GE(ccs.size(), 40u) << "one step per block across the 47-block ramp";
    for (size_t i = 1; i < ccs.size(); ++i)
        EXPECT_GT(ccs[i].value, ccs[i - 1].value) << "only CHANGES are sent";
    EXPECT_EQ(ccs.back().value, 127);
    EXPECT_EQ(ccs.back().block, blockOfBeat(1.0)) << "the endpoint lands on its own breakpoint";
}

TEST(TimelineMidiSourceControllerTest, EmitsOnEveryChannelTheClipsNotesUse) {
    Doc doc;
    ASSERT_TRUE(doc.doc.addNote(doc.clipId, makeNote(4.0, 60, 1.0, 100, 2)).isValid());
    ASSERT_TRUE(doc.doc.addNote(doc.clipId, makeNote(5.0, 60, 1.0, 100, 5)).isValid());
    ASSERT_TRUE(doc.doc.setControllerLanePoints(doc.clipId, 7, {cp(0.0, 100.0, 0)}));
    auto snapshot = doc.snapshot();

    Harness h;
    ASSERT_TRUE(h.transport.play());
    const auto ccs = renderCcs(h, snapshot.get(), 1);
    ASSERT_EQ(ccs.size(), 2u);
    EXPECT_EQ(ccs[0].channel, 2);
    EXPECT_EQ(ccs[1].channel, 5);
}

TEST(TimelineMidiSourceControllerTest, CcPrecedesANoteOnAtTheSameSample) {
    Doc doc;
    ASSERT_TRUE(doc.doc.addNote(doc.clipId, makeNote(1.0, 60)).isValid());
    ASSERT_TRUE(doc.doc.setControllerLanePoints(doc.clipId, 1, {cp(0.0, 0.0, 0), cp(1.0, 64.0, 0)}));
    auto snapshot = doc.snapshot();

    Harness h;
    ASSERT_TRUE(h.transport.play());
    h.renderBlocks(snapshot.get(), blockOfBeat(1.0));
    h.renderBlock(snapshot.get());
    bool sawCc = false;
    for (const auto metadata : h.midi) {
        const auto message = metadata.getMessage();
        if (message.isController())
            sawCc = true;
        if (message.isNoteOn())
            EXPECT_TRUE(sawCc) << "the controller reaches the destination before the note it shapes";
    }
    EXPECT_TRUE(sawCc);
}

TEST(TimelineMidiSourceControllerTest, LocateChasesTheCurrentValue) {
    Doc doc;
    ASSERT_TRUE(doc.doc.setControllerLanePoints(doc.clipId, 1, {cp(0.0, 20.0, 0), cp(4.0, 80.0, 0)}));
    auto snapshot = doc.snapshot();

    Harness h;
    ASSERT_TRUE(h.transport.play());
    auto ccs = renderCcs(h, snapshot.get(), 4);
    ASSERT_EQ(ccs.size(), 1u);
    EXPECT_EQ(ccs[0].value, 20);

    // Locate into the middle of the second segment: the value there (80) is re-sent at once.
    ASSERT_TRUE(h.transport.locateBeat(6.0));
    ccs = renderCcs(h, snapshot.get(), 2);
    ASSERT_EQ(ccs.size(), 1u);
    EXPECT_EQ(ccs[0].value, 80);
    EXPECT_EQ(ccs[0].sample, 0);

    // Locate back into the first segment: 20 is chased even though it was sent before.
    ASSERT_TRUE(h.transport.locateBeat(1.0));
    ccs = renderCcs(h, snapshot.get(), 1);
    ASSERT_EQ(ccs.size(), 1u);
    EXPECT_EQ(ccs[0].value, 20);
}

TEST(TimelineMidiSourceControllerTest, StopReleasesAHeldSustainAndRestartChases) {
    Doc doc;
    ASSERT_TRUE(doc.doc.setControllerLanePoints(doc.clipId, 64, {cp(0.0, 127.0, 0)}));
    auto snapshot = doc.snapshot();

    Harness h;
    ASSERT_TRUE(h.transport.play());
    auto ccs = renderCcs(h, snapshot.get(), 3);
    ASSERT_EQ(ccs.size(), 1u);
    EXPECT_EQ(ccs[0].value, 127);

    ASSERT_TRUE(h.transport.stop());
    ccs = renderCcs(h, snapshot.get(), 1);
    ASSERT_EQ(ccs.size(), 1u) << "stop must not leave the pedal down under the flushed notes";
    EXPECT_EQ(ccs[0].cc, 64);
    EXPECT_EQ(ccs[0].value, 0);

    ASSERT_TRUE(h.transport.play());
    ccs = renderCcs(h, snapshot.get(), 1);
    ASSERT_EQ(ccs.size(), 1u) << "restart chases the lane's value";
    EXPECT_EQ(ccs[0].value, 127);
}

TEST(TimelineMidiSourceControllerTest, LoopWrapChasesTheLoopStartValue) {
    Doc doc;
    ASSERT_TRUE(doc.doc.setControllerLanePoints(doc.clipId, 1, {cp(0.0, 30.0, 0), cp(1.0, 100.0, 0)}));
    auto snapshot = doc.snapshot();

    Harness h;
    ASSERT_TRUE(h.transport.setLoop(0.0, 2.0, true));
    ASSERT_TRUE(h.transport.play());
    const auto ccs = renderCcs(h, snapshot.get(), blockOfBeat(2.0) + 2);
    ASSERT_EQ(ccs.size(), 3u) << "30 at the start, 100 at beat 1, 30 again at the wrap";
    EXPECT_EQ(ccs[0].value, 30);
    EXPECT_EQ(ccs[1].value, 100);
    EXPECT_EQ(ccs[2].value, 30);
    EXPECT_EQ(ccs[2].block, blockOfBeat(2.0));
}

TEST(TimelineMidiSourceControllerTest, MutedClipAndOutsideTheClipWindowSendNothing) {
    Doc doc("11111111-2222-3333-4444-555555555555", 2.0); // clip covers beats 0..2
    ASSERT_TRUE(doc.doc.setControllerLanePoints(doc.clipId, 1, {cp(0.0, 10.0, 0), cp(3.0, 90.0, 0)}));
    auto snapshot = doc.snapshot();

    Harness h;
    ASSERT_TRUE(h.transport.play());
    const auto ccs = renderCcs(h, snapshot.get(), blockOfBeat(4.0));
    ASSERT_EQ(ccs.size(), 1u) << "the point at beat 3 lies past the clip end and never plays";
    EXPECT_EQ(ccs[0].value, 10);

    doc.doc.setClipMuted(doc.clipId, true);
    auto mutedSnapshot = doc.snapshot();
    Harness h2;
    ASSERT_TRUE(h2.transport.play());
    EXPECT_TRUE(renderCcs(h2, mutedSnapshot.get(), 8).empty());
}
