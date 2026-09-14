// WavetableOscillatorModuleWarpTests.cpp
// Warp modes (issue #180 phase 2/4): the parameterised alias/bounds sweeps over every
// Warp::Count entry, plus warp-amount and warp-CV behavior.

#include "WavetableOscillatorModuleTestFixture.h"

class WavetableWarpAliasTest
    : public WavetableOscillatorModuleTest
    , public ::testing::WithParamInterface<int> {};

// The anti-aliasing guarantee is the module's contract, and a warp is exactly the thing that can
// break it: warps run AFTER mip selection, so they can put back the harmonics the pyramid exists
// to remove. Every mode is swept at full warp on a high note. Nothing legitimate lives below the
// fundamental for any of these modes — they all reweight or multiply harmonics of f0 — so any
// energy down there is folded.
TEST_P(WavetableWarpAliasTest, EveryWarpModeStaysCleanBelowTheFundamental) {
    constexpr float kNoteHz = 4186.0f; // MIDI 108
    const int warpIndex = GetParam();

    setFloat(*module, "position", 1.0f); // squarest frame — the most harmonics to fold
    setChoice(*module, "warp", warpIndex);
    setFloat(*module, "warpAmount", 1.0f);
    module->prepareToPlay(kSampleRate, kBlockSize);

    const auto out = renderNote(*module, 108);

    // Legitimate content: the fundamental and its harmonics, all at or above kNoteHz.
    const float signal = peakInBand(out, 0, kNoteHz * 0.95f, 20000.0f, 100.0f);
    ASSERT_GT(signal, 0.01f) << kWarpNames[warpIndex] << " produced no audible signal";

    // Folded content lands below the fundamental.
    const float alias = peakInBand(out, 0, 300.0f, kNoteHz * 0.85f, 100.0f);

    EXPECT_LT(alias, signal * 0.05f) << "warp \"" << kWarpNames[warpIndex] << "\" aliases: " << alias << " vs signal "
                                     << signal;
}

INSTANTIATE_TEST_SUITE_P(AllWarpModes, WavetableWarpAliasTest,
                         ::testing::Range(0, (int)WavetableOscillatorModule::Warp::Count));

class WavetableWarpBoundsTest
    : public WavetableOscillatorModuleTest
    , public ::testing::WithParamInterface<int> {};

TEST_P(WavetableWarpBoundsTest, EveryWarpModeStaysBounded) {
    setChoice(*module, "warp", GetParam());
    setFloat(*module, "warpAmount", 1.0f);
    setInt(*module, "unison", 8);
    setFloat(*module, "detune", 50.0f);
    setFloat(*module, "subLevel", 1.0f);
    module->prepareToPlay(kSampleRate, kBlockSize);

    const auto out = renderNote(*module, 60);
    for (int ch : {0, WavetableOscillatorModule::kRightBase}) {
        const float peak = out.getMagnitude(ch, 0, out.getNumSamples());
        EXPECT_LT(peak, 4.0f) << kWarpNames[GetParam()] << " channel " << ch << " blew up";
        EXPECT_TRUE(std::isfinite(peak)) << kWarpNames[GetParam()] << " produced a non-finite sample";
    }
}

INSTANTIATE_TEST_SUITE_P(AllWarpModes, WavetableWarpBoundsTest,
                         ::testing::Range(0, (int)WavetableOscillatorModule::Warp::Count));

TEST_F(WavetableOscillatorModuleTest, WarpAmountZeroLeavesTheWaveAlone) {
    setFloat(*module, "position", 0.6f);
    setChoice(*module, "warp", (int)WavetableOscillatorModule::Warp::Asym);
    setFloat(*module, "warpAmount", 0.0f);
    module->prepareToPlay(kSampleRate, kBlockSize);
    const auto warped = renderNote(*module, 60, 4);

    auto plain = std::make_unique<WavetableOscillatorModule>();
    setFloat(*plain, "position", 0.6f);
    plain->prepareToPlay(kSampleRate, kBlockSize);
    const auto reference = renderNote(*plain, 60, 4);

    for (int i = 0; i < reference.getNumSamples(); i += 37)
        ASSERT_NEAR(warped.getReadPointer(0)[i], reference.getReadPointer(0)[i], 1.0e-5f) << "sample " << i;
}

TEST_F(WavetableOscillatorModuleTest, WarpChangesTheSpectrum) {
    // A sine frame warped by Bend gains harmonics it did not have.
    setFloat(*module, "position", 0.0f); // pure sine
    module->prepareToPlay(kSampleRate, kBlockSize);
    const auto clean = renderNote(*module, 60, 4);
    const float cleanSecond = magnitudeAt(clean, 0, 523.3f, kSampleRate); // 2nd harmonic of C4

    setChoice(*module, "warp", (int)WavetableOscillatorModule::Warp::BendPlus);
    setFloat(*module, "warpAmount", 1.0f);
    module->prepareToPlay(kSampleRate, kBlockSize);
    const auto bent = renderNote(*module, 60, 4);
    const float bentSecond = magnitudeAt(bent, 0, 523.3f, kSampleRate);

    EXPECT_GT(bentSecond, cleanSecond * 4.0f) << "Bend + must add harmonics to a sine";
}

TEST_F(WavetableOscillatorModuleTest, WarpAmountCVDrivesTheWarp) {
    using WT = WavetableOscillatorModule;
    setFloat(*module, "position", 0.0f);
    setChoice(*module, "warp", (int)WT::Warp::BendPlus);
    setFloat(*module, "warpAmount", 0.0f);
    module->prepareToPlay(kSampleRate, kBlockSize);

    // Warm up on a note, then feed full-scale CV into the Warp jack.
    juce::AudioBuffer<float> block(kCh, kBlockSize);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    block.clear();
    module->processBlock(block, midi);

    juce::AudioBuffer<float> out(kCh, 4 * kBlockSize);
    out.clear();
    for (int b = 0; b < 4; ++b) {
        block.clear();
        juce::FloatVectorOperations::fill(block.getWritePointer(WT::kJackWarp), 1.0f, kBlockSize);
        juce::MidiBuffer empty;
        module->processBlock(block, empty);
        for (int ch = 0; ch < kCh; ++ch)
            out.copyFrom(ch, b * kBlockSize, block, ch, 0, kBlockSize);
    }

    EXPECT_GT(magnitudeAt(out, 0, 523.3f, kSampleRate), 0.02f)
        << "CV on the Warp jack must warp the read even with the knob at zero";
}
