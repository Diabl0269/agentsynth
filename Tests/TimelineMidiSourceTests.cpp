// Tests for TL3-1: the "Track In" module (TimelineMidiSourceModule) — the graph-side end of a
// timeline MIDI track.
//
// The module is a PULL consumer: nothing schedules events into it, so every test here drives it
// the same way the real audio callback does — tick the transport, park the block's snapshot on it
// (TransportService::setCurrentTimelineSnapshot, the TL3-1 handoff), call processBlock, read the
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
#include <gtest/gtest.h>
#include <memory>
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

    std::vector<Event> renderBlock(const TimelineSnapshot* snapshot) {
        transport.tick(kBlock);
        transport.setCurrentTimelineSnapshot(snapshot);
        buffer.clear();
        midi.clear();
        module.processBlock(buffer, midi);
        return eventsOf(midi);
    }

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

// A one-track document bound to `uuid`, with one 16-beat clip starting at 0.
struct Doc {
    TimelineDoc doc;
    synth::TrackId trackId;
    synth::ClipId clipId;

    explicit Doc(const char* uuid = kMyUuid) {
        trackId = doc.addTrack(TrackKind::Midi, "Track 1");
        doc.setTrackBinding(trackId, uuid);
        clipId = doc.addClip(trackId, 0.0, 16.0, "Clip");
    }

    std::unique_ptr<TimelineSnapshot> snapshot() const { return TimelineSnapshot::buildFrom(doc); }
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
// Engine integration: the whole TL3-1 chain, through a real graph
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
