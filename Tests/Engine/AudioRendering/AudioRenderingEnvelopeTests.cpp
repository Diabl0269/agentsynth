// Topic: envelope-gated note sequences -- the Oscillator -> VCA chain driven by an ADSR must reproduce
// osc * envelope sample for sample, so no note onset is lost, delayed or stepped.

#include "AudioRenderingTestHelpers.h"

// Regression test for FRO744: a fast sine sequence (1 ms attack, sustain 0) clicked at every note onset
// because the VCA probed only the first 64 samples of a block for its CV, found the idle envelope
// silent, and gated the whole block with a silent fallback channel.
TEST_F(AudioRenderingTest, EnvelopeGatedNotesFollowTheEnvelopeAtEveryOnset) {
    OscillatorModule osc;
    VCAModule vca;
    ADSRModule adsr;
    setChoiceParam(&osc, "waveform", 0);
    setParamValue(&adsr, "attack", 0.001f);
    setParamValue(&adsr, "decay", 0.0735f);
    setParamValue(&adsr, "sustain", 0.0f);
    setParamValue(&adsr, "release", 0.015f);
    prepareModule(osc);
    prepareModule(vca);
    prepareModule(adsr);

    const int pitches[16] = {68, 52, 58, 51, 53, 48, 66, 69, 37, 46, 65, 41, 71, 53, 69, 39};
    const double step = 0.125 * kSampleRate; // 16th notes at 120 bpm, each ending where the next begins
    const int total = static_cast<int>(step * 16);
    const int oscCh = std::max(osc.getTotalNumInputChannels(), osc.getTotalNumOutputChannels());
    const int adsrCh = std::max(adsr.getTotalNumInputChannels(), adsr.getTotalNumOutputChannels());
    const int vcaCh = std::max(vca.getTotalNumInputChannels(), vca.getTotalNumOutputChannels());
    const float vcaGain = 0.5f; // the VCA's default

    float worstError = 0.0f;
    for (int pos = 0; pos < total; pos += kBlockSize) {
        const int n = std::min(kBlockSize, total - pos);
        juce::MidiBuffer midi;
        for (int i = 0; i < 16; ++i) {
            const int on = static_cast<int>(std::lround(i * step));
            const int off = static_cast<int>(std::lround((i + 1) * step));
            if (off >= pos && off < pos + n)
                midi.addEvent(juce::MidiMessage::noteOff(1, pitches[i], 0.0f), off - pos);
            if (on >= pos && on < pos + n)
                midi.addEvent(juce::MidiMessage::noteOn(1, pitches[i], 0.8f), on - pos);
        }
        juce::AudioBuffer<float> oscBuf(oscCh, n), adsrBuf(adsrCh, n), vcaBuf(vcaCh, n);
        oscBuf.clear();
        adsrBuf.clear();
        vcaBuf.clear();
        osc.processBlock(oscBuf, midi);
        adsr.processBlock(adsrBuf, midi);
        vcaBuf.copyFrom(0, 0, oscBuf, 0, 0, n);
        vcaBuf.copyFrom(1, 0, adsrBuf, 0, 0, n);
        juce::MidiBuffer none;
        vca.processBlock(vcaBuf, none);
        for (int i = 0; i < n; ++i) {
            const float expected = oscBuf.getSample(0, i) * adsrBuf.getSample(0, i) * vcaGain;
            worstError = std::max(worstError, std::abs(vcaBuf.getSample(0, i) - expected));
        }
    }
    EXPECT_LT(worstError, 1.0e-4f);
}
