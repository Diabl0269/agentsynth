#include "Modules/PolySequencerModule.h"
#include "Transport/TransportService.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <vector>

class PolySequencerModuleTest : public ::testing::Test {
protected:
    void SetUp() override {
        seq = std::make_unique<PolySequencerModule>();
        seq->prepareToPlay(44100.0, 512);
    }

    std::unique_ptr<PolySequencerModule> seq;
};

namespace {
juce::AudioParameterBool* polySeqBoolParam(juce::AudioProcessor& p, const juce::String& id) {
    return dynamic_cast<juce::AudioParameterBool*>(findParameterByID(&p, id));
}
juce::AudioParameterInt* polySeqIntParam(juce::AudioProcessor& p, const juce::String& id) {
    return dynamic_cast<juce::AudioParameterInt*>(findParameterByID(&p, id));
}

struct PolySeqMidiEvent {
    int block;
    int sample;
    bool isOn;
    int note;
};
} // namespace

TEST_F(PolySequencerModuleTest, ProducesMidi) { EXPECT_TRUE(seq->producesMidi()); }

TEST_F(PolySequencerModuleTest, StoppedByDefault) {
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    seq->processBlock(buffer, midi);
    EXPECT_TRUE(midi.isEmpty());
}

TEST_F(PolySequencerModuleTest, GeneratesChordWhenRunning) {
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;

    // Run
    auto* runParam = dynamic_cast<juce::AudioParameterBool*>(seq->getParameters()[1]);
    runParam->setValueNotifyingHost(1.0f);

    // Set Minor chord for step 0
    auto* chord0 = dynamic_cast<juce::AudioParameterChoice*>(seq->getParameters()[12 + 0]); // Step 1 Chord
    *chord0 = 2;                                                                            // Minor

    seq->processBlock(buffer, midi);

    // Minor chord should have 3 notes (root, root+3, root+7)
    int noteCount = 0;
    for (const auto meta : midi) {
        if (meta.getMessage().isNoteOn())
            noteCount++;
    }
    EXPECT_EQ(noteCount, 3);
}

TEST_F(PolySequencerModuleTest, StepsAdvance) {
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;

    auto* runParam = dynamic_cast<juce::AudioParameterBool*>(seq->getParameters()[1]);
    runParam->setValueNotifyingHost(1.0f);

    int initialStep = seq->currentActiveStep;
    seq->processBlock(buffer, midi);

    // Beat 120BPM = 0.5s = 22050 samples
    int samples = 22050 + 512;
    while (samples > 0) {
        juce::AudioBuffer<float> b(2, 512);
        juce::MidiBuffer m;
        seq->processBlock(b, m);
        samples -= 512;
    }

    EXPECT_EQ(seq->currentActiveStep, 1);
}

TEST_F(PolySequencerModuleTest, BPMChange) {
    auto* runParam = dynamic_cast<juce::AudioParameterBool*>(seq->getParameters()[1]);
    auto* bpmParam = dynamic_cast<juce::AudioParameterFloat*>(seq->getParameters()[2]);
    runParam->setValueNotifyingHost(1.0f);

    // Fast BPM
    bpmParam->setValueNotifyingHost(bpmParam->getNormalisableRange().convertTo0to1(300.0f));

    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    seq->processBlock(buffer, midi);

    // 300 BPM = 5 beats per sec = 0.2s per beat = 8820 samples
    int samples = 9000;
    while (samples > 0) {
        juce::AudioBuffer<float> b(2, 512);
        juce::MidiBuffer m;
        seq->processBlock(b, m);
        samples -= 512;
    }

    EXPECT_EQ(seq->currentActiveStep, 1);
}

TEST_F(PolySequencerModuleTest, AllChordTypes) {
    auto* runParam = dynamic_cast<juce::AudioParameterBool*>(seq->getParameters()[1]);
    runParam->setValueNotifyingHost(1.0f);

    for (int i = 0; i < 8; ++i) {
        auto* chord0 = dynamic_cast<juce::AudioParameterChoice*>(seq->getParameters()[12]);
        *chord0 = i;

        juce::AudioBuffer<float> buffer(2, 512);
        juce::MidiBuffer midi;
        seq->processBlock(buffer, midi);

        // Just verify it doesn't crash and generates at least 1 note
        int noteCount = 0;
        for (const auto meta : midi)
            if (meta.getMessage().isNoteOn())
                noteCount++;
        EXPECT_GE(noteCount, 1);

        // Prepare for next beat trigger
        seq->prepareToPlay(44100.0, 512);
    }
}

TEST_F(PolySequencerModuleTest, NoteOffOnStop) {
    auto* runParam = dynamic_cast<juce::AudioParameterBool*>(seq->getParameters()[1]);

    // Use Minor chord so we get 3 notes (root, root+3, root+7)
    auto* chord0 = dynamic_cast<juce::AudioParameterChoice*>(seq->getParameters()[12]);
    *chord0 = 2; // Minor

    // Block 1: start running — fires note-ons
    runParam->setValueNotifyingHost(1.0f);
    juce::AudioBuffer<float> buf1(2, 512);
    juce::MidiBuffer midi1;
    seq->processBlock(buf1, midi1);

    int noteOnCount = 0;
    std::vector<int> noteOnPitches;
    for (const auto meta : midi1) {
        if (meta.getMessage().isNoteOn()) {
            noteOnCount++;
            noteOnPitches.push_back(meta.getMessage().getNoteNumber());
        }
    }
    EXPECT_EQ(noteOnCount, 3);

    // Block 2: stop — must emit one note-off per active note
    runParam->setValueNotifyingHost(0.0f);
    juce::AudioBuffer<float> buf2(2, 512);
    juce::MidiBuffer midi2;
    seq->processBlock(buf2, midi2);

    int noteOffCount = 0;
    std::vector<int> noteOffPitches;
    for (const auto meta : midi2) {
        if (meta.getMessage().isNoteOff()) {
            noteOffCount++;
            noteOffPitches.push_back(meta.getMessage().getNoteNumber());
        }
    }
    EXPECT_EQ(noteOffCount, noteOnCount);
    // Each note-on pitch should have a corresponding note-off
    for (int pitch : noteOnPitches) {
        bool found = std::find(noteOffPitches.begin(), noteOffPitches.end(), pitch) != noteOffPitches.end();
        EXPECT_TRUE(found) << "Missing note-off for pitch " << pitch;
    }

    // Block 3: still stopped — must produce NO further note-offs
    juce::AudioBuffer<float> buf3(2, 512);
    juce::MidiBuffer midi3;
    seq->processBlock(buf3, midi3);

    int extraNoteOffs = 0;
    for (const auto meta : midi3)
        if (meta.getMessage().isNoteOff())
            extraNoteOffs++;
    EXPECT_EQ(extraNoteOffs, 0);
}

TEST_F(PolySequencerModuleTest, StopThenRestart) {
    auto* runParam = dynamic_cast<juce::AudioParameterBool*>(seq->getParameters()[1]);

    // Start and fire one block
    runParam->setValueNotifyingHost(1.0f);
    juce::AudioBuffer<float> buf1(2, 512);
    juce::MidiBuffer midi1;
    seq->processBlock(buf1, midi1);

    // Stop — captures note-offs, clears state
    runParam->setValueNotifyingHost(0.0f);
    juce::AudioBuffer<float> buf2(2, 512);
    juce::MidiBuffer midi2;
    seq->processBlock(buf2, midi2);

    // Restart — prepareToPlay resets beat timer; next block fires fresh note-ons, no spurious note-offs
    seq->prepareToPlay(44100.0, 512);
    runParam->setValueNotifyingHost(1.0f);

    juce::AudioBuffer<float> buf3(2, 512);
    juce::MidiBuffer midi3;
    seq->processBlock(buf3, midi3);

    bool gotNoteOn = false;
    int spuriousNoteOffs = 0;
    for (const auto meta : midi3) {
        if (meta.getMessage().isNoteOn())
            gotNoteOn = true;
        if (meta.getMessage().isNoteOff())
            spuriousNoteOffs++;
    }
    EXPECT_TRUE(gotNoteOn);
    EXPECT_EQ(spuriousNoteOffs, 0);
}

TEST_F(PolySequencerModuleTest, StopDuringGap) {
    // Sequencer never started — no active notes; stopping must produce no note-offs
    auto* runParam = dynamic_cast<juce::AudioParameterBool*>(seq->getParameters()[1]);
    runParam->setValueNotifyingHost(0.0f);

    juce::AudioBuffer<float> buf(2, 512);
    juce::MidiBuffer midi;
    seq->processBlock(buf, midi);

    int noteOffCount = 0;
    for (const auto meta : midi)
        if (meta.getMessage().isNoteOff())
            noteOffCount++;
    EXPECT_EQ(noteOffCount, 0);
}

// syncToTransport defaults to false, so every preset saved before this parameter existed
// loads with the sync path off (the legacy free-running clock).
TEST_F(PolySequencerModuleTest, SyncOffByDefault) { EXPECT_FALSE(polySeqBoolParam(*seq, "syncToTransport")->get()); }

// Golden: the exact legacy (sync-off) event schedule, re-derived independently here from a careful
// reading of PolySequencerModule::processBlock's un-synced body (NOT by calling into it), so a
// change to that body has to reproduce the same sample-accurate math by coincidence to keep this
// green. Must pass both before and after this parameter was added, since sync-off runs it completely unchanged.
// Default chord type is "Unison" (index 0) for every step, so each step plays exactly one note
// (its root) — no randomness from the "Random" chord case is exercised here.
TEST_F(PolySequencerModuleTest, LegacyScheduleIsByteIdenticalWithSyncOff) {
    ASSERT_FALSE(polySeqBoolParam(*seq, "syncToTransport")->get()) << "this test only pins the sync-OFF path";
    polySeqBoolParam(*seq, "run")->setValueNotifyingHost(1.0f);
    auto* bpm = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(seq.get(), "bpm"));
    bpm->setValueNotifyingHost(bpm->getNormalisableRange().convertTo0to1(120.0f)); // default, set explicitly

    constexpr int kNumBlocks = 200;
    constexpr int kBlockSize = 512;

    std::vector<PolySeqMidiEvent> actual;
    for (int b = 0; b < kNumBlocks; ++b) {
        juce::AudioBuffer<float> buf(2, kBlockSize);
        juce::MidiBuffer midi;
        seq->processBlock(buf, midi);
        for (const auto meta : midi) {
            auto msg = meta.getMessage();
            if (msg.isNoteOn() || msg.isNoteOff())
                actual.push_back({b, meta.samplePosition, msg.isNoteOn(), msg.getNoteNumber()});
        }
    }

    // Reference re-derivation of PolySequencerModule::processBlock's sync-off body.
    const int rootDefaults[8] = {48, 52, 55, 60, 48, 55, 52, 60};
    const double samplesPerBeat = (60.0 / 120.0) * 44100.0; // 22050.0
    const int noteDuration = (int)(samplesPerBeat * 0.5);   // gate default 0.5 -> 11025

    std::vector<PolySeqMidiEvent> expected;
    int currentStep = 0;
    std::vector<int> activeNotes;
    int samplesUntilNextBeat = 0;
    int samplesUntilNoteOff = 0;

    for (int b = 0; b < kNumBlocks; ++b) {
        samplesUntilNextBeat -= kBlockSize;
        if (samplesUntilNextBeat <= 0) {
            for (int note : activeNotes)
                expected.push_back({b, 0, false, note});
            activeNotes.clear();

            int root = rootDefaults[currentStep];
            if (root < 24)
                root = 48;
            samplesUntilNoteOff = noteDuration;

            // Chord type Unison (default): just the root.
            const int noteOnOffset = std::min(1, kBlockSize - 1);
            expected.push_back({b, noteOnOffset, true, root});
            activeNotes.push_back(root);

            currentStep = (currentStep + 1) % 8;
            samplesUntilNextBeat += (int)samplesPerBeat;
        }

        if (!activeNotes.empty() && samplesUntilNoteOff > 0) {
            samplesUntilNoteOff -= kBlockSize;
            if (samplesUntilNoteOff <= 0) {
                for (int note : activeNotes)
                    expected.push_back({b, 0, false, note});
                activeNotes.clear();
            }
        }
    }

    ASSERT_EQ(actual.size(), expected.size());
    for (size_t i = 0; i < actual.size(); ++i) {
        EXPECT_EQ(actual[i].block, expected[i].block) << "event " << i;
        EXPECT_EQ(actual[i].sample, expected[i].sample) << "event " << i;
        EXPECT_EQ(actual[i].isOn, expected[i].isOn) << "event " << i;
        EXPECT_EQ(actual[i].note, expected[i].note) << "event " << i;
    }
}

// Asserts the timeline integration (the Synced* dispatch); compiled out with the flag so
// the flag-OFF CI job stays green. The legacy (sync-off) golden tests above stay always-on — they
// are the whole point of the OFF state.

TEST_F(PolySequencerModuleTest, SyncedStepsFollowTransportBeats) {
    synth::TransportService transport;
    transport.prepare(48000.0, 512);
    seq->prepareToPlay(48000.0, 512);
    seq->setPlayHead(&transport);

    polySeqBoolParam(*seq, "run")->setValueNotifyingHost(1.0f);
    polySeqBoolParam(*seq, "syncToTransport")->setValueNotifyingHost(1.0f);
    transport.play();

    std::vector<PolySeqMidiEvent> events;
    constexpr int kBlockSize = 512;
    for (int b = 0; b < 60; ++b) {
        transport.tick(kBlockSize);
        juce::AudioBuffer<float> buf(2, kBlockSize);
        juce::MidiBuffer midi;
        seq->processBlock(buf, midi);
        for (const auto meta : midi) {
            auto msg = meta.getMessage();
            if (msg.isNoteOn() || msg.isNoteOff())
                events.push_back({b, meta.samplePosition, msg.isNoteOn(), msg.getNoteNumber()});
        }
    }

    const int step0Root = polySeqIntParam(*seq, "Step 1 Root")->get();
    const int step1Root = polySeqIntParam(*seq, "Step 2 Root")->get();

    bool foundBeat0 = false, foundBeat1 = false;
    for (const auto& e : events) {
        if (!e.isOn)
            continue;
        const juce::int64 absoluteSample = (juce::int64)e.block * kBlockSize + e.sample;
        if (e.note == step0Root && absoluteSample == 0)
            foundBeat0 = true;
        else if (e.note == step1Root && absoluteSample == 24000)
            foundBeat1 = true;
    }
    EXPECT_TRUE(foundBeat0) << "step 0 (beat 0) should fire at absolute sample 0";
    EXPECT_TRUE(foundBeat1) << "step 1 (beat 1) should fire at absolute sample 24000 (120 BPM @ 48kHz)";
}

TEST_F(PolySequencerModuleTest, SyncedIgnoresBpmParam) {
    synth::TransportService transport;
    transport.prepare(48000.0, 512);
    seq->prepareToPlay(48000.0, 512);
    seq->setPlayHead(&transport);

    polySeqBoolParam(*seq, "run")->setValueNotifyingHost(1.0f);
    polySeqBoolParam(*seq, "syncToTransport")->setValueNotifyingHost(1.0f);
    auto* bpm = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(seq.get(), "bpm"));
    bpm->setValueNotifyingHost(bpm->getNormalisableRange().convertTo0to1(240.0f));
    transport.play();

    std::vector<juce::int64> noteOnSamples;
    constexpr int kBlockSize = 512;
    for (int b = 0; b < 60; ++b) {
        transport.tick(kBlockSize);
        juce::AudioBuffer<float> buf(2, kBlockSize);
        juce::MidiBuffer midi;
        seq->processBlock(buf, midi);
        for (const auto meta : midi)
            if (meta.getMessage().isNoteOn())
                noteOnSamples.push_back((juce::int64)b * kBlockSize + meta.samplePosition);
    }

    ASSERT_GE(noteOnSamples.size(), 2u);
    EXPECT_EQ(noteOnSamples[1] - noteOnSamples[0], 24000);
}

TEST_F(PolySequencerModuleTest, SyncedLocateJumpsPattern) {
    synth::TransportService transport;
    transport.prepare(48000.0, 512);
    seq->prepareToPlay(48000.0, 512);
    seq->setPlayHead(&transport);

    polySeqBoolParam(*seq, "run")->setValueNotifyingHost(1.0f);
    polySeqBoolParam(*seq, "syncToTransport")->setValueNotifyingHost(1.0f);
    transport.play();
    transport.locateBeat(5.5);

    constexpr int kBlockSize = 512;
    int firedNote = -1;
    // Samples elapsed SINCE this loop's first tick (i.e. since the locate), not from transport
    // origin: the locate itself jumps the origin-relative position, so "elapsed since locate" is
    // what's directly comparable to a fixed number of beats-since-5.5.
    juce::int64 firedElapsedSample = -1;
    for (int b = 0; b < 60 && firedNote < 0; ++b) {
        transport.tick(kBlockSize);
        juce::AudioBuffer<float> buf(2, kBlockSize);
        juce::MidiBuffer midi;
        seq->processBlock(buf, midi);
        for (const auto meta : midi) {
            if (meta.getMessage().isNoteOn()) {
                firedNote = meta.getMessage().getNoteNumber();
                firedElapsedSample = (juce::int64)b * kBlockSize + meta.samplePosition;
                break;
            }
        }
    }

    ASSERT_GE(firedNote, 0) << "expected a note-on within 60 blocks of locating to beat 5.5";
    const int step6Root = polySeqIntParam(*seq, "Step 7 Root")->get(); // step index 6 == beat % 8 for beat 6
    EXPECT_EQ(firedNote, step6Root) << "beat 5 must NOT fire (locate landed past it); beat 6 is next";
    // Beat 6 is 0.5 beat (12000 samples @ 24000 samples/beat) after the locate point of beat 5.5.
    EXPECT_EQ(firedElapsedSample, (juce::int64)12000);
}

TEST_F(PolySequencerModuleTest, SyncedStopSendsNoteOffOnce) {
    synth::TransportService transport;
    transport.prepare(48000.0, 512);
    seq->prepareToPlay(48000.0, 512);
    seq->setPlayHead(&transport);

    // Minor chord (3 notes) so this also proves the whole chord is killed together, once.
    auto* chord0 = dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(seq.get(), "Step 1 Chord"));
    *chord0 = 2;

    polySeqBoolParam(*seq, "run")->setValueNotifyingHost(1.0f);
    polySeqBoolParam(*seq, "syncToTransport")->setValueNotifyingHost(1.0f);
    transport.play();

    constexpr int kBlockSize = 512;
    transport.tick(kBlockSize);
    juce::AudioBuffer<float> buf0(2, kBlockSize);
    juce::MidiBuffer midi0;
    seq->processBlock(buf0, midi0);

    std::vector<int> heldNotes;
    for (const auto meta : midi0)
        if (meta.getMessage().isNoteOn())
            heldNotes.push_back(meta.getMessage().getNoteNumber());
    ASSERT_EQ(heldNotes.size(), 3u);

    transport.stop();
    transport.tick(kBlockSize);
    juce::AudioBuffer<float> buf1(2, kBlockSize);
    juce::MidiBuffer midi1;
    seq->processBlock(buf1, midi1);

    std::vector<int> noteOffs;
    for (const auto meta : midi1)
        if (meta.getMessage().isNoteOff())
            noteOffs.push_back(meta.getMessage().getNoteNumber());
    EXPECT_EQ(noteOffs.size(), heldNotes.size());
    for (int note : heldNotes)
        EXPECT_NE(std::find(noteOffs.begin(), noteOffs.end(), note), noteOffs.end())
            << "missing note-off for held note " << note;

    for (int b = 0; b < 5; ++b) {
        transport.tick(kBlockSize);
        juce::AudioBuffer<float> buf(2, kBlockSize);
        juce::MidiBuffer midi;
        seq->processBlock(buf, midi);
        EXPECT_TRUE(midi.isEmpty()) << "block " << b << " after stop should be silent";
    }
}

TEST_F(PolySequencerModuleTest, SyncedLoopWrapFiresLoopStartStep) {
    synth::TransportService transport;
    transport.prepare(48000.0, 512);
    seq->prepareToPlay(48000.0, 512);
    seq->setPlayHead(&transport);

    polySeqBoolParam(*seq, "run")->setValueNotifyingHost(1.0f);
    polySeqBoolParam(*seq, "syncToTransport")->setValueNotifyingHost(1.0f);
    transport.setLoop(0.0, 2.0, true);
    transport.play();

    struct WrapBlock {
        int block;
        int loopWrapSample;
    };
    std::vector<WrapBlock> wraps;
    std::vector<PolySeqMidiEvent> events;

    constexpr int kBlockSize = 512;
    constexpr int kTotalBlocks = 300;
    for (int b = 0; b < kTotalBlocks; ++b) {
        const auto& info = transport.tick(kBlockSize);
        if (info.loopWrapSample >= 0)
            wraps.push_back({b, info.loopWrapSample});

        juce::AudioBuffer<float> buf(2, kBlockSize);
        juce::MidiBuffer midi;
        seq->processBlock(buf, midi);
        for (const auto meta : midi) {
            auto msg = meta.getMessage();
            if (msg.isNoteOn())
                events.push_back({b, meta.samplePosition, true, msg.getNoteNumber()});
        }
    }

    ASSERT_GE(wraps.size(), 2u) << "expected at least 2 loop wraps in " << kTotalBlocks << " blocks";

    const int step0Root = polySeqIntParam(*seq, "Step 1 Root")->get();
    const int step1Root = polySeqIntParam(*seq, "Step 2 Root")->get();

    std::vector<int> noteOnSequence;
    for (const auto& e : events)
        noteOnSequence.push_back(e.note);

    ASSERT_GE(noteOnSequence.size(), 4u);
    for (size_t i = 0; i < noteOnSequence.size(); ++i) {
        const int expected = (i % 2 == 0) ? step0Root : step1Root;
        EXPECT_EQ(noteOnSequence[i], expected) << "note-on #" << i << " broke the loop's step order";
    }

    for (const auto& wrap : wraps) {
        bool found = false;
        for (const auto& e : events) {
            if (e.block == wrap.block && e.note == step0Root && e.sample == wrap.loopWrapSample) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "block " << wrap.block << " wraps at sample " << wrap.loopWrapSample
                           << " but step 0 did not fire there";
    }
}
