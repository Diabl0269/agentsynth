// TimelineMidiSource binding tests: unbound/wrong-uuid tracks, null snapshot/stopped transport, active-note overflow,
// and the incoming MIDI buffer being replaced rather than merged.

#include "TimelineMidiSourceTestHelpers.h"

// ============================================================================
// Binding, and the degenerate inputs
// ============================================================================

TEST(TimelineMidiSourceTest, UnboundOrWrongUuidEmitsNothing) {
    Doc doc;
    ASSERT_TRUE(doc.doc.addNote(doc.clipId, makeNote(1.0, 60, 1.0)).isValid());
    auto snapshot = doc.snapshot();

    {
        // No uuid at all: an unsaved node has no identity, and "" must not match an unbound track.
        Harness h(nullptr);
        ASSERT_STREQ(h.module.getNodeUuid(), "");
        ASSERT_TRUE(h.transport.play());
        for (const auto& [block, events] : h.renderBlocks(snapshot.get(), blockOfBeat(2.0) + 1))
            ADD_FAILURE() << "a uuid-less Track In emitted " << events.size() << " events in block " << block;
    }
    {
        Harness h("99999999-8888-7777-6666-555555555555");
        ASSERT_TRUE(h.transport.play());
        for (const auto& [block, events] : h.renderBlocks(snapshot.get(), blockOfBeat(2.0) + 1))
            ADD_FAILURE() << "a Track In bound elsewhere emitted " << events.size() << " events in block " << block;
    }
}

// An unbound TRACK (empty bindingUuid) must not be picked up by a bound module either.
TEST(TimelineMidiSourceTest, UnboundTrackIsNotAdopted) {
    Doc doc("");
    ASSERT_TRUE(doc.doc.addNote(doc.clipId, makeNote(1.0, 60, 1.0)).isValid());
    auto snapshot = doc.snapshot();

    Harness h;
    ASSERT_TRUE(h.transport.play());
    for (const auto& [block, events] : h.renderBlocks(snapshot.get(), blockOfBeat(2.0) + 1))
        ADD_FAILURE() << "an unbound track was adopted — block " << block << " emitted " << events.size();
}

TEST(TimelineMidiSourceTest, NullSnapshotOrStoppedIsSilent) {
    Doc doc;
    ASSERT_TRUE(doc.doc.addNote(doc.clipId, makeNote(1.0, 60, 1.0)).isValid());
    auto snapshot = doc.snapshot();

    {
        Harness h;
        ASSERT_TRUE(h.transport.play());
        for (const auto& [block, events] : h.renderBlocks(nullptr, blockOfBeat(2.0) + 1))
            ADD_FAILURE() << "no snapshot published, yet block " << block << " emitted " << events.size();
    }
    {
        Harness h; // never told to play
        for (const auto& [block, events] : h.renderBlocks(snapshot.get(), blockOfBeat(2.0) + 1))
            ADD_FAILURE() << "a stopped transport emitted " << events.size() << " events in block " << block;
    }
}

// No playhead at all (a bare processor, or a foreign host's playhead that won't downcast).
TEST(TimelineMidiSourceTest, NoTransportPlayHeadIsSilent) {
    TimelineMidiSourceModule module;
    module.prepareToPlay(kSampleRate, kBlock);
    module.setNodeUuid(kMyUuid);

    juce::AudioBuffer<float> buffer(1, kBlock);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0); // must be replaced, not forwarded
    EXPECT_NO_THROW(module.processBlock(buffer, midi));
    EXPECT_TRUE(midi.isEmpty());
}

TEST(TimelineMidiSourceTest, ActiveNoteOverflowDropsGracefully) {
    Doc doc;
    // 129 simultaneous notes against a 128-slot table: one must be dropped, and nothing may
    // allocate or crash. (Pitch 60 appears twice — the doc allows it, and 128 distinct pitches is
    // all MIDI has.)
    for (int pitch = 0; pitch < 128; ++pitch)
        ASSERT_TRUE(doc.doc.addNote(doc.clipId, makeNote(1.0, pitch, 8.0)).isValid());
    ASSERT_TRUE(doc.doc.addNote(doc.clipId, makeNote(1.0, 60, 8.0)).isValid());
    auto snapshot = doc.snapshot();

    Harness h;
    ASSERT_TRUE(h.transport.play());
    const auto blocks = h.renderBlocks(snapshot.get(), blockOfBeat(1.0) + 1);
    ASSERT_EQ(blocks.size(), 1u);
    EXPECT_EQ(blocks[0].second.size(), (std::size_t)TimelineMidiSourceModule::kMaxActiveNotes)
        << "the overflowing note-on must be dropped, not emitted untracked";
    EXPECT_EQ(h.module.getActiveNoteCount(), TimelineMidiSourceModule::kMaxActiveNotes);

    // And the promise still holds for the 128 that were accepted.
    ASSERT_TRUE(h.transport.stop());
    const auto stopped = h.renderBlock(snapshot.get());
    EXPECT_EQ(stopped.size(), (std::size_t)TimelineMidiSourceModule::kMaxActiveNotes);
    EXPECT_EQ(h.module.getActiveNoteCount(), 0);
}

// The module is a source: whatever the graph handed it is replaced, never forwarded.
TEST(TimelineMidiSourceTest, ReplacesIncomingMidiBuffer) {
    Doc doc;
    auto snapshot = doc.snapshot();

    Harness h;
    ASSERT_TRUE(h.transport.play());
    h.transport.tick(kBlock);
    h.transport.setCurrentTimelineSnapshot(snapshot.get());
    h.midi.clear();
    h.midi.addEvent(juce::MidiMessage::noteOn(1, 42, (juce::uint8)100), 17);
    h.module.processBlock(h.buffer, h.midi);
    EXPECT_TRUE(h.midi.isEmpty());
}
