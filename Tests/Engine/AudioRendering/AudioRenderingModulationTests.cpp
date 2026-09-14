// Topic: envelope and LFO modulation shaping the rendered signal.

#include "AudioRenderingTestHelpers.h"

// ===========================================================================
// Test 9: ADSR envelope shape (attack/sustain/release phases)
// ===========================================================================
TEST_F(AudioRenderingTest, ADSREnvelopeShape) {
    OscillatorModule osc;
    VCAModule vca;
    ADSRModule adsr;

    setParamValue(&adsr, "attack", 0.01f); // 10ms
    setParamValue(&adsr, "decay", 0.1f);   // 100ms
    setParamValue(&adsr, "sustain", 0.5f); // 50%
    setParamValue(&adsr, "release", 0.1f); // 100ms

    prepareModule(osc);
    prepareModule(vca);
    prepareModule(adsr);

    int oscCh = std::max(osc.getTotalNumInputChannels(), osc.getTotalNumOutputChannels());
    int vcaCh = std::max(vca.getTotalNumInputChannels(), vca.getTotalNumOutputChannels());
    int adsrCh = std::max(adsr.getTotalNumInputChannels(), adsr.getTotalNumOutputChannels());

    int attackSamples = static_cast<int>(kSampleRate * 0.01);
    int sustainSamples = static_cast<int>(kSampleRate * 0.2);
    int noteOffSample = attackSamples + sustainSamples;
    int releaseSamples = static_cast<int>(kSampleRate * 0.3);
    int totalSamples = noteOffSample + releaseSamples;

    juce::AudioBuffer<float> result(2, totalSamples);
    result.clear();

    auto noteOnMidi = TestAudioHelpers::createNoteOnMidi(60, 1.0f);
    int rendered = 0;
    bool sentNoteOn = false;
    bool sentNoteOff = false;

    while (rendered < totalSamples) {
        int n = std::min(kBlockSize, totalSamples - rendered);

        juce::MidiBuffer blockMidi;
        if (!sentNoteOn) {
            blockMidi = noteOnMidi;
            sentNoteOn = true;
        }
        // Send noteOff at the right time
        if (!sentNoteOff && rendered + n > noteOffSample) {
            int offsetInBlock = noteOffSample - rendered;
            if (offsetInBlock >= 0 && offsetInBlock < n)
                blockMidi.addEvent(juce::MidiMessage::noteOff(1, 60, 0.0f), offsetInBlock);
            sentNoteOff = true;
        }

        juce::AudioBuffer<float> oscBuf(oscCh, n);
        oscBuf.clear();
        osc.processBlock(oscBuf, blockMidi);

        juce::AudioBuffer<float> adsrBuf(adsrCh, n);
        adsrBuf.clear();
        adsr.processBlock(adsrBuf, blockMidi);

        juce::AudioBuffer<float> vcaBuf(vcaCh, n);
        vcaBuf.clear();
        vcaBuf.copyFrom(0, 0, oscBuf, 0, 0, n);
        vcaBuf.copyFrom(1, 0, adsrBuf, 0, 0, n);
        juce::MidiBuffer emptyMidi;
        vca.processBlock(vcaBuf, emptyMidi);

        result.copyFrom(0, rendered, vcaBuf, 0, 0, n);
        rendered += n;
    }

    // Sustain phase should have non-zero output
    float sustainRMS = TestAudioHelpers::computeRMSInRange(result, attackSamples, noteOffSample, 0);
    EXPECT_GT(sustainRMS, 0.01f) << "Sustain phase should have non-zero level";

    // After release completes, output should be quieter
    float lateRMS = TestAudioHelpers::computeRMSInRange(result, noteOffSample + releaseSamples / 2, totalSamples, 0);
    EXPECT_LT(lateRMS, sustainRMS) << "Output should decrease during release";
}

// ===========================================================================
// Test 10: LFO modulates filter (output differs from static)
// ===========================================================================
TEST_F(AudioRenderingTest, LFOModulatesFilter) {
    int totalSamples = static_cast<int>(kSampleRate * 0.5);

    // Render 1: Osc → Filter (static cutoff)
    OscillatorModule osc1;
    FilterModule filter1;
    prepareModule(osc1);
    prepareModule(filter1);

    auto midi1 = TestAudioHelpers::createNoteOnMidi(60, 1.0f);
    auto staticOutput = renderChainTwo(osc1, filter1, midi1, totalSamples, 0, 0, true);
    float staticRMS = TestAudioHelpers::computeRMS(staticOutput, 0);

    // Render 2: Osc → Filter with LFO modulating cutoff (ch1)
    OscillatorModule osc2;
    FilterModule filter2;
    LFOModule lfo;
    prepareModule(osc2);
    prepareModule(filter2);
    prepareModule(lfo);

    int oscCh = std::max(osc2.getTotalNumInputChannels(), osc2.getTotalNumOutputChannels());
    int filterCh = std::max(filter2.getTotalNumInputChannels(), filter2.getTotalNumOutputChannels());
    int lfoCh = std::max(lfo.getTotalNumInputChannels(), lfo.getTotalNumOutputChannels());

    auto midi2 = TestAudioHelpers::createNoteOnMidi(60, 1.0f);
    juce::AudioBuffer<float> modulatedResult(filterCh, totalSamples);
    modulatedResult.clear();

    int rendered = 0;
    bool first = true;
    while (rendered < totalSamples) {
        int n = std::min(kBlockSize, totalSamples - rendered);

        juce::MidiBuffer blockMidi;
        if (first) {
            blockMidi = midi2;
            first = false;
        }

        juce::AudioBuffer<float> oscBuf(oscCh, n);
        oscBuf.clear();
        osc2.processBlock(oscBuf, blockMidi);

        juce::AudioBuffer<float> lfoBuf(lfoCh, n);
        lfoBuf.clear();
        juce::MidiBuffer emptyMidi;
        lfo.processBlock(lfoBuf, emptyMidi);

        juce::AudioBuffer<float> filterBuf(filterCh, n);
        filterBuf.clear();
        filterBuf.copyFrom(0, 0, oscBuf, 0, 0, n); // audio in
        filterBuf.copyFrom(1, 0, lfoBuf, 0, 0, n); // cutoff CV
        filter2.processBlock(filterBuf, emptyMidi);

        for (int ch = 0; ch < filterCh; ++ch)
            modulatedResult.copyFrom(ch, rendered, filterBuf, ch, 0, n);
        rendered += n;
    }

    // Both should produce audio
    EXPECT_GT(staticRMS, 0.0f) << "Static filter should produce audio";
    EXPECT_FALSE(TestAudioHelpers::isSilent(modulatedResult, 0)) << "Modulated filter should produce audio";

    // Modulated output should differ from static.
    // Compare sample-by-sample: the two outputs should not be identical.
    float diffSum = 0.0f;
    int compareSamples = std::min(staticOutput.getNumSamples(), modulatedResult.getNumSamples());
    const float* s = staticOutput.getReadPointer(0);
    const float* m = modulatedResult.getReadPointer(0);
    for (int i = 0; i < compareSamples; ++i)
        diffSum += std::abs(s[i] - m[i]);

    EXPECT_GT(diffSum, 0.0f) << "LFO modulation should produce different output than static filter";
}
