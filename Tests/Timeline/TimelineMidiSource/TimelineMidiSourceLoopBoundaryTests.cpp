// Loop-wrap guard for the Track In module: every combination of block size, loop length and note end
// (before, exactly at, and past the loop end) must release the note once per pass and never stack a
// note-on on a held one. A loop whose length is a whole number of blocks puts the wrap exactly on a
// block boundary, which the transport reports differently from a wrap inside a block.
//
// 48 kHz / 120 BPM: a 4 beat loop is 96000 samples (187.5 blocks of 512, 200 of 480, 375 of 256, 217.7
// of 441); an 8 beat loop is 192000 samples (375 blocks of 512, so aligned for every size but 441).

#include "TimelineMidiSourceTestHelpers.h"
#include <bit>

namespace {

constexpr int kPasses = 6;
constexpr int kHeldPitch = 60;
constexpr int kWitnessPitch = 62;

enum class NoteEnd { BeforeLoopEnd, ExactlyAtLoopEnd, PastLoopEnd };

double endBeatFor(NoteEnd end, double loopBeats) {
    switch (end) {
    case NoteEnd::BeforeLoopEnd:
        return loopBeats - 0.01;
    case NoteEnd::ExactlyAtLoopEnd:
        return loopBeats;
    case NoteEnd::PastLoopEnd:
        return loopBeats + 1.0;
    }
    return loopBeats;
}

const char* nameOf(NoteEnd end) {
    switch (end) {
    case NoteEnd::BeforeLoopEnd:
        return "before the loop end";
    case NoteEnd::ExactlyAtLoopEnd:
        return "exactly at the loop end";
    case NoteEnd::PastLoopEnd:
        return "past the loop end";
    }
    return "";
}

} // namespace

TEST(TimelineMidiSourceTest, LoopBoundaryReleasesEveryNoteOnEveryPassForEveryBlockSize) {
    for (const double loopBeats : {4.0, 8.0}) {
        for (const int blockSize : {512, 480, 256, 441}) {
            for (const NoteEnd end : {NoteEnd::BeforeLoopEnd, NoteEnd::ExactlyAtLoopEnd, NoteEnd::PastLoopEnd}) {
                const juce::String where = "loop " + juce::String(loopBeats) + " beats, block " +
                                           juce::String(blockSize) + ", note ends " + nameOf(end);
                SCOPED_TRACE(where.toRawUTF8());

                Doc doc;
                ASSERT_TRUE(doc.doc.addNote(doc.clipId, makeNote(0.0, kWitnessPitch, 0.25)).isValid());
                ASSERT_TRUE(
                    doc.doc.addNote(doc.clipId, makeNote(1.0, kHeldPitch, endBeatFor(end, loopBeats) - 1.0)).isValid());
                auto snapshot = doc.snapshot();

                Harness h;
                ASSERT_TRUE(h.transport.setLoop(0.0, loopBeats, true));
                ASSERT_TRUE(h.transport.play());

                const std::int64_t loopSamples = (std::int64_t)(loopBeats * kSamplesPerBeat);
                HeldKeys model;
                std::int64_t previousStart = -1;
                int pass = -1;
                int onsHeld = 0, offsHeld = 0;
                int probedPasses = 0;

                while (pass < kPasses) {
                    const auto events = h.renderBlock(snapshot.get(), blockSize);
                    const auto start = h.blockStartSample();
                    const bool newPass = start < previousStart || pass < 0;
                    previousStart = start;
                    if (newPass)
                        ++pass;
                    if (pass >= kPasses)
                        break;

                    for (const auto& e : events) {
                        model.apply(e, "pass " + juce::String(pass));
                        if (e.pitch == kHeldPitch)
                            (e.isNoteOn ? onsHeld : offsHeld) += 1;
                    }

                    // Probe once per pass at beat ~0.5: the previous pass is over, the held note has not
                    // started yet, so nothing may be sounding and every note-on has had its note-off.
                    const double beat = (double)start / kSamplesPerBeat;
                    if (beat >= 0.5 && beat < 0.5 + (double)blockSize / kSamplesPerBeat && probedPasses == pass) {
                        EXPECT_EQ(model.numDown(), 0) << "pass " << pass << ": a note is still held at beat 0.5";
                        EXPECT_EQ(onsHeld, pass) << "pass " << pass;
                        EXPECT_EQ(offsHeld, pass) << "pass " << pass << ": note-ons and note-offs differ";
                        EXPECT_EQ(h.module.getActiveNoteCount(), 0) << "pass " << pass;
                        ++probedPasses;
                    }
                    ASSERT_LT(start, loopSamples + blockSize);
                }

                EXPECT_EQ(probedPasses, kPasses) << "every pass must have been probed";
                EXPECT_TRUE(model.firstFailure.isEmpty()) << model.firstFailure;
            }
        }
    }
}

// A poly voice stage fed by the same track: the voices sounding in the quiet part of each pass (before
// the chord starts) and in the held part must be identical on every pass.
TEST(TimelineMidiSourceTest, LoopPassesKeepTheSameVoiceCountAtEveryBlockSize) {
    for (const int blockSize : {512, 480, 256, 441}) {
        SCOPED_TRACE(blockSize);

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
        ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(juce::JSON::parse(patchJson), engine.getGraph(),
                                                           /*clearExisting=*/true, /*trusted=*/true));
        PolyMidiModule* poly = nullptr;
        for (auto* node : engine.getGraph().getNodes())
            if (auto* p = dynamic_cast<PolyMidiModule*>(node->getProcessor()))
                poly = p;
        ASSERT_NE(poly, nullptr);
        engine.prepareForHost(kSampleRate, kBlock, 0, 2);

        // A three note chord from beat 0.5 held to the end of a 4 beat loop (the loop end).
        Doc doc;
        for (const int pitch : {60, 64, 67})
            ASSERT_TRUE(doc.doc.addNote(doc.clipId, makeNote(0.5, pitch, 3.5)).isValid());
        engine.getTimelineSnapshots().publish(doc.snapshot());
        ASSERT_TRUE(engine.getTransport().setLoop(0.0, 4.0, true));
        ASSERT_TRUE(engine.getTransport().play());

        std::vector<int> quiet, held;
        int previousStart = -1;
        int pass = -1;
        std::int64_t position = 0;
        for (int block = 0; block < 6 * 96000 / blockSize + 20 && pass < kPasses; ++block) {
            juce::AudioBuffer<float> buffer(2, blockSize);
            buffer.clear();
            juce::MidiBuffer midi;
            engine.processHostBlock(buffer, midi);

            const auto start = engine.getTransport().getCurrentBlockInfo().blockStartSample;
            position = start;
            if (start < previousStart || pass < 0)
                ++pass;
            previousStart = (int)start;
            const double beat = (double)position / kSamplesPerBeat;
            const int voices = std::popcount((unsigned)poly->getActiveVoiceMask());
            if (beat >= 0.2 && beat < 0.2 + (double)blockSize / kSamplesPerBeat)
                quiet.push_back(voices);
            if (beat >= 2.0 && beat < 2.0 + (double)blockSize / kSamplesPerBeat)
                held.push_back(voices);
        }

        ASSERT_GE(quiet.size(), (std::size_t)kPasses);
        ASSERT_GE(held.size(), (std::size_t)kPasses);
        for (std::size_t i = 0; i < quiet.size(); ++i)
            EXPECT_EQ(quiet[i], 0) << "pass " << i << ": voices still sounding before the chord starts";
        for (std::size_t i = 0; i < held.size(); ++i)
            EXPECT_EQ(held[i], 3) << "pass " << i << ": the chord must sound with exactly three voices";

        engine.releaseFromHost();
        engine.shutdown();
    }
}
