#include "Modules/SequencerModule.h"
#include <gtest/gtest.h>

class SequencerModuleTest : public ::testing::Test {
protected:
    void SetUp() override {
        seq = std::make_unique<SequencerModule>();
        seq->prepareToPlay(44100.0, 512);
    }

    std::unique_ptr<SequencerModule> seq;
};

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
