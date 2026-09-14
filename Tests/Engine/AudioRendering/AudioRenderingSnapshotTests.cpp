// Topic: golden-file snapshot/reference regression tests for module and modulation chains.

#include "AudioRenderingTestHelpers.h"

// ===========================================================================
// Snapshot/Reference Tests
// These compare rendered audio against stored reference files.
// If a reference file doesn't exist, the test creates it (first run).
// On subsequent runs, it compares output against the reference.
// To regenerate: delete Tests/reference/ and re-run tests.
// ===========================================================================

TEST_F(AudioRenderingTest, SnapshotOscillatorSine) {
    OscillatorModule osc;
    setChoiceParam(&osc, "waveform", 0); // Sine
    prepareModule(osc);

    auto midi = TestAudioHelpers::createNoteOnMidi(60, 1.0f);
    auto output = renderModule(osc, midi, static_cast<int>(kSampleRate * 0.5)); // 0.5 seconds

    auto refPath = getReferencePath("osc_sine_c4.raw");
    if (!TestAudioHelpers::referenceExists(refPath)) {
        ASSERT_TRUE(TestAudioHelpers::saveReference(output, refPath)) << "Failed to save reference file: " << refPath;
        std::cout << "[  GOLDEN ] Created reference: " << refPath << std::endl;
    } else {
        auto ref = TestAudioHelpers::loadReference(refPath);
        ASSERT_EQ(ref.getNumSamples(), output.getNumSamples()) << "Reference sample count mismatch";
        float maxDiff = TestAudioHelpers::compareBuffers(output, ref);
        EXPECT_LT(maxDiff, 1e-5f) << "Oscillator sine output drifted from reference (max diff: " << maxDiff << ")";
    }
}

TEST_F(AudioRenderingTest, SnapshotOscillatorSaw) {
    OscillatorModule osc;
    setChoiceParam(&osc, "waveform", 2); // Saw
    prepareModule(osc);

    auto midi = TestAudioHelpers::createNoteOnMidi(60, 1.0f);
    auto output = renderModule(osc, midi, static_cast<int>(kSampleRate * 0.5));

    auto refPath = getReferencePath("osc_saw_c4.raw");
    if (!TestAudioHelpers::referenceExists(refPath)) {
        ASSERT_TRUE(TestAudioHelpers::saveReference(output, refPath)) << "Failed to save reference file: " << refPath;
        std::cout << "[  GOLDEN ] Created reference: " << refPath << std::endl;
    } else {
        auto ref = TestAudioHelpers::loadReference(refPath);
        ASSERT_EQ(ref.getNumSamples(), output.getNumSamples());
        float maxDiff = TestAudioHelpers::compareBuffers(output, ref);
        EXPECT_LT(maxDiff, 1e-5f) << "Oscillator saw output drifted from reference (max diff: " << maxDiff << ")";
    }
}

TEST_F(AudioRenderingTest, SnapshotFilterLowPass) {
    OscillatorModule osc;
    FilterModule filter;
    setChoiceParam(&osc, "waveform", 2); // Saw
    setParamValue(&filter, "cutoff", 500.0f);
    prepareModule(osc);
    prepareModule(filter);

    auto midi = TestAudioHelpers::createNoteOnMidi(60, 1.0f);
    auto output = renderChainTwo(osc, filter, midi, static_cast<int>(kSampleRate * 0.5), 0, 0, true);

    auto refPath = getReferencePath("filter_lpf_500hz.raw");
    if (!TestAudioHelpers::referenceExists(refPath)) {
        ASSERT_TRUE(TestAudioHelpers::saveReference(output, refPath)) << "Failed to save reference file: " << refPath;
        std::cout << "[  GOLDEN ] Created reference: " << refPath << std::endl;
    } else {
        auto ref = TestAudioHelpers::loadReference(refPath);
        ASSERT_EQ(ref.getNumSamples(), output.getNumSamples());
        float maxDiff = TestAudioHelpers::compareBuffers(output, ref);
        EXPECT_LT(maxDiff, 1e-5f) << "Filter low-pass output drifted from reference (max diff: " << maxDiff << ")";
    }
}

TEST_F(AudioRenderingTest, SnapshotOscFilterVCAChain) {
    OscillatorModule osc;
    FilterModule filter;
    VCAModule vca;
    ADSRModule adsr;

    setChoiceParam(&osc, "waveform", 2); // Saw
    setParamValue(&filter, "cutoff", 1000.0f);
    setParamValue(&adsr, "attack", 0.01f);
    setParamValue(&adsr, "sustain", 0.8f);

    prepareModule(osc);
    prepareModule(filter);
    prepareModule(vca);
    prepareModule(adsr);

    int totalSamples = static_cast<int>(kSampleRate * 0.5);
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

        osc.processBlock(oscBuf, blockMidi);
        adsr.processBlock(adsrBuf, blockMidi);

        filterBuf.copyFrom(0, 0, oscBuf, 0, 0, n);
        juce::MidiBuffer emptyMidi;
        filter.processBlock(filterBuf, emptyMidi);

        vcaBuf.copyFrom(0, 0, filterBuf, 0, 0, n);
        vcaBuf.copyFrom(1, 0, adsrBuf, 0, 0, n);
        vca.processBlock(vcaBuf, emptyMidi);

        result.copyFrom(0, rendered, vcaBuf, 0, 0, n);
        rendered += n;
    }

    auto refPath = getReferencePath("chain_osc_filter_vca.raw");
    if (!TestAudioHelpers::referenceExists(refPath)) {
        ASSERT_TRUE(TestAudioHelpers::saveReference(result, refPath)) << "Failed to save reference file: " << refPath;
        std::cout << "[  GOLDEN ] Created reference: " << refPath << std::endl;
    } else {
        auto ref = TestAudioHelpers::loadReference(refPath);
        ASSERT_EQ(ref.getNumSamples(), result.getNumSamples());
        float maxDiff = TestAudioHelpers::compareBuffers(result, ref);
        EXPECT_LT(maxDiff, 1e-5f) << "Full chain output drifted from reference (max diff: " << maxDiff << ")";
    }
}

// ===========================================================================
// Test: LFO modulating filter cutoff
// ===========================================================================
TEST_F(AudioRenderingTest, SnapshotLFOToFilterCutoff) {
    OscillatorModule osc;
    FilterModule filter;
    LFOModule lfo;

    setChoiceParam(&osc, "waveform", 2); // Saw
    setParamValue(&filter, "cutoff", 1000.0f);
    prepareModule(osc);
    prepareModule(filter);
    prepareModule(lfo);

    int oscCh = std::max(osc.getTotalNumInputChannels(), osc.getTotalNumOutputChannels());
    int filterCh = std::max(filter.getTotalNumInputChannels(), filter.getTotalNumOutputChannels());
    int lfoCh = std::max(lfo.getTotalNumInputChannels(), lfo.getTotalNumOutputChannels());
    int totalSamples = static_cast<int>(kSampleRate * 0.5);

    auto midi = TestAudioHelpers::createNoteOnMidi(60, 1.0f);
    juce::AudioBuffer<float> result(filterCh, totalSamples);
    result.clear();

    int rendered = 0;
    bool first = true;
    while (rendered < totalSamples) {
        int n = std::min(kBlockSize, totalSamples - rendered);
        juce::MidiBuffer blockMidi;
        if (first) {
            blockMidi = midi;
            first = false;
        }

        juce::AudioBuffer<float> oscBuf(oscCh, n);
        oscBuf.clear();
        osc.processBlock(oscBuf, blockMidi);

        juce::AudioBuffer<float> lfoBuf(lfoCh, n);
        lfoBuf.clear();
        juce::MidiBuffer emptyMidi;
        lfo.processBlock(lfoBuf, emptyMidi);

        juce::AudioBuffer<float> filterBuf(filterCh, n);
        filterBuf.clear();
        filterBuf.copyFrom(0, 0, oscBuf, 0, 0, n); // audio in
        filterBuf.copyFrom(1, 0, lfoBuf, 0, 0, n); // cutoff CV
        filter.processBlock(filterBuf, emptyMidi);

        result.copyFrom(0, rendered, filterBuf, 0, 0, n);
        rendered += n;
    }

    auto refPath = getReferencePath("mod_lfo_filter_cutoff.raw");
    if (!TestAudioHelpers::referenceExists(refPath)) {
        ASSERT_TRUE(TestAudioHelpers::saveReference(result, refPath)) << "Failed to save reference file: " << refPath;
        std::cout << "[  GOLDEN ] Created reference: " << refPath << std::endl;
    } else {
        auto ref = TestAudioHelpers::loadReference(refPath);
        ASSERT_EQ(ref.getNumSamples(), result.getNumSamples());
        float maxDiff = TestAudioHelpers::compareBuffers(result, ref);
        EXPECT_LT(maxDiff, 1e-5f) << "LFO-to-filter cutoff modulation drifted from reference (max diff: " << maxDiff
                                  << ")";
    }
}

// ===========================================================================
// Test: ADSR envelope controlling VCA
// ===========================================================================
TEST_F(AudioRenderingTest, SnapshotADSRToVCAEnvelope) {
    OscillatorModule osc;
    VCAModule vca;
    ADSRModule adsr;

    setChoiceParam(&osc, "waveform", 0); // Sine
    setParamValue(&adsr, "attack", 0.05f);
    setParamValue(&adsr, "decay", 0.2f);
    setParamValue(&adsr, "sustain", 0.6f);
    setParamValue(&adsr, "release", 0.3f);

    prepareModule(osc);
    prepareModule(vca);
    prepareModule(adsr);

    int oscCh = std::max(osc.getTotalNumInputChannels(), osc.getTotalNumOutputChannels());
    int vcaCh = std::max(vca.getTotalNumInputChannels(), vca.getTotalNumOutputChannels());
    int adsrCh = std::max(adsr.getTotalNumInputChannels(), adsr.getTotalNumOutputChannels());
    int totalSamples = static_cast<int>(kSampleRate * 1.0); // 1 second

    auto noteOn = TestAudioHelpers::createNoteOnMidi(60, 1.0f);
    juce::AudioBuffer<float> result(vcaCh, totalSamples);
    result.clear();

    int rendered = 0;
    bool first = true;
    int noteOffSample = static_cast<int>(kSampleRate * 0.5); // noteOff at 0.5s

    while (rendered < totalSamples) {
        int n = std::min(kBlockSize, totalSamples - rendered);
        juce::MidiBuffer blockMidi;

        if (first) {
            blockMidi = noteOn;
            first = false;
        }

        // Inject noteOff at the appropriate sample
        if (rendered < noteOffSample && rendered + n >= noteOffSample) {
            int offsetInBlock = noteOffSample - rendered;
            juce::MidiMessage noteOffMsg = juce::MidiMessage::noteOff(1, 60, (juce::uint8)0);
            blockMidi.addEvent(noteOffMsg, offsetInBlock);
        }

        juce::AudioBuffer<float> oscBuf(oscCh, n);
        oscBuf.clear();
        osc.processBlock(oscBuf, blockMidi);

        juce::AudioBuffer<float> adsrBuf(adsrCh, n);
        adsrBuf.clear();
        adsr.processBlock(adsrBuf, blockMidi);

        juce::AudioBuffer<float> vcaBuf(vcaCh, n);
        vcaBuf.clear();
        vcaBuf.copyFrom(0, 0, oscBuf, 0, 0, n);  // audio in
        vcaBuf.copyFrom(1, 0, adsrBuf, 0, 0, n); // envelope CV
        juce::MidiBuffer emptyMidi;
        vca.processBlock(vcaBuf, emptyMidi);

        result.copyFrom(0, rendered, vcaBuf, 0, 0, n);
        rendered += n;
    }

    auto refPath = getReferencePath("mod_adsr_vca_envelope.raw");
    if (!TestAudioHelpers::referenceExists(refPath)) {
        ASSERT_TRUE(TestAudioHelpers::saveReference(result, refPath)) << "Failed to save reference file: " << refPath;
        std::cout << "[  GOLDEN ] Created reference: " << refPath << std::endl;
    } else {
        auto ref = TestAudioHelpers::loadReference(refPath);
        ASSERT_EQ(ref.getNumSamples(), result.getNumSamples());
        float maxDiff = TestAudioHelpers::compareBuffers(result, ref);
        EXPECT_LT(maxDiff, 1e-5f) << "ADSR-to-VCA envelope modulation drifted from reference (max diff: " << maxDiff
                                  << ")";
    }
}

// ===========================================================================
// Test: LFO modulating oscillator pitch (vibrato effect)
// ===========================================================================
TEST_F(AudioRenderingTest, SnapshotLFOToOscPitch) {
    OscillatorModule osc;
    LFOModule lfo;

    setChoiceParam(&osc, "waveform", 0); // Sine
    prepareModule(osc);
    prepareModule(lfo);

    int oscCh = std::max(osc.getTotalNumInputChannels(), osc.getTotalNumOutputChannels());
    int lfoCh = std::max(lfo.getTotalNumInputChannels(), lfo.getTotalNumOutputChannels());
    int totalSamples = static_cast<int>(kSampleRate * 0.5);

    auto midi = TestAudioHelpers::createNoteOnMidi(60, 1.0f);
    juce::AudioBuffer<float> result(oscCh, totalSamples);
    result.clear();

    int rendered = 0;
    bool first = true;
    while (rendered < totalSamples) {
        int n = std::min(kBlockSize, totalSamples - rendered);
        juce::MidiBuffer blockMidi;
        if (first) {
            blockMidi = midi;
            first = false;
        }

        juce::AudioBuffer<float> lfoBuf(lfoCh, n);
        lfoBuf.clear();
        juce::MidiBuffer emptyMidi;
        lfo.processBlock(lfoBuf, emptyMidi);

        juce::AudioBuffer<float> oscBuf(oscCh, n);
        oscBuf.clear();
        // Route LFO to fine pitch CV (channel 4)
        if (oscCh > 4) {
            oscBuf.copyFrom(4, 0, lfoBuf, 0, 0, n);
        }
        osc.processBlock(oscBuf, blockMidi);

        result.copyFrom(0, rendered, oscBuf, 0, 0, n);
        rendered += n;
    }

    auto refPath = getReferencePath("mod_lfo_osc_pitch.raw");
    if (!TestAudioHelpers::referenceExists(refPath)) {
        ASSERT_TRUE(TestAudioHelpers::saveReference(result, refPath)) << "Failed to save reference file: " << refPath;
        std::cout << "[  GOLDEN ] Created reference: " << refPath << std::endl;
    } else {
        auto ref = TestAudioHelpers::loadReference(refPath);
        ASSERT_EQ(ref.getNumSamples(), result.getNumSamples());
        float maxDiff = TestAudioHelpers::compareBuffers(result, ref);
        EXPECT_LT(maxDiff, 1e-5f) << "LFO-to-oscillator pitch modulation drifted from reference (max diff: " << maxDiff
                                  << ")";
    }
}

// ===========================================================================
// Test: Full patch with modulation (Osc → Filter → VCA, LFO → Filter cutoff, ADSR → VCA)
// ===========================================================================
TEST_F(AudioRenderingTest, SnapshotFullPatchWithModulation) {
    OscillatorModule osc;
    FilterModule filter;
    VCAModule vca;
    ADSRModule adsr;
    LFOModule lfo;

    setChoiceParam(&osc, "waveform", 2); // Saw
    setParamValue(&filter, "cutoff", 2000.0f);
    setParamValue(&adsr, "attack", 0.02f);
    setParamValue(&adsr, "sustain", 0.7f);

    prepareModule(osc);
    prepareModule(filter);
    prepareModule(vca);
    prepareModule(adsr);
    prepareModule(lfo);

    int oscCh = std::max(osc.getTotalNumInputChannels(), osc.getTotalNumOutputChannels());
    int filterCh = std::max(filter.getTotalNumInputChannels(), filter.getTotalNumOutputChannels());
    int vcaCh = std::max(vca.getTotalNumInputChannels(), vca.getTotalNumOutputChannels());
    int adsrCh = std::max(adsr.getTotalNumInputChannels(), adsr.getTotalNumOutputChannels());
    int lfoCh = std::max(lfo.getTotalNumInputChannels(), lfo.getTotalNumOutputChannels());
    int totalSamples = static_cast<int>(kSampleRate * 0.5);

    auto midi = TestAudioHelpers::createNoteOnMidi(60, 1.0f);
    juce::AudioBuffer<float> result(2, totalSamples);
    result.clear();

    int rendered = 0;
    bool first = true;
    while (rendered < totalSamples) {
        int n = std::min(kBlockSize, totalSamples - rendered);
        juce::MidiBuffer blockMidi;
        if (first) {
            blockMidi = midi;
            first = false;
        }

        // OSC produces audio
        juce::AudioBuffer<float> oscBuf(oscCh, n);
        oscBuf.clear();
        osc.processBlock(oscBuf, blockMidi);

        // ADSR produces envelope
        juce::AudioBuffer<float> adsrBuf(adsrCh, n);
        adsrBuf.clear();
        adsr.processBlock(adsrBuf, blockMidi);

        // LFO produces modulation
        juce::AudioBuffer<float> lfoBuf(lfoCh, n);
        lfoBuf.clear();
        juce::MidiBuffer emptyMidi;
        lfo.processBlock(lfoBuf, emptyMidi);

        // Filter: osc audio → ch0, LFO → ch1 (cutoff CV)
        juce::AudioBuffer<float> filterBuf(filterCh, n);
        filterBuf.clear();
        filterBuf.copyFrom(0, 0, oscBuf, 0, 0, n);
        filterBuf.copyFrom(1, 0, lfoBuf, 0, 0, n);
        filter.processBlock(filterBuf, emptyMidi);

        // VCA: filter audio → ch0, ADSR → ch1 (amplitude CV)
        juce::AudioBuffer<float> vcaBuf(vcaCh, n);
        vcaBuf.clear();
        vcaBuf.copyFrom(0, 0, filterBuf, 0, 0, n);
        vcaBuf.copyFrom(1, 0, adsrBuf, 0, 0, n);
        vca.processBlock(vcaBuf, emptyMidi);

        result.copyFrom(0, rendered, vcaBuf, 0, 0, n);
        rendered += n;
    }

    auto refPath = getReferencePath("mod_full_patch_modulation.raw");
    if (!TestAudioHelpers::referenceExists(refPath)) {
        ASSERT_TRUE(TestAudioHelpers::saveReference(result, refPath)) << "Failed to save reference file: " << refPath;
        std::cout << "[  GOLDEN ] Created reference: " << refPath << std::endl;
    } else {
        auto ref = TestAudioHelpers::loadReference(refPath);
        ASSERT_EQ(ref.getNumSamples(), result.getNumSamples());
        float maxDiff = TestAudioHelpers::compareBuffers(result, ref);
        EXPECT_LT(maxDiff, 1e-5f) << "Full patch with modulation drifted from reference (max diff: " << maxDiff << ")";
    }
}

// ===========================================================================
// Test: ExternalMidiModule triggers Oscillator (End-to-End)
// ===========================================================================
TEST_F(AudioRenderingTest, SnapshotExternalMidiToOscillator) {
    ExternalMidiModule midiModule;
    OscillatorModule osc;

    setChoiceParam(&osc, "waveform", 0); // Sine
    prepareModule(midiModule);
    prepareModule(osc);

    int oscCh = std::max(osc.getTotalNumInputChannels(), osc.getTotalNumOutputChannels());
    int totalSamples = static_cast<int>(kSampleRate * 0.5);

    // Push MIDI to ExternalMidiModule
    auto midiMsg = juce::MidiMessage::noteOn(1, 60, 1.0f);
    midiModule.pushMidiMessage(midiMsg);

    juce::AudioBuffer<float> result(oscCh, totalSamples);
    result.clear();

    int rendered = 0;
    while (rendered < totalSamples) {
        int n = std::min(kBlockSize, totalSamples - rendered);

        // Process ExternalMidiModule to generate MIDI stream
        juce::AudioBuffer<float> midiBuf(1, n);
        midiBuf.clear();
        juce::MidiBuffer outputMidi;
        midiModule.processBlock(midiBuf, outputMidi);

        // Feed generated MIDI to Oscillator
        juce::AudioBuffer<float> oscBuf(oscCh, n);
        oscBuf.clear();
        osc.processBlock(oscBuf, outputMidi);

        result.copyFrom(0, rendered, oscBuf, 0, 0, n);
        rendered += n;
    }

    // Compare against the reference sine wave at C4
    auto refPath = getReferencePath("osc_sine_c4.raw");
    if (!TestAudioHelpers::referenceExists(refPath)) {
        ASSERT_TRUE(TestAudioHelpers::saveReference(result, refPath)) << "Failed to save reference file: " << refPath;
        std::cout << "[  GOLDEN ] Created reference: " << refPath << std::endl;
    } else {
        auto ref = TestAudioHelpers::loadReference(refPath);
        ASSERT_EQ(ref.getNumSamples(), result.getNumSamples());
        float maxDiff = TestAudioHelpers::compareBuffers(result, ref);
        EXPECT_LT(maxDiff, 1e-5f) << "ExternalMidi -> Oscillator output drifted from reference (max diff: " << maxDiff
                                  << ")";
    }
}
