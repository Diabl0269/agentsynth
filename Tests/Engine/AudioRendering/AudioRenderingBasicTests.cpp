// Topic: basic module and chain rendering -- oscillator/filter/VCA/sequencer signal flow with no graph or presets
// involved.

#include "AudioRenderingTestHelpers.h"

// ===========================================================================
// Test 1: Oscillator produces sound with MIDI note
// ===========================================================================
TEST_F(AudioRenderingTest, OscillatorProducesSound) {
    OscillatorModule osc;
    prepareModule(osc);

    auto midi = TestAudioHelpers::createNoteOnMidi(60, 1.0f);
    auto output = renderModule(osc, midi, static_cast<int>(kSampleRate));

    EXPECT_FALSE(TestAudioHelpers::isSilent(output, 0)) << "Oscillator should produce audio with MIDI noteOn";
}

// ===========================================================================
// Test 2: Oscillator produces correct frequency (C4 = 261.6 Hz)
// ===========================================================================
TEST_F(AudioRenderingTest, OscillatorFrequencyIsCorrect) {
    OscillatorModule osc;
    setChoiceParam(&osc, "waveform", 0); // Sine
    prepareModule(osc);

    auto midi = TestAudioHelpers::createNoteOnMidi(60, 1.0f);
    auto output = renderModule(osc, midi, static_cast<int>(kSampleRate));

    float freq = TestAudioHelpers::estimateFrequencyByZeroCrossings(output, kSampleRate, 0);
    EXPECT_NEAR(freq, 261.6f, 10.0f) << "C4 should be ~261.6 Hz";
}

// ===========================================================================
// Test 3: Osc → Filter → VCA chain produces sound
// ===========================================================================
TEST_F(AudioRenderingTest, OscFilterVCAChainProducesSound) {
    OscillatorModule osc;
    FilterModule filter;
    VCAModule vca;
    ADSRModule adsr;

    prepareModule(osc);
    prepareModule(filter);
    prepareModule(vca);
    prepareModule(adsr);

    int totalSamples = static_cast<int>(kSampleRate); // 1 second
    auto midi = TestAudioHelpers::createNoteOnMidi(60, 1.0f);

    int oscCh = std::max(osc.getTotalNumInputChannels(), osc.getTotalNumOutputChannels());
    int filterCh = std::max(filter.getTotalNumInputChannels(), filter.getTotalNumOutputChannels());
    int vcaCh = std::max(vca.getTotalNumInputChannels(), vca.getTotalNumOutputChannels());
    int adsrCh = std::max(adsr.getTotalNumInputChannels(), adsr.getTotalNumOutputChannels());

    juce::AudioBuffer<float> result(2, totalSamples);
    result.clear();

    int rendered = 0;
    bool first = true;
    while (rendered < totalSamples) {
        int n = std::min(kBlockSize, totalSamples - rendered);

        juce::AudioBuffer<float> oscBuf(oscCh, n);
        oscBuf.clear();
        juce::AudioBuffer<float> adsrBuf(adsrCh, n);
        adsrBuf.clear();
        juce::AudioBuffer<float> filterBuf(filterCh, n);
        filterBuf.clear();
        juce::AudioBuffer<float> vcaBuf(vcaCh, n);
        vcaBuf.clear();

        juce::MidiBuffer blockMidi;
        if (first) {
            blockMidi = midi;
            first = false;
        }

        // Process osc (reads MIDI for pitch)
        osc.processBlock(oscBuf, blockMidi);

        // Process ADSR (reads MIDI for gate)
        adsr.processBlock(adsrBuf, blockMidi);

        // Route osc ch0 → filter ch0
        filterBuf.copyFrom(0, 0, oscBuf, 0, 0, n);
        juce::MidiBuffer emptyMidi;
        filter.processBlock(filterBuf, emptyMidi);

        // Route filter ch0 → VCA ch0 (audio), ADSR ch0 → VCA ch1 (CV)
        vcaBuf.copyFrom(0, 0, filterBuf, 0, 0, n);
        vcaBuf.copyFrom(1, 0, adsrBuf, 0, 0, n);
        vca.processBlock(vcaBuf, emptyMidi);

        // Collect VCA output ch0
        result.copyFrom(0, rendered, vcaBuf, 0, 0, n);
        rendered += n;
    }

    EXPECT_FALSE(TestAudioHelpers::isSilent(result, 0)) << "Osc→Filter→VCA chain should produce audio";
}

// ===========================================================================
// Test 4: Filter affects spectrum (low cutoff reduces RMS)
// ===========================================================================
TEST_F(AudioRenderingTest, FilterAffectsSpectrum) {
    // Render 1: Osc (saw) → no filter
    OscillatorModule osc1;
    setChoiceParam(&osc1, "waveform", 2); // Saw
    prepareModule(osc1);

    auto midi = TestAudioHelpers::createNoteOnMidi(60, 1.0f);
    int totalSamples = static_cast<int>(kSampleRate * 0.5);
    auto unfilteredOutput = renderModule(osc1, midi, totalSamples);
    float unfilteredRMS = TestAudioHelpers::computeRMS(unfilteredOutput, 0);

    // Render 2: Osc (saw) → Filter (low cutoff 200 Hz)
    OscillatorModule osc2;
    FilterModule filter;
    setChoiceParam(&osc2, "waveform", 2); // Saw
    setParamValue(&filter, "cutoff", 200.0f);
    prepareModule(osc2);
    prepareModule(filter);

    auto midi2 = TestAudioHelpers::createNoteOnMidi(60, 1.0f);
    auto filteredOutput = renderChainTwo(osc2, filter, midi2, totalSamples, 0, 0, true);
    float filteredRMS = TestAudioHelpers::computeRMS(filteredOutput, 0);

    EXPECT_GT(unfilteredRMS, 0.0f) << "Unfiltered saw should produce audio";
    EXPECT_LT(filteredRMS, unfilteredRMS) << "Low-pass filter at 200Hz should reduce RMS of saw wave at C4";
}

// ===========================================================================
// Test 5: Sequencer drives oscillator (internal MIDI generation)
// ===========================================================================
TEST_F(AudioRenderingTest, SequencerDrivesOscillator) {
    SequencerModule seq;
    OscillatorModule osc;
    prepareModule(seq);
    prepareModule(osc);

    int oscCh = std::max(osc.getTotalNumInputChannels(), osc.getTotalNumOutputChannels());
    int totalSamples = static_cast<int>(kSampleRate); // 1 second at 120 BPM
    juce::AudioBuffer<float> result(oscCh, totalSamples);
    result.clear();

    int rendered = 0;
    while (rendered < totalSamples) {
        int n = std::min(kBlockSize, totalSamples - rendered);

        // Sequencer has 0 audio channels, only generates MIDI
        juce::AudioBuffer<float> seqBuf(1, n); // min 1 channel
        seqBuf.clear();
        juce::MidiBuffer seqMidi;
        seq.processBlock(seqBuf, seqMidi);

        // Feed sequencer's MIDI to oscillator
        juce::AudioBuffer<float> oscBuf(oscCh, n);
        oscBuf.clear();
        osc.processBlock(oscBuf, seqMidi);

        result.copyFrom(0, rendered, oscBuf, 0, 0, n);
        rendered += n;
    }

    EXPECT_FALSE(TestAudioHelpers::isSilent(result, 0))
        << "Sequencer should generate MIDI that drives oscillator to produce audio";
}

// ===========================================================================
// Test 6: Sequencer → Osc → Filter (full patch without VCA complexity)
// ===========================================================================
TEST_F(AudioRenderingTest, SequencerFullPatchWithFilter) {
    SequencerModule seq;
    OscillatorModule osc;
    FilterModule filter;

    prepareModule(seq);
    prepareModule(osc);
    prepareModule(filter);

    int oscCh = std::max(osc.getTotalNumInputChannels(), osc.getTotalNumOutputChannels());
    int filterCh = std::max(filter.getTotalNumInputChannels(), filter.getTotalNumOutputChannels());
    int totalSamples = static_cast<int>(kSampleRate); // 1 second

    juce::AudioBuffer<float> result(2, totalSamples);
    result.clear();

    int rendered = 0;
    while (rendered < totalSamples) {
        int n = std::min(kBlockSize, totalSamples - rendered);

        // 1. Sequencer generates MIDI
        juce::AudioBuffer<float> seqBuf(1, n);
        seqBuf.clear();
        juce::MidiBuffer seqMidi;
        seq.processBlock(seqBuf, seqMidi);

        // 2. Osc consumes MIDI
        juce::AudioBuffer<float> oscBuf(oscCh, n);
        oscBuf.clear();
        osc.processBlock(oscBuf, seqMidi);

        // 3. Osc → Filter
        juce::AudioBuffer<float> filterBuf(filterCh, n);
        filterBuf.clear();
        filterBuf.copyFrom(0, 0, oscBuf, 0, 0, n);
        juce::MidiBuffer emptyMidi;
        filter.processBlock(filterBuf, emptyMidi);

        result.copyFrom(0, rendered, filterBuf, 0, 0, n);
        rendered += n;
    }

    EXPECT_FALSE(TestAudioHelpers::isSilent(result, 0)) << "Patch Seq→Osc→Filter should produce audio";
}
