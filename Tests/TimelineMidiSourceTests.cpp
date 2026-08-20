// Tests for the "Track In" module (TimelineMidiSourceModule) — the graph-side end of a
// timeline MIDI track.
//
// The module is a PULL consumer: nothing schedules events into it, so every test here drives it
// the same way the real audio callback does — tick the transport, park the block's snapshot on it
// (TransportService::setCurrentTimelineSnapshot), call processBlock, read the
// MIDI buffer. Most of the file needs no engine and no graph at all, which is what keeps the
// timing assertions exact.
//
// Timing arithmetic used throughout: 48 kHz, 512-sample blocks, 120 BPM => 24000 samples per beat.
// Beat 1.0 is sample 24000, which is block 46 (46 * 512 = 23552) at offset 448.
//
// Headless/deterministic house rules apply: no audio device (the one engine here is
// HostMode::Hosted), no network, no sleeps.

#include "../Source/AI/AIStateMapper.h"
#include "../Source/AudioEngine.h"
#include "../Source/Modules/PolyMidiModule.h"
#include "../Source/Modules/TimelineMidiSourceModule.h"
#include "../Source/Transport/TransportService.h"
#include "Timeline/TimelineDoc.h"
#include "Timeline/TimelineSnapshot.h"
#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <map>
#include <memory>
#include <utility>
#include <vector>

using synth::MidiNote;
using synth::TimelineDoc;
using synth::TimelineSnapshot;
using synth::TrackKind;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlock = 512;
constexpr int kSamplesPerBeat = 24000; // 120 BPM at 48 kHz
constexpr const char* kMyUuid = "11111111-2222-3333-4444-555555555555";

// The block a given absolute beat lands in, and the offset within it.
int blockOfBeat(double beat) { return (int)((beat * kSamplesPerBeat) / kBlock); }
int offsetOfBeat(double beat) { return (int)((juce::int64)(beat * kSamplesPerBeat) % kBlock); }

struct Event {
    int sample = 0;
    bool isNoteOn = false;
    int pitch = 0;
    int velocity = 0;
    int channel = 0;
    // The module's contract is "note-ons and note-offs, nothing else" — in particular never a
    // blanket all-notes-off (CC 123), which would silence other sources sharing the MIDI cable.
    bool isNoteOff = false;
    bool isOther = false;
    juce::String description;
};

std::vector<Event> eventsOf(const juce::MidiBuffer& buffer) {
    std::vector<Event> events;
    for (const auto metadata : buffer) {
        const auto message = metadata.getMessage();
        Event e;
        e.sample = metadata.samplePosition;
        e.isNoteOn = message.isNoteOn();
        e.pitch = message.getNoteNumber();
        e.velocity = message.getVelocity();
        e.channel = message.getChannel();
        e.isNoteOff = message.isNoteOff();
        e.isOther = !e.isNoteOn && !e.isNoteOff;
        e.description = message.getDescription();
        events.push_back(e);
    }
    return events;
}

// Drives one module exactly as AudioEngine::renderNextBlock does, minus the graph.
struct Harness {
    synth::TransportService transport;
    TimelineMidiSourceModule module;
    juce::AudioBuffer<float> buffer{1, kBlock};
    juce::MidiBuffer midi;

    explicit Harness(const char* uuid = kMyUuid) {
        transport.prepare(kSampleRate, kBlock);
        module.setPlayHead(&transport);
        module.prepareToPlay(kSampleRate, kBlock);
        if (uuid != nullptr)
            module.setNodeUuid(uuid);
    }

    std::vector<Event> renderBlock(const TimelineSnapshot* snapshot, int numSamples = kBlock) {
        transport.tick(numSamples);
        transport.setCurrentTimelineSnapshot(snapshot);
        if (buffer.getNumSamples() != numSamples)
            buffer.setSize(1, numSamples, false, false, false);
        buffer.clear();
        midi.clear();
        module.processBlock(buffer, midi);
        return eventsOf(midi);
    }

    // The transport sample position this block started at — the block info survives processBlock,
    // so a test can turn a block-relative offset into an absolute (== loop-relative, when the loop
    // starts at 0) sample position.
    std::int64_t blockStartSample() const { return transport.getCurrentBlockInfo().blockStartSample; }
    int loopWrapSample() const { return transport.getCurrentBlockInfo().loopWrapSample; }

    // Renders `numBlocks` blocks, returning only those that produced events, tagged with the
    // 0-based block index — so a failure says WHICH block was wrong, not just that one was.
    std::vector<std::pair<int, std::vector<Event>>> renderBlocks(const TimelineSnapshot* snapshot, int numBlocks,
                                                                 int firstBlockIndex = 0) {
        std::vector<std::pair<int, std::vector<Event>>> out;
        for (int i = 0; i < numBlocks; ++i) {
            auto events = renderBlock(snapshot);
            if (!events.empty())
                out.emplace_back(firstBlockIndex + i, std::move(events));
        }
        return out;
    }
};

MidiNote makeNote(double startBeat, int pitch, double lengthBeats = 1.0, int velocity = 100, int channel = 1) {
    MidiNote note;
    note.startBeat = startBeat;
    note.lengthBeats = lengthBeats;
    note.pitch = pitch;
    note.velocity = velocity;
    note.channel = channel;
    return note;
}

// A one-track document bound to `uuid`, with one clip (16 beats by default) starting at 0.
struct Doc {
    TimelineDoc doc;
    synth::TrackId trackId;
    synth::ClipId clipId;

    explicit Doc(const char* uuid = kMyUuid, double clipLengthBeats = 16.0) {
        trackId = doc.addTrack(TrackKind::Midi, "Track 1");
        doc.setTrackBinding(trackId, uuid);
        clipId = doc.addClip(trackId, 0.0, clipLengthBeats, "Clip");
    }

    std::unique_ptr<TimelineSnapshot> snapshot() const { return TimelineSnapshot::buildFrom(doc); }
};

// ---- helpers ----------------------------------------------------------------------------------

// One emitted event plus the block it came out of. A wrapping block splits into two beat ranges, so
// "which block, and where was that block on the transport" is what turns an offset into a musical
// position a test can assert on.
struct BlockEvent {
    int blockIndex = 0;
    std::int64_t blockStart = 0;
    int wrapSample = -1;
    Event event;

    // Absolute transport sample of the event. Only meaningful for events in the block's PRIMARY
    // range — past the wrap the block's samples belong to the next loop pass.
    std::int64_t absSample() const { return blockStart + event.sample; }
};

std::vector<BlockEvent> renderCollect(Harness& h, const TimelineSnapshot* snapshot, int numBlocks,
                                      int numSamples = kBlock) {
    std::vector<BlockEvent> out;
    for (int i = 0; i < numBlocks; ++i) {
        const auto events = h.renderBlock(snapshot, numSamples);
        for (const auto& e : events)
            out.push_back({i, h.blockStartSample(), h.loopWrapSample(), e});
    }
    return out;
}

// A model of what the emitted stream says is sounding, and the three invariants that make the
// module's promise checkable from the outside: nothing but note-ons and note-offs is ever emitted,
// a note-on for a key already down means we never released it, and a note-off for a key that is up
// means we emitted an off we never owed.
struct HeldKeys {
    std::map<std::pair<int, int>, int> down; // (channel, pitch) -> 0 or 1
    int ons = 0;
    int offs = 0;
    juce::String firstFailure;

    void apply(const Event& e, const juce::String& where) {
        if (e.isOther) {
            fail(where + ": emitted a non-note message (" + e.description +
                 ") — this source sends per-note offs only, never CC 123");
            return;
        }

        const auto key = std::make_pair(e.channel, e.pitch);
        const juce::String what = where + ": ch" + juce::String(e.channel) + " pitch " + juce::String(e.pitch);
        if (e.isNoteOn) {
            ++ons;
            if (down[key] != 0)
                fail(what + " note-on while it is already sounding (no release in between)");
            down[key] = 1;
        } else {
            ++offs;
            if (down[key] != 1)
                fail(what + " note-off for a key that is not sounding");
            down[key] = 0;
        }
    }

    int numDown() const {
        int n = 0;
        for (const auto& entry : down)
            n += entry.second;
        return n;
    }

    void fail(const juce::String& message) {
        if (firstFailure.isEmpty())
            firstFailure = message;
    }
};

} // namespace

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

// ============================================================================
// Engine integration: the whole Track In chain, through a real graph
// ============================================================================

#if SYNTH_ENABLE_TIMELINE

// Everything the increment added, wired together: a trusted patch creates a Track In node and
// hands it a uuid (which adoptUuidIfTrusted must MIRROR onto the processor — plumbing piece 2),
// AudioEngine::renderNextBlock parks the block's snapshot on the transport (piece 1), the module
// finds its track and emits notes, and Poly MIDI turns them into gate CV that reaches the engine's
// output buffer. Nothing here reaches into the module: the assertion is on rendered audio.
TEST(TimelineMidiSourceTest, EngineRendersTimelineNotesAsGateCV) {
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    engine.initialise();

    // Trusted patch: Track In --MIDI--> Poly MIDI, Poly MIDI gate voice 0 (raw channel 8) --> the
    // graph's audio output, so the gate is directly observable in the rendered buffer.
    const juce::String patchJson = juce::String(R"({
        "nodes": [
            {"id": 1, "type": "Track In",     "uuid": ")") +
                                   kMyUuid + R"("},
            {"id": 2, "type": "Poly MIDI",    "uuid": "aaaaaaaa-0000-0000-0000-000000000002"},
            {"id": 3, "type": "Audio Output", "uuid": "aaaaaaaa-0000-0000-0000-000000000003"}
        ],
        "connections": [
            {"src": 1, "srcPort": -1, "dst": 2, "dstPort": -1},
            {"src": 2, "srcPort": 8,  "dst": 3, "dstPort": 0}
        ]
    })";

    const juce::var patch = juce::JSON::parse(patchJson);
    ASSERT_TRUE(patch.isObject()) << "test patch JSON is malformed";
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(patch, engine.getGraph(), /*clearExisting=*/true,
                                                       /*trusted=*/true));

    // The mirror is what makes the module recognisable to the timeline at all.
    TimelineMidiSourceModule* trackIn = nullptr;
    PolyMidiModule* polyMidi = nullptr;
    for (auto* node : engine.getGraph().getNodes()) {
        if (auto* t = dynamic_cast<TimelineMidiSourceModule*>(node->getProcessor()))
            trackIn = t;
        if (auto* p = dynamic_cast<PolyMidiModule*>(node->getProcessor()))
            polyMidi = p;
    }
    ASSERT_NE(trackIn, nullptr) << "the trusted patch must have created a Track In node";
    ASSERT_NE(polyMidi, nullptr);
    EXPECT_STREQ(trackIn->getNodeUuid(), kMyUuid) << "a trusted apply must mirror the node uuid into the processor";

    engine.prepareForHost(kSampleRate, kBlock, 0, 2);

    // One note, beat 1.0 -> 2.0, on the track bound to that uuid.
    TimelineDoc doc;
    const auto trackId = doc.addTrack(TrackKind::Midi, "Track 1");
    ASSERT_TRUE(doc.setTrackBinding(trackId, kMyUuid));
    const auto clipId = doc.addClip(trackId, 0.0, 8.0, "Clip");
    ASSERT_TRUE(doc.addNote(clipId, makeNote(1.0, 60, 1.0)).isValid());
    engine.getTimelineSnapshots().publish(TimelineSnapshot::buildFrom(doc));

    ASSERT_TRUE(engine.getTransport().play());

    // Render past the note-on (block 46) and past the note-off (block 93), sampling the gate at
    // the end of each block so Poly MIDI's 5 ms gate smoothing has settled.
    float gateDuringNote = 0.0f;
    float gateAfterNote = 1.0f;
    for (int block = 0; block <= 100; ++block) {
        juce::AudioBuffer<float> buffer(2, kBlock);
        buffer.clear();
        juce::MidiBuffer midi;
        engine.processHostBlock(buffer, midi);

        const float lastSample = buffer.getSample(0, kBlock - 1);
        if (block == 60) // comfortably inside [beat 1, beat 2)
            gateDuringNote = lastSample;
        if (block == 100) // well past the note-off in block 93
            gateAfterNote = lastSample;
    }

    EXPECT_NEAR(gateDuringNote, 1.0f, 1.0e-3f) << "the gate must be high while the timeline note sounds";
    EXPECT_NEAR(gateAfterNote, 0.0f, 1.0e-3f) << "and back down once it ends";

    engine.releaseFromHost();
    engine.shutdown();
}

#endif // SYNTH_ENABLE_TIMELINE
