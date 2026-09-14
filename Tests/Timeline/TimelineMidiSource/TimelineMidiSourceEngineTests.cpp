// TimelineMidiSource engine-integration tests: the whole Track In chain, through a real graph, rendering timeline notes
// as gate CV.

#include "TimelineMidiSourceTestHelpers.h"

// ============================================================================
// Engine integration: the whole Track In chain, through a real graph
// ============================================================================

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

// THE user report: "stopping the timeline while a note is playing leaves the note sounding forever."
// Driven through the whole engine (transport tick -> snapshot publish -> graph render -> gate CV),
// because the module-level StopFlushesActiveNotes above already passes — so if this hangs, the
// note-off is being emitted and then lost somewhere between the module and the instrument.
TEST(TimelineMidiSourceTest, EngineStopMidNoteDropsTheGateWithinOneBlock) {
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    engine.initialise();

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
    ASSERT_TRUE(patch.isObject());
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(patch, engine.getGraph(), /*clearExisting=*/true,
                                                       /*trusted=*/true));

    TimelineMidiSourceModule* trackIn = nullptr;
    for (auto* node : engine.getGraph().getNodes())
        if (auto* t = dynamic_cast<TimelineMidiSourceModule*>(node->getProcessor()))
            trackIn = t;
    ASSERT_NE(trackIn, nullptr);

    engine.prepareForHost(kSampleRate, kBlock, 0, 2);

    // A LONG note — beat 1 to beat 9 — so the stop below lands squarely inside it.
    TimelineDoc doc;
    const auto trackId = doc.addTrack(TrackKind::Midi, "Track 1");
    ASSERT_TRUE(doc.setTrackBinding(trackId, kMyUuid));
    const auto clipId = doc.addClip(trackId, 0.0, 16.0, "Clip");
    ASSERT_TRUE(doc.addNote(clipId, makeNote(1.0, 60, 8.0)).isValid());
    engine.getTimelineSnapshots().publish(TimelineSnapshot::buildFrom(doc));

    ASSERT_TRUE(engine.getTransport().play());

    const auto renderOne = [&engine] {
        juce::AudioBuffer<float> buffer(2, kBlock);
        buffer.clear();
        juce::MidiBuffer midi;
        engine.processHostBlock(buffer, midi);
        return buffer.getSample(0, kBlock - 1);
    };

    float gateDuringNote = 0.0f;
    for (int block = 0; block <= 60; ++block)
        gateDuringNote = renderOne();
    ASSERT_NEAR(gateDuringNote, 1.0f, 1.0e-3f) << "precondition: the note is sounding when we stop";
    ASSERT_EQ(trackIn->getActiveNoteCount(), 1) << "precondition: the source is holding it";

    // STOP, mid-note.
    ASSERT_TRUE(engine.getTransport().stop());

    // The block the stop takes effect in must release the note.
    renderOne();
    EXPECT_EQ(trackIn->getActiveNoteCount(), 0) << "the stop must release the source's held notes";

    // The gate has 5 ms of smoothing on it, so give it a few blocks to fall — but it must actually
    // fall. THIS is the assertion the user's bug fails: the note sounds forever.
    float gateAfterStop = 1.0f;
    for (int block = 0; block < 40; ++block)
        gateAfterStop = renderOne();
    EXPECT_NEAR(gateAfterStop, 0.0f, 1.0e-3f) << "stopping the timeline mid-note must silence it";

    engine.releaseFromHost();
    engine.shutdown();
}

// The same stop, with automation SLICING on — a callback becomes several 64-sample render passes,
// so the stop transition lands on one slice and the continuity prediction has to survive the rest.
TEST(TimelineMidiSourceTest, EngineStopMidNoteDropsTheGateWithAutomationSlicing) {
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    engine.initialise();
    engine.setAutomationSlicingEnabled(true);

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
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(juce::JSON::parse(patchJson), engine.getGraph(),
                                                       /*clearExisting=*/true, /*trusted=*/true));

    TimelineMidiSourceModule* trackIn = nullptr;
    for (auto* node : engine.getGraph().getNodes())
        if (auto* t = dynamic_cast<TimelineMidiSourceModule*>(node->getProcessor()))
            trackIn = t;
    ASSERT_NE(trackIn, nullptr);

    engine.prepareForHost(kSampleRate, kBlock, 0, 2);

    TimelineDoc doc;
    const auto trackId = doc.addTrack(TrackKind::Midi, "Track 1");
    ASSERT_TRUE(doc.setTrackBinding(trackId, kMyUuid));
    const auto clipId = doc.addClip(trackId, 0.0, 16.0, "Clip");
    ASSERT_TRUE(doc.addNote(clipId, makeNote(1.0, 60, 8.0)).isValid());
    engine.getTimelineSnapshots().publish(TimelineSnapshot::buildFrom(doc));

    ASSERT_TRUE(engine.getTransport().play());

    const auto renderOne = [&engine] {
        juce::AudioBuffer<float> buffer(2, kBlock);
        buffer.clear();
        juce::MidiBuffer midi;
        engine.processHostBlock(buffer, midi);
        return buffer.getSample(0, kBlock - 1);
    };

    float gate = 0.0f;
    for (int block = 0; block <= 60; ++block)
        gate = renderOne();
    ASSERT_NEAR(gate, 1.0f, 1.0e-3f) << "precondition: sounding when we stop";
    ASSERT_EQ(trackIn->getActiveNoteCount(), 1);

    ASSERT_TRUE(engine.getTransport().stop());
    renderOne();
    EXPECT_EQ(trackIn->getActiveNoteCount(), 0) << "sliced passes must still see the stop";

    for (int block = 0; block < 40; ++block)
        gate = renderOne();
    EXPECT_NEAR(gate, 0.0f, 1.0e-3f) << "stopping mid-note must silence it with slicing on too";

    engine.releaseFromHost();
    engine.shutdown();
}
