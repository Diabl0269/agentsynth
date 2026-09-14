// Topic: every FX module produces output when fed an oscillator.

#include "AudioRenderingTestHelpers.h"

// ===========================================================================
// Test 7: Each FX module produces output
// ===========================================================================
void testFXModule(juce::AudioProcessor& fx) {
    OscillatorModule osc;
    osc.prepareToPlay(kSampleRate, kBlockSize);
    fx.prepareToPlay(kSampleRate, kBlockSize);

    int oscCh = std::max(osc.getTotalNumInputChannels(), osc.getTotalNumOutputChannels());
    int fxCh = std::max(fx.getTotalNumInputChannels(), fx.getTotalNumOutputChannels());
    if (fxCh == 0)
        fxCh = 2;
    int totalSamples = static_cast<int>(kSampleRate * 0.5);

    juce::AudioBuffer<float> result(fxCh, totalSamples);
    result.clear();

    auto midi = TestAudioHelpers::createNoteOnMidi(60, 1.0f);
    int rendered = 0;
    bool first = true;

    while (rendered < totalSamples) {
        int n = std::min(kBlockSize, totalSamples - rendered);

        juce::AudioBuffer<float> oscBuf(oscCh, n);
        oscBuf.clear();
        juce::MidiBuffer blockMidi;
        if (first) {
            blockMidi = midi;
            first = false;
        }
        osc.processBlock(oscBuf, blockMidi);

        juce::AudioBuffer<float> fxBuf(fxCh, n);
        fxBuf.clear();
        // Route osc ch0 → FX ch0 (and ch1 for stereo FX)
        fxBuf.copyFrom(0, 0, oscBuf, 0, 0, n);
        if (fxCh >= 2)
            fxBuf.copyFrom(1, 0, oscBuf, 0, 0, n);
        juce::MidiBuffer emptyMidi;
        fx.processBlock(fxBuf, emptyMidi);

        result.copyFrom(0, rendered, fxBuf, 0, 0, n);
        rendered += n;
    }

    EXPECT_FALSE(TestAudioHelpers::isSilent(result, 0));
}

TEST_F(AudioRenderingTest, DelayModuleProducesOutput) {
    DelayModule fx;
    testFXModule(fx);
}

TEST_F(AudioRenderingTest, DistortionModuleProducesOutput) {
    DistortionModule fx;
    testFXModule(fx);
}

TEST_F(AudioRenderingTest, ReverbModuleProducesOutput) {
    ReverbModule fx;
    testFXModule(fx);
}

TEST_F(AudioRenderingTest, ChorusModuleProducesOutput) {
    ChorusModule fx;
    testFXModule(fx);
}

TEST_F(AudioRenderingTest, PhaserModuleProducesOutput) {
    PhaserModule fx;
    testFXModule(fx);
}

TEST_F(AudioRenderingTest, CompressorModuleProducesOutput) {
    CompressorModule fx;
    testFXModule(fx);
}

TEST_F(AudioRenderingTest, FlangerModuleProducesOutput) {
    FlangerModule fx;
    testFXModule(fx);
}

TEST_F(AudioRenderingTest, LimiterModuleProducesOutput) {
    LimiterModule fx;
    testFXModule(fx);
}
