#include "Modules/SequencerModule.h"
#include "Transport/TransportService.h"
#include <gtest/gtest.h>
#include <vector>

class SequencerModuleTest : public ::testing::Test {
protected:
    void SetUp() override {
        seq = std::make_unique<SequencerModule>();
        seq->prepareToPlay(44100.0, 512);
    }

    std::unique_ptr<SequencerModule> seq;
};

namespace {
juce::AudioParameterBool* seqBoolParam(juce::AudioProcessor& p, const juce::String& id) {
    return dynamic_cast<juce::AudioParameterBool*>(findParameterByID(&p, id));
}
juce::AudioParameterInt* seqIntParam(juce::AudioProcessor& p, const juce::String& id) {
    return dynamic_cast<juce::AudioParameterInt*>(findParameterByID(&p, id));
}

struct SeqMidiEvent {
    int block;
    int sample;
    bool isOn;
    int note;
};
} // namespace

TEST_F(SequencerModuleTest, StoppedByDefault) {
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;

    seq->processBlock(buffer, midi);
    EXPECT_TRUE(midi.isEmpty());
}

TEST_F(SequencerModuleTest, GeneratesMidiWhenRunning) {
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;

    auto* runParam = dynamic_cast<juce::AudioParameterBool*>(seq->getParameters()[1]);
    runParam->setValueNotifyingHost(1.0f);

    // Initial state: samplesUntilNextBeat = 0
    // First processBlock should trigger a note
    seq->processBlock(buffer, midi);

    EXPECT_FALSE(midi.isEmpty());

    bool foundNoteOn = false;
    for (const auto metadata : midi) {
        if (metadata.getMessage().isNoteOn()) {
            foundNoteOn = true;
            break;
        }
    }
    EXPECT_TRUE(foundNoteOn);
}

TEST_F(SequencerModuleTest, StepsAdvance) {
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;

    auto* runParam = dynamic_cast<juce::AudioParameterBool*>(seq->getParameters()[1]);
    runParam->setValueNotifyingHost(1.0f);

    int initialStep = seq->currentActiveStep;
    seq->processBlock(buffer, midi);

    // It should advance after the beat timer expires.
    // In the first block, it triggers step 0 and advances currentStep to 1.
    // currentActiveStep is updated TO currentStep when the timer expires.
    // Wait, let's check code:
    // samplesUntilNextBeat <= 0:
    //   currentActiveStep = currentStep (0)
    //   currentStep = (0 + 1) % 8 (1)

    EXPECT_EQ(seq->currentActiveStep, 0);

    // Need to process enough samples to reach next beat
    // 120 BPM = 0.5s = 22050 samples at 44.1k
    int samplesToNextBeat = 22050;
    while (samplesToNextBeat > 0) {
        juce::AudioBuffer<float> b(2, 512);
        juce::MidiBuffer m;
        seq->processBlock(b, m);
        samplesToNextBeat -= 512;
    }

    // Should have advanced
    EXPECT_EQ(seq->currentActiveStep, 1);
}

TEST_F(SequencerModuleTest, SendsFilterEnvCC) {
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;

    auto* runParam = dynamic_cast<juce::AudioParameterBool*>(seq->getParameters()[1]);
    runParam->setValueNotifyingHost(1.0f);

    seq->processBlock(buffer, midi);

    bool foundCC74 = false;
    for (const auto metadata : midi) {
        auto msg = metadata.getMessage();
        if (msg.isController() && msg.getControllerNumber() == 74) {
            foundCC74 = true;
            break;
        }
    }
    EXPECT_TRUE(foundCC74);
}

TEST_F(SequencerModuleTest, RestStepSendsNoteOff) {
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;

    auto* runParam = dynamic_cast<juce::AudioParameterBool*>(seq->getParameters()[1]);
    runParam->setValueNotifyingHost(1.0f);

    // Set step 0 pitch to 0 (rest)
    auto* step0 = dynamic_cast<juce::AudioParameterInt*>(seq->getParameters()[11 + 0]);
    *step0 = 0;

    // Trigger step 0 — should produce no NoteOn, but should handle rest path
    seq->processBlock(buffer, midi);

    bool foundNoteOn = false;
    for (const auto metadata : midi)
        if (metadata.getMessage().isNoteOn())
            foundNoteOn = true;

    EXPECT_FALSE(foundNoteOn);
}

TEST_F(SequencerModuleTest, NoteOffFiredAfterGateDuration) {
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;

    auto* runParam = dynamic_cast<juce::AudioParameterBool*>(seq->getParameters()[1]);
    runParam->setValueNotifyingHost(1.0f);

    // Trigger step 0 — sends note on
    seq->processBlock(buffer, midi);

    // Advance enough samples so the gate length expires (default 0.5, at 120BPM = ~11025 samples)
    int samplesToGateOff = 11100;
    bool foundNoteOff = false;
    while (samplesToGateOff > 0) {
        juce::AudioBuffer<float> b(2, 512);
        juce::MidiBuffer m;
        seq->processBlock(b, m);
        for (const auto meta : m)
            if (meta.getMessage().isNoteOff())
                foundNoteOff = true;
        samplesToGateOff -= 512;
    }
    EXPECT_TRUE(foundNoteOff);
}

TEST_F(SequencerModuleTest, BPMChangeAffectsTempo) {
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;

    auto* runParam = dynamic_cast<juce::AudioParameterBool*>(seq->getParameters()[1]);
    auto* bpmParam = dynamic_cast<juce::AudioParameterFloat*>(seq->getParameters()[2]);
    runParam->setValueNotifyingHost(1.0f);

    // Set fast BPM and trigger step 0
    bpmParam->setValueNotifyingHost(bpmParam->getNormalisableRange().convertTo0to1(240.0f));
    seq->processBlock(buffer, midi);

    // At 240 BPM one beat = 11025 samples. Step should advance quickly.
    int samplesToNextStep = 11100;
    while (samplesToNextStep > 0) {
        juce::AudioBuffer<float> b(2, 512);
        juce::MidiBuffer m;
        seq->processBlock(b, m);
        samplesToNextStep -= 512;
    }
    EXPECT_EQ(seq->currentActiveStep, 1);
}

TEST_F(SequencerModuleTest, NoteOffOnStop) {
    auto* runParam = dynamic_cast<juce::AudioParameterBool*>(seq->getParameters()[1]);

    // Block 1: start running — fires note-on for step 0
    runParam->setValueNotifyingHost(1.0f);
    juce::AudioBuffer<float> buf1(2, 512);
    juce::MidiBuffer midi1;
    seq->processBlock(buf1, midi1);

    // Confirm a note-on was produced
    bool gotNoteOn = false;
    int noteOnPitch = -1;
    for (const auto meta : midi1) {
        if (meta.getMessage().isNoteOn()) {
            gotNoteOn = true;
            noteOnPitch = meta.getMessage().getNoteNumber();
        }
    }
    EXPECT_TRUE(gotNoteOn);

    // Block 2: stop — must emit exactly one note-off for the active note
    runParam->setValueNotifyingHost(0.0f);
    juce::AudioBuffer<float> buf2(2, 512);
    juce::MidiBuffer midi2;
    seq->processBlock(buf2, midi2);

    int noteOffCount = 0;
    int noteOffPitch = -1;
    for (const auto meta : midi2) {
        if (meta.getMessage().isNoteOff()) {
            noteOffCount++;
            noteOffPitch = meta.getMessage().getNoteNumber();
        }
    }
    EXPECT_EQ(noteOffCount, 1);
    EXPECT_EQ(noteOffPitch, noteOnPitch);

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

TEST_F(SequencerModuleTest, StopThenRestart) {
    auto* runParam = dynamic_cast<juce::AudioParameterBool*>(seq->getParameters()[1]);

    // Start and fire one block
    runParam->setValueNotifyingHost(1.0f);
    juce::AudioBuffer<float> buf1(2, 512);
    juce::MidiBuffer midi1;
    seq->processBlock(buf1, midi1);

    // Stop — captures note-off, clears state
    runParam->setValueNotifyingHost(0.0f);
    juce::AudioBuffer<float> buf2(2, 512);
    juce::MidiBuffer midi2;
    seq->processBlock(buf2, midi2);

    // Restart — next block should fire a fresh note-on and NO spurious note-off
    runParam->setValueNotifyingHost(1.0f);
    // Reset beat timer so a note triggers immediately
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

TEST_F(SequencerModuleTest, StopDuringGap) {
    // Sequencer is never started — no active note; stopping must produce no note-offs
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
TEST_F(SequencerModuleTest, SyncOffByDefault) { EXPECT_FALSE(seqBoolParam(*seq, "syncToTransport")->get()); }

// Golden: the exact legacy (sync-off) event schedule, re-derived independently here from a careful
// reading of SequencerModule::processBlock's un-synced body (NOT by calling into it), so a change
// to that body has to reproduce the same sample-accurate math by coincidence to keep this green.
// Must pass against the path both before and after this parameter was added, since sync-off runs it completely
// unchanged.
TEST_F(SequencerModuleTest, LegacyScheduleIsByteIdenticalWithSyncOff) {
    ASSERT_FALSE(seqBoolParam(*seq, "syncToTransport")->get()) << "this test only pins the sync-OFF path";
    seqBoolParam(*seq, "run")->setValueNotifyingHost(1.0f);
    auto* bpm = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(seq.get(), "bpm"));
    bpm->setValueNotifyingHost(bpm->getNormalisableRange().convertTo0to1(120.0f)); // default, set explicitly

    constexpr int kNumBlocks = 200;
    constexpr int kBlockSize = 512;

    std::vector<SeqMidiEvent> actualNotes;
    std::vector<SeqMidiEvent> actualCC; // note field repurposed to hold the CC value

    for (int b = 0; b < kNumBlocks; ++b) {
        juce::AudioBuffer<float> buf(2, kBlockSize);
        juce::MidiBuffer midi;
        seq->processBlock(buf, midi);
        for (const auto meta : midi) {
            auto msg = meta.getMessage();
            if (msg.isNoteOn() || msg.isNoteOff())
                actualNotes.push_back({b, meta.samplePosition, msg.isNoteOn(), msg.getNoteNumber()});
            else if (msg.isController() && msg.getControllerNumber() == 74)
                actualCC.push_back({b, meta.samplePosition, false, msg.getControllerValue()});
        }
    }

    // Reference re-derivation of SequencerModule::processBlock's sync-off body.
    const int pitchDefaults[8] = {53, 65, 54, 61, 53, 57, 54, 60};
    const float filterEnvDefault = 0.5f;
    const double samplesPerBeat = (60.0 / 120.0) * 44100.0; // 22050.0
    const int noteDuration = (int)(samplesPerBeat * 0.5);   // gate default 0.5 -> 11025
    const int ccValue = (int)(filterEnvDefault * 127.0f);   // 63

    std::vector<SeqMidiEvent> expectedNotes;
    std::vector<SeqMidiEvent> expectedCC;
    int currentStep = 0;
    int lastNote = -1;
    int samplesUntilNextBeat = 0;
    int samplesUntilNoteOff = 0;

    for (int b = 0; b < kNumBlocks; ++b) {
        samplesUntilNextBeat -= kBlockSize;
        if (samplesUntilNextBeat <= 0) {
            const int noteVal = pitchDefaults[currentStep];

            if (lastNote > 0) {
                expectedNotes.push_back({b, 0, false, lastNote});
                lastNote = -1;
            }
            samplesUntilNoteOff = 0;

            if (noteVal > 0) {
                expectedCC.push_back({b, 0, false, ccValue});
                const int noteOnOffset = std::min(1, kBlockSize - 1);
                expectedNotes.push_back({b, noteOnOffset, true, noteVal});
                lastNote = noteVal;
                samplesUntilNoteOff = noteDuration;
            } else {
                lastNote = -1;
                samplesUntilNoteOff = 0;
            }

            currentStep = (currentStep + 1) % 8;
            samplesUntilNextBeat += (int)samplesPerBeat;
        }

        if (lastNote > 0 && samplesUntilNoteOff > 0) {
            samplesUntilNoteOff -= kBlockSize;
            if (samplesUntilNoteOff <= 0) {
                expectedNotes.push_back({b, 0, false, lastNote});
                lastNote = -1;
            }
        }
    }

    ASSERT_EQ(actualNotes.size(), expectedNotes.size());
    for (size_t i = 0; i < actualNotes.size(); ++i) {
        EXPECT_EQ(actualNotes[i].block, expectedNotes[i].block) << "note event " << i;
        EXPECT_EQ(actualNotes[i].sample, expectedNotes[i].sample) << "note event " << i;
        EXPECT_EQ(actualNotes[i].isOn, expectedNotes[i].isOn) << "note event " << i;
        EXPECT_EQ(actualNotes[i].note, expectedNotes[i].note) << "note event " << i;
    }

    ASSERT_EQ(actualCC.size(), expectedCC.size());
    for (size_t i = 0; i < actualCC.size(); ++i) {
        EXPECT_EQ(actualCC[i].block, expectedCC[i].block) << "CC event " << i;
        EXPECT_EQ(actualCC[i].sample, expectedCC[i].sample) << "CC event " << i;
        EXPECT_EQ(actualCC[i].note, expectedCC[i].note) << "CC event " << i; // repurposed as CC value
    }
}

// Asserts the timeline integration (the Synced* dispatch); compiled out with the flag so
// the flag-OFF CI job stays green. The legacy (sync-off) golden tests above stay always-on — they
// are the whole point of the OFF state.

TEST_F(SequencerModuleTest, SyncedStepsFollowTransportBeats) {
    synth::TransportService transport;
    transport.prepare(48000.0, 512);
    seq->prepareToPlay(48000.0, 512);
    seq->setPlayHead(&transport);

    seqBoolParam(*seq, "run")->setValueNotifyingHost(1.0f);
    seqBoolParam(*seq, "syncToTransport")->setValueNotifyingHost(1.0f);
    transport.play();

    std::vector<SeqMidiEvent> events;
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

    const int step0Note = seqIntParam(*seq, "Pitch 1")->get();
    const int step1Note = seqIntParam(*seq, "Pitch 2")->get();

    // Beat 0 fires at absolute sample 0 (block 0, offset 0).
    bool foundBeat0 = false, foundBeat1 = false;
    for (const auto& e : events) {
        if (!e.isOn)
            continue;
        const juce::int64 absoluteSample = (juce::int64)e.block * kBlockSize + e.sample;
        if (e.note == step0Note && absoluteSample == 0) {
            foundBeat0 = true;
        } else if (e.note == step1Note && absoluteSample == 24000) {
            foundBeat1 = true;
        }
    }
    EXPECT_TRUE(foundBeat0) << "step 0 (beat 0) should fire at absolute sample 0";
    EXPECT_TRUE(foundBeat1) << "step 1 (beat 1) should fire at absolute sample 24000 (120 BPM @ 48kHz)";
}

TEST_F(SequencerModuleTest, SyncedIgnoresBpmParam) {
    synth::TransportService transport;
    transport.prepare(48000.0, 512);
    seq->prepareToPlay(48000.0, 512);
    seq->setPlayHead(&transport);

    seqBoolParam(*seq, "run")->setValueNotifyingHost(1.0f);
    seqBoolParam(*seq, "syncToTransport")->setValueNotifyingHost(1.0f);
    // The module's own bpm param says 240; the transport (default) says 120. Synced mode must use
    // the transport's tempo and ignore this entirely.
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
    // 120 BPM @ 48kHz -> 24000 samples/beat. If the module mistakenly read the 240 BPM param,
    // this would be 12000.
    EXPECT_EQ(noteOnSamples[1] - noteOnSamples[0], 24000);
}

TEST_F(SequencerModuleTest, SyncedLocateJumpsPattern) {
    synth::TransportService transport;
    transport.prepare(48000.0, 512);
    seq->prepareToPlay(48000.0, 512);
    seq->setPlayHead(&transport);

    seqBoolParam(*seq, "run")->setValueNotifyingHost(1.0f);
    seqBoolParam(*seq, "syncToTransport")->setValueNotifyingHost(1.0f);
    transport.play();
    transport.locateBeat(5.5); // both commands apply at sample 0 of the first tick below

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
    const int step6Note = seqIntParam(*seq, "Pitch 7")->get(); // step index 6 == beat % 8 for beat 6
    EXPECT_EQ(firedNote, step6Note) << "beat 5 must NOT fire (locate landed past it); beat 6 is next";
    // Beat 6 is 0.5 beat (12000 samples @ 24000 samples/beat) after the locate point of beat 5.5.
    EXPECT_EQ(firedElapsedSample, (juce::int64)12000);
}

TEST_F(SequencerModuleTest, SyncedStopSendsNoteOffOnce) {
    synth::TransportService transport;
    transport.prepare(48000.0, 512);
    seq->prepareToPlay(48000.0, 512);
    seq->setPlayHead(&transport);

    seqBoolParam(*seq, "run")->setValueNotifyingHost(1.0f);
    seqBoolParam(*seq, "syncToTransport")->setValueNotifyingHost(1.0f);
    transport.play();

    constexpr int kBlockSize = 512;
    // Block 0: fires beat 0's note-on.
    transport.tick(kBlockSize);
    juce::AudioBuffer<float> buf0(2, kBlockSize);
    juce::MidiBuffer midi0;
    seq->processBlock(buf0, midi0);

    int heldNote = -1;
    for (const auto meta : midi0)
        if (meta.getMessage().isNoteOn())
            heldNote = meta.getMessage().getNoteNumber();
    ASSERT_GE(heldNote, 0);

    // Stop, then process the first stopped block: exactly one note-off for the held note.
    transport.stop();
    transport.tick(kBlockSize);
    juce::AudioBuffer<float> buf1(2, kBlockSize);
    juce::MidiBuffer midi1;
    seq->processBlock(buf1, midi1);

    int noteOffCount = 0;
    int noteOffPitch = -1;
    for (const auto meta : midi1) {
        if (meta.getMessage().isNoteOff()) {
            noteOffCount++;
            noteOffPitch = meta.getMessage().getNoteNumber();
        }
    }
    EXPECT_EQ(noteOffCount, 1);
    EXPECT_EQ(noteOffPitch, heldNote);

    // Still stopped: silence, no stuck note, no further note-offs.
    for (int b = 0; b < 5; ++b) {
        transport.tick(kBlockSize);
        juce::AudioBuffer<float> buf(2, kBlockSize);
        juce::MidiBuffer midi;
        seq->processBlock(buf, midi);
        EXPECT_TRUE(midi.isEmpty()) << "block " << b << " after stop should be silent";
    }
}

TEST_F(SequencerModuleTest, SyncedLoopWrapFiresLoopStartStep) {
    synth::TransportService transport;
    transport.prepare(48000.0, 512);
    seq->prepareToPlay(48000.0, 512);
    seq->setPlayHead(&transport);

    seqBoolParam(*seq, "run")->setValueNotifyingHost(1.0f);
    seqBoolParam(*seq, "syncToTransport")->setValueNotifyingHost(1.0f);
    transport.setLoop(0.0, 2.0, true); // 2-beat loop == 48000 samples at 120 BPM / 48kHz
    transport.play();

    struct WrapBlock {
        int block;
        int loopWrapSample;
    };
    std::vector<WrapBlock> wraps;
    std::vector<SeqMidiEvent> events;

    constexpr int kBlockSize = 512;
    constexpr int kTotalBlocks = 300; // > 3 loop iterations (93.75 blocks/loop)
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

    const int step0Note = seqIntParam(*seq, "Pitch 1")->get();
    const int step1Note = seqIntParam(*seq, "Pitch 2")->get();

    // The pattern alternates step0, step1, step0, step1, ... every loop pass — no double-fire,
    // no skipped beat, across several iterations.
    std::vector<int> noteOnSequence;
    for (const auto& e : events)
        noteOnSequence.push_back(e.note);

    ASSERT_GE(noteOnSequence.size(), 4u);
    for (size_t i = 0; i < noteOnSequence.size(); ++i) {
        const int expected = (i % 2 == 0) ? step0Note : step1Note;
        EXPECT_EQ(noteOnSequence[i], expected) << "note-on #" << i << " broke the loop's step order";
    }

    // Every wrap block fires step 0's note-on at exactly loopWrapSample (beat 0 == loop start, so
    // the crossing offset within the wrapped range is 0).
    for (const auto& wrap : wraps) {
        bool found = false;
        for (const auto& e : events) {
            if (e.block == wrap.block && e.note == step0Note && e.sample == wrap.loopWrapSample) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "block " << wrap.block << " wraps at sample " << wrap.loopWrapSample
                           << " but step 0 did not fire there";
    }
}
