// WavetableOscillatorModuleVoiceTests.cpp
// Per-voice retrigger phase, unison stacking/spread/blend, sub-oscillator, and sync modes.

#include "WavetableOscillatorModuleTestFixture.h"

TEST_F(WavetableOscillatorModuleTest, RetriggerPhaseStartsTheWaveWhereAsked) {
    setFloat(*module, "position", 0.0f); // sine: phase is directly readable from sample 0

    // Phase 0 starts at the sine's zero crossing, rising.
    setFloat(*module, "phase", 0.0f);
    module->prepareToPlay(kSampleRate, kBlockSize);
    EXPECT_NEAR(renderAttack(*module, 60).getReadPointer(0)[0], 0.0f, 0.02f);

    // A quarter turn starts at the positive peak instead.
    setFloat(*module, "phase", 90.0f);
    module->prepareToPlay(kSampleRate, kBlockSize);
    EXPECT_GT(renderAttack(*module, 60).getReadPointer(0)[0], 0.8f);

    // Three quarters starts at the negative peak.
    setFloat(*module, "phase", 270.0f);
    module->prepareToPlay(kSampleRate, kBlockSize);
    EXPECT_LT(renderAttack(*module, 60).getReadPointer(0)[0], -0.8f);
}

TEST_F(WavetableOscillatorModuleTest, RandomPhaseDecorrelatesRepeatedNotes) {
    setFloat(*module, "position", 0.0f);
    setFloat(*module, "randomPhase", 1.0f);
    module->prepareToPlay(kSampleRate, kBlockSize);

    // Without randomisation every note-on would start at exactly the same sample value.
    std::vector<float> firstSamples;
    for (int n = 0; n < 6; ++n)
        firstSamples.push_back(renderAttack(*module, 60).getReadPointer(0)[0]);

    float spread = 0.0f;
    for (float v : firstSamples)
        spread = std::max(spread, std::abs(v - firstSamples[0]));

    EXPECT_GT(spread, 0.1f) << "random phase must vary the attack between notes";
}

TEST_F(WavetableOscillatorModuleTest, UnisonSpreadDecorrelatesTheStack) {
    // Eight phase-correlated sines at the same pitch sum to 8x one sine; spreading their start
    // phases around the cycle makes them cancel instead. Detune stays at 0 so the ONLY
    // difference between the two renders is the start phase.
    setFloat(*module, "position", 0.0f);
    setInt(*module, "unison", 8);
    setFloat(*module, "detune", 0.0f);
    setFloat(*module, "spread", 0.0f);
    module->prepareToPlay(kSampleRate, kBlockSize);
    const float correlated = rms(renderNote(*module, 60, 2), 0);

    setFloat(*module, "spread", 1.0f);
    module->prepareToPlay(kSampleRate, kBlockSize);
    const float spread = rms(renderNote(*module, 60, 2), 0);

    EXPECT_LT(spread, correlated * 0.5f) << "spreading unison start phases must break the pile-up";
}

TEST_F(WavetableOscillatorModuleTest, SubOscillatorAddsASubOctavePartial) {
    setFloat(*module, "position", 0.0f);
    setFloat(*module, "subLevel", 0.0f);
    module->prepareToPlay(kSampleRate, kBlockSize);
    const auto dry = renderNote(*module, 69, 4); // A4 = 440 Hz
    const float drySub = magnitudeAt(dry, 0, 220.0f, kSampleRate);

    setFloat(*module, "subLevel", 1.0f);
    module->prepareToPlay(kSampleRate, kBlockSize);
    const auto wet = renderNote(*module, 69, 4);
    EXPECT_GT(magnitudeAt(wet, 0, 220.0f, kSampleRate), drySub + 0.2f) << "sub must appear an octave down";

    // Two octaves down puts it at 110 Hz instead.
    setChoice(*module, "subOctave", 1);
    module->prepareToPlay(kSampleRate, kBlockSize);
    const auto twoDown = renderNote(*module, 69, 4);
    EXPECT_GT(magnitudeAt(twoDown, 0, 110.0f, kSampleRate), 0.2f);
}

TEST_F(WavetableOscillatorModuleTest, StackModesTransposeUnisonVoices) {
    setFloat(*module, "position", 0.0f);
    setInt(*module, "unison", 4);
    setFloat(*module, "detune", 0.0f);
    setChoice(*module, "stack", (int)WavetableOscillatorModule::Stack::PowerChord);
    module->prepareToPlay(kSampleRate, kBlockSize);

    const auto out = renderNote(*module, 69, 4); // A4 = 440
    // Power chord: root, fifth (+7 = 659.3), octave (+12 = 880).
    EXPECT_GT(magnitudeAt(out, 0, 440.0f, kSampleRate), 0.05f) << "root missing";
    EXPECT_GT(magnitudeAt(out, 0, 659.3f, kSampleRate), 0.05f) << "fifth missing";
    EXPECT_GT(magnitudeAt(out, 0, 880.0f, kSampleRate), 0.05f) << "octave missing";
}

TEST_F(WavetableOscillatorModuleTest, BlendFadesTheStackAgainstTheCentreVoice) {
    setInt(*module, "unison", 8);
    setFloat(*module, "detune", 40.0f);

    setFloat(*module, "blend", 1.0f);
    module->prepareToPlay(kSampleRate, kBlockSize);
    const auto full = renderNote(*module, 60, 2);

    setFloat(*module, "blend", 0.0f);
    module->prepareToPlay(kSampleRate, kBlockSize);
    const auto centreOnly = renderNote(*module, 60, 2);

    // With the stack muted only the centre voice remains, so the two renders differ.
    float diff = 0.0f;
    for (int i = 0; i < full.getNumSamples(); ++i)
        diff = std::max(diff, std::abs(full.getReadPointer(0)[i] - centreOnly.getReadPointer(0)[i]));
    EXPECT_GT(diff, 0.02f);
    EXPECT_GT(rms(centreOnly, 0), 0.05f) << "the centre voice must survive blend 0";
}

TEST_F(WavetableOscillatorModuleTest, RingModMultipliesBySyncInput) {
    using WT = WavetableOscillatorModule;
    setFloat(*module, "position", 0.0f);
    setChoice(*module, "syncMode", (int)WT::SyncMode::RingMod);
    module->prepareToPlay(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> block(kCh, kBlockSize);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 69, 1.0f), 0);
    block.clear();
    module->processBlock(block, midi);

    // Ring-modulating a 440 Hz carrier with a 110 Hz sine gives sidebands at 330 and 550,
    // and suppresses the carrier itself.
    juce::AudioBuffer<float> out(kCh, 8 * kBlockSize);
    out.clear();
    double phase = 0.0;
    for (int b = 0; b < 8; ++b) {
        block.clear();
        float* sync = block.getWritePointer(WT::kJackSync);
        for (int i = 0; i < kBlockSize; ++i) {
            sync[i] = (float)std::sin(phase);
            phase += 2.0 * juce::MathConstants<double>::pi * 110.0 / kSampleRate;
        }
        juce::MidiBuffer empty;
        module->processBlock(block, empty);
        for (int ch = 0; ch < kCh; ++ch)
            out.copyFrom(ch, b * kBlockSize, block, ch, 0, kBlockSize);
    }

    const float carrier = magnitudeAt(out, 0, 440.0f, kSampleRate);
    const float lower = magnitudeAt(out, 0, 330.0f, kSampleRate);
    const float upper = magnitudeAt(out, 0, 550.0f, kSampleRate);
    EXPECT_GT(lower, 0.1f) << "lower sideband missing";
    EXPECT_GT(upper, 0.1f) << "upper sideband missing";
    EXPECT_LT(carrier, lower * 0.5f) << "ring mod must suppress the carrier";
}

TEST_F(WavetableOscillatorModuleTest, HardSyncResetsThePhaseOnTheMasterEdge) {
    using WT = WavetableOscillatorModule;
    setFloat(*module, "position", 0.0f);
    setChoice(*module, "syncMode", (int)WT::SyncMode::HardSync);
    // The slave must sit at a NON-integer multiple of the master: at an exact multiple it
    // completes a whole number of cycles per master period and the reset is a no-op.
    setInt(*module, "octave", 2);
    setInt(*module, "coarse", 5); // 110 Hz * 4 * 2^(5/12) ~= 587 Hz, ratio 5.34
    module->prepareToPlay(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> block(kCh, kBlockSize);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 57, 1.0f), 0); // A2 = 110 Hz -> slave at 440
    block.clear();
    module->processBlock(block, midi);

    juce::AudioBuffer<float> out(kCh, 8 * kBlockSize);
    out.clear();
    double phase = 0.0;
    for (int b = 0; b < 8; ++b) {
        block.clear();
        float* sync = block.getWritePointer(WT::kJackSync);
        for (int i = 0; i < kBlockSize; ++i) {
            sync[i] = (float)std::sin(phase);
            phase += 2.0 * juce::MathConstants<double>::pi * 110.0 / kSampleRate;
        }
        juce::MidiBuffer empty;
        module->processBlock(block, empty);
        for (int ch = 0; ch < kCh; ++ch)
            out.copyFrom(ch, b * kBlockSize, block, ch, 0, kBlockSize);
    }

    // Retriggering a sine at 110 Hz imposes that period on the output, so energy appears at the
    // master's fundamental where a free-running 440 Hz sine would have none.
    EXPECT_GT(magnitudeAt(out, 0, 110.0f, kSampleRate), 0.02f) << "hard sync must imprint the master period";
}
