// WavetableOscillatorModuleOutputTests.cpp
// Basic audio generation, anti-aliasing (mip selection), and the mute/bypass contract.

#include "WavetableOscillatorModuleTestFixture.h"

TEST_F(WavetableOscillatorModuleTest, ZeroChannelsDoesNotCrash) {
    juce::AudioBuffer<float> buffer(0, 0);
    juce::MidiBuffer midi;
    EXPECT_NO_THROW(module->processBlock(buffer, midi));
}

TEST_F(WavetableOscillatorModuleTest, ProducesAudioOnChannelZero) {
    const auto out = render(*module, 13, 4);
    EXPECT_GT(rms(out, 0), 0.05f) << "default table should produce audio";

    for (int ch = 1; ch < 13; ++ch)
        EXPECT_NEAR(rms(out, ch), 0.0f, 1.0e-6f) << "channel " << ch << " must stay silent in mono mode";
}

TEST_F(WavetableOscillatorModuleTest, DefaultTablePositionZeroIsASine) {
    // "Basic Shapes" at position 0 is a pure sine: nearly all energy at the fundamental.
    const auto out = render(*module, 13, 8);
    const float fundamental = magnitudeAt(out, 0, 440.0f, kSampleRate);
    const float secondHarmonic = magnitudeAt(out, 0, 880.0f, kSampleRate);
    const float thirdHarmonic = magnitudeAt(out, 0, 1320.0f, kSampleRate);

    EXPECT_GT(fundamental, 0.5f);
    EXPECT_LT(secondHarmonic, fundamental * 0.05f);
    EXPECT_LT(thirdHarmonic, fundamental * 0.05f);
}

TEST_F(WavetableOscillatorModuleTest, ScanningPositionChangesTheSpectrum) {
    const auto atZero = render(*module, 13, 8);
    const float sineThird = magnitudeAt(atZero, 0, 1320.0f, kSampleRate);

    // Position 1.0 of "Basic Shapes" is a square wave — strong odd harmonics.
    module = std::make_unique<WavetableOscillatorModule>();
    module->prepareToPlay(kSampleRate, kBlockSize);
    setFloat(*module, "position", 1.0f);
    const auto atOne = render(*module, 13, 8);
    const float squareThird = magnitudeAt(atOne, 0, 1320.0f, kSampleRate);

    EXPECT_GT(squareThird, sineThird * 10.0f) << "scanning to the square end must add harmonics";
    EXPECT_GT(rms(atOne, 0), 0.05f);
}

TEST_F(WavetableOscillatorModuleTest, EveryBuiltInTableProducesAudio) {
    for (int table = 0; table < WavetableOscillatorModule::kNumBuiltIns; ++table) {
        auto osc = std::make_unique<WavetableOscillatorModule>();
        osc->prepareToPlay(kSampleRate, kBlockSize);
        setChoice(*osc, "table", table);
        setFloat(*osc, "position", 0.5f);
        const auto out = render(*osc, 13, 4);
        EXPECT_GT(rms(out, 0), 0.01f) << "built-in table " << table << " produced no audio";
        EXPECT_EQ(osc->getNumFrames(), WavetableOscillatorModule::kBuiltInFrames);
    }
}

TEST_F(WavetableOscillatorModuleTest, LoadedFileChoiceFallsBackWhenNothingIsLoaded) {
    setChoice(*module, "table", WavetableOscillatorModule::kLoadedTableChoice);
    EXPECT_FALSE(module->hasLoadedWavetable());
    const auto out = render(*module, 13, 4);
    EXPECT_GT(rms(out, 0), 0.05f) << "should fall back to the first built-in, not go silent";
}

TEST_F(WavetableOscillatorModuleTest, LowSampleRateWithExtremeTuningStaysFinite) {
    // At 8 kHz with octave +4 and coarse +12 the note is ~14 kHz, i.e. more than one full
    // cycle per sample. Guards the phase wrap (`while`, not `if`) in renderVoice.
    module->prepareToPlay(8000.0, kBlockSize);
    setInt(*module, "octave", 4);
    setInt(*module, "coarse", 12);
    setInt(*module, "unison", 4);
    setFloat(*module, "detune", 40.0f);

    const auto out = render(*module, 13, 4);
    const float* data = out.getReadPointer(0);
    for (int i = 0; i < out.getNumSamples(); ++i) {
        ASSERT_TRUE(std::isfinite(data[i])) << "non-finite sample at " << i;
        ASSERT_LT(std::abs(data[i]), 2.0f) << "sample out of range at " << i;
    }
}

TEST_F(WavetableOscillatorModuleTest, OutputStaysBounded) {
    setChoice(*module, "table", 2); // Pulse — the most peaky built-in
    setFloat(*module, "position", 0.0f);
    setInt(*module, "unison", 8);
    setFloat(*module, "detune", 50.0f);
    const auto out = render(*module, 13, 8);
    EXPECT_LT(out.getMagnitude(0, 0, out.getNumSamples()), 2.0f) << "output must not blow up";
}

TEST_F(WavetableOscillatorModuleTest, HighNotesDoNotAlias) {
    // A square wave (position 1.0) at MIDI 108 (~4186 Hz) has only 2 harmonics below
    // Nyquist. Without mip selection its 3rd harmonic upwards would fold back down into
    // the audible band. Assert the band below the fundamental stays clean.
    setFloat(*module, "position", 1.0f);
    module->prepareToPlay(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> block(13, kBlockSize);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 108, 1.0f), 0);
    block.clear();
    module->processBlock(block, midi);

    const auto out = render(*module, 13, 8);
    const float fundamental = magnitudeAt(out, 0, 4186.0f, kSampleRate);
    ASSERT_GT(fundamental, 0.1f) << "the note itself must be audible";

    // Sweep the band below the fundamental — nothing should be generated there.
    float worstAlias = 0.0f;
    for (float hz = 200.0f; hz < 3500.0f; hz += 100.0f)
        worstAlias = std::max(worstAlias, magnitudeAt(out, 0, hz, kSampleRate));

    EXPECT_LT(worstAlias, fundamental * 0.05f) << "aliased energy below the fundamental: " << worstAlias;
}

class WavetableMuteBypassTest
    : public WavetableOscillatorModuleTest
    , public ::testing::WithParamInterface<bool> {};

TEST_P(WavetableMuteBypassTest, OutputIsSilentWhenMutedOrBypassed) {
    // Warm up so there is real signal to silence.
    render(*module, 13, 2);

    if (GetParam())
        module->setMuted(true);
    else
        module->setBypassed(true);

    const auto out = render(*module, 13, 2);
    for (int ch = 0; ch < out.getNumChannels(); ++ch)
        EXPECT_NEAR(rms(out, ch), 0.0f, 1.0e-9f) << "channel " << ch;
}

INSTANTIATE_TEST_SUITE_P(MuteAndBypass, WavetableMuteBypassTest, ::testing::Values(true, false));
