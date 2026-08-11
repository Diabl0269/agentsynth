#include "../Source/AudioEngine.h"
#include "../Source/Modules/ADSRModule.h"
#include "../Source/Modules/ExternalMidiModule.h"
#include "../Source/Modules/FX/ChorusModule.h"
#include "../Source/Modules/FX/CompressorModule.h"
#include "../Source/Modules/FX/DelayModule.h"
#include "../Source/Modules/FX/DistortionModule.h"
#include "../Source/Modules/FX/FlangerModule.h"
#include "../Source/Modules/FX/LimiterModule.h"
#include "../Source/Modules/FX/PhaserModule.h"
#include "../Source/Modules/FX/ReverbModule.h"
#include "../Source/Modules/FilterModule.h"
#include "../Source/Modules/LFOModule.h"
#include "../Source/Modules/MidiKeyboardModule.h"
#include "../Source/Modules/OscillatorModule.h"
#include "../Source/Modules/SequencerModule.h"
#include "../Source/Modules/VCAModule.h"
#include "../Source/PresetManager.h"
#include "TestAudioHelpers.h"
#include <gtest/gtest.h>

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int kBlockSize = 512;

// ---------------------------------------------------------------------------
// Helpers: set parameters via public JUCE API
// ---------------------------------------------------------------------------

void setParamValue(juce::AudioProcessor* proc, const juce::String& paramId, float denormalized) {
    for (auto* param : proc->getParameters()) {
        if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param))
            if (p->paramID == paramId)
                if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(param)) {
                    r->setValueNotifyingHost(r->getNormalisableRange().convertTo0to1(denormalized));
                    return;
                }
    }
}

void setChoiceParam(juce::AudioProcessor* proc, const juce::String& paramId, int index) {
    for (auto* param : proc->getParameters()) {
        if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param))
            if (p->paramID == paramId)
                if (auto* c = dynamic_cast<juce::AudioParameterChoice*>(param)) {
                    c->setValueNotifyingHost(float(index) / float(c->choices.size() - 1));
                    return;
                }
    }
}

// Get the path to the reference directory (Tests/reference/ relative to source tree)
juce::String getReferencePath(const juce::String& filename) {
    // __FILE__ gives us the path to this .cpp file in Tests/
    juce::File thisFile(juce::File(__FILE__));
    return thisFile.getParentDirectory().getChildFile("reference").getChildFile(filename).getFullPathName();
}

// ---------------------------------------------------------------------------
// Helper: process a module for N samples, accumulating output.
// Creates a properly sized buffer matching the module's channel count.
// MIDI is injected on the first block only.
// ---------------------------------------------------------------------------

juce::AudioBuffer<float> renderModule(juce::AudioProcessor& module, juce::MidiBuffer& midi, int totalSamples,
                                      int blockSize = kBlockSize) {
    int numCh = std::max(module.getTotalNumInputChannels(), module.getTotalNumOutputChannels());
    if (numCh == 0)
        numCh = 2;

    juce::AudioBuffer<float> result(numCh, totalSamples);
    result.clear();

    int rendered = 0;
    bool first = true;

    while (rendered < totalSamples) {
        int n = std::min(blockSize, totalSamples - rendered);
        juce::AudioBuffer<float> block(numCh, n);
        block.clear();

        if (first) {
            module.processBlock(block, midi);
            first = false;
        } else {
            juce::MidiBuffer empty;
            module.processBlock(block, empty);
        }

        for (int ch = 0; ch < numCh; ++ch)
            result.copyFrom(ch, rendered, block, ch, 0, n);
        rendered += n;
    }
    return result;
}

// Process a chain: module A produces audio/MIDI, then B consumes it.
// srcCh/dstCh control which channel to route between them.
// midiThrough = true forwards the same MIDI buffer.
juce::AudioBuffer<float> renderChainTwo(juce::AudioProcessor& a, juce::AudioProcessor& b, juce::MidiBuffer& midi,
                                        int totalSamples, int srcCh = 0, int dstCh = 0, bool midiThrough = false) {
    int aCh = std::max(a.getTotalNumInputChannels(), a.getTotalNumOutputChannels());
    int bCh = std::max(b.getTotalNumInputChannels(), b.getTotalNumOutputChannels());
    if (aCh == 0)
        aCh = 2;
    if (bCh == 0)
        bCh = 2;

    juce::AudioBuffer<float> result(bCh, totalSamples);
    result.clear();

    int rendered = 0;
    bool first = true;

    while (rendered < totalSamples) {
        int n = std::min(kBlockSize, totalSamples - rendered);

        juce::AudioBuffer<float> aBuf(aCh, n);
        aBuf.clear();
        juce::AudioBuffer<float> bBuf(bCh, n);
        bBuf.clear();

        juce::MidiBuffer midiForA;
        juce::MidiBuffer midiForB;
        if (first) {
            midiForA = midi;
            if (midiThrough)
                midiForB = midi;
            first = false;
        }

        a.processBlock(aBuf, midiForA);

        // Route audio: A's srcCh → B's dstCh
        bBuf.copyFrom(dstCh, 0, aBuf, srcCh, 0, n);

        // Forward MIDI if needed
        if (midiThrough || !midiForA.isEmpty()) {
            // Merge MIDI output from A into B's input
            for (const auto metadata : midiForA)
                midiForB.addEvent(metadata.getMessage(), metadata.samplePosition);
        }

        b.processBlock(bBuf, midiForB);

        for (int ch = 0; ch < bCh; ++ch)
            result.copyFrom(ch, rendered, bBuf, ch, 0, n);
        rendered += n;
    }
    return result;
}

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

class AudioRenderingTest : public ::testing::Test {
protected:
    void prepareModule(juce::AudioProcessor& m) { m.prepareToPlay(kSampleRate, kBlockSize); }
};

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

// ===========================================================================
// Test 8: All factory presets render non-silent
// Uses the graph via AudioEngine + PresetManager (presets are graph-based).
// Injects MIDI via MidiKeyboardModule (same as the real app).
// ===========================================================================
TEST_F(AudioRenderingTest, AllPresetsRenderNonSilent) {
    // Configure graph headlessly — do NOT call engine.initialise() as it opens the audio
    // device and routes processed audio to speakers.
    AudioEngine engine;
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
    graph.prepareToPlay(kSampleRate, kBlockSize);

    auto presetNames = synth::PresetManager::getPresetNames();

    for (int i = 0; i < presetNames.size(); ++i) {
        SCOPED_TRACE(presetNames[i].toStdString());

        synth::PresetManager::loadPreset(i, graph);
        graph.prepareToPlay(kSampleRate, kBlockSize);

        // Find the MidiKeyboardModule in the preset and inject a note
        for (auto* node : graph.getNodes()) {
            if (auto* kb = dynamic_cast<MidiKeyboardModule*>(node->getProcessor())) {
                kb->getKeyboardState().noteOn(1, 60, 1.0f);
                break;
            }
        }

        // Render 1 second
        int totalSamples = static_cast<int>(kSampleRate);
        juce::AudioBuffer<float> result(2, totalSamples);
        result.clear();
        int rendered = 0;
        while (rendered < totalSamples) {
            int n = std::min(kBlockSize, totalSamples - rendered);
            juce::AudioBuffer<float> block(2, n);
            block.clear();
            juce::MidiBuffer emptyMidi;
            graph.processBlock(block, emptyMidi);
            result.copyFrom(0, rendered, block, 0, 0, n);
            if (block.getNumChannels() > 1)
                result.copyFrom(1, rendered, block, 1, 0, n);
            rendered += n;
        }

        // Presets with sequencer should produce sound even without keyboard
        // Some presets might not have a keyboard module, but sequencer runs automatically
        // We just check that at least something comes out
        bool hasMidi = false;
        for (auto* node : graph.getNodes()) {
            if (dynamic_cast<MidiKeyboardModule*>(node->getProcessor()) ||
                dynamic_cast<SequencerModule*>(node->getProcessor())) {
                hasMidi = true;
                break;
            }
        }
        if (hasMidi) {
            EXPECT_FALSE(TestAudioHelpers::isSilent(result, 0))
                << "Preset '" << presetNames[i].toStdString() << "' rendered silent audio";
        }
    }
}

// ===========================================================================
// Test 8b: the "Poly Pad" preset renders identically twice (issue #198)
//
// Pushes an over-full chord through the real preset graph (MIDI Keyboard -> Poly MIDI -> 8-voice
// Oscillator -> Filter -> VCA -> Reverb) — the path an offline bounce or a golden-render regression
// would take — and requires the whole chain to be bit-reproducible.
//
// Scope, deliberately: this is a *forward* guard, not a detector for the original bug. Checked by
// experiment — with voice age restored to juce::Time::getMillisecondCounter() this test still
// passes 8/8, because every note here lands at one sample offset, so the buggy code degenerates
// *consistently* (always voice 0) and both runs agree. What actually catches wall-clock stamping
// is PolyMidiModuleTest.SameBlockNotesStealInArrivalOrder / OverfullChordInOneBlock…, which pin
// *which* voice is stolen. This test's job is to fail if anything in the poly chain later
// introduces nondeterminism (an unseeded PRNG, a clock read, uninitialised state).
// ===========================================================================
juce::AudioBuffer<float> renderPolyPadChord(int numNotes, int numBlocks) {
    AudioEngine engine; // do NOT initialise() — that would open a real audio device
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
    synth::PresetManager::loadPreset(6, graph);
    graph.prepareToPlay(kSampleRate, kBlockSize);

    for (auto* node : graph.getNodes())
        if (auto* kb = dynamic_cast<MidiKeyboardModule*>(node->getProcessor()))
            for (int i = 0; i < numNotes; ++i)
                kb->getKeyboardState().noteOn(1, 60 + i, 1.0f);

    juce::AudioBuffer<float> result(2, numBlocks * kBlockSize);
    result.clear();
    for (int b = 0; b < numBlocks; ++b) {
        juce::AudioBuffer<float> block(2, kBlockSize);
        block.clear();
        juce::MidiBuffer emptyMidi;
        graph.processBlock(block, emptyMidi);
        for (int ch = 0; ch < 2; ++ch)
            result.copyFrom(ch, b * kBlockSize, block, ch, 0, kBlockSize);
    }
    return result;
}

TEST_F(AudioRenderingTest, PolyPadPresetRendersIdenticallyTwice) {
    ASSERT_EQ(synth::PresetManager::getPresetNames()[6], "Poly Pad") << "preset index 6 moved";

    const auto first = renderPolyPadChord(12, 20); // 12 notes into 8 voices — 4 steals
    juce::Thread::sleep(3);                        // a wall-clock stamp would tick over here
    const auto second = renderPolyPadChord(12, 20);

    ASSERT_EQ(first.getNumSamples(), second.getNumSamples());
    EXPECT_FALSE(TestAudioHelpers::isSilent(first, 0)) << "nothing was rendered, so nothing is proven";
    EXPECT_EQ(TestAudioHelpers::compareBuffers(first, second), 0.0f)
        << "the same chord must render bit-identically — voice stealing may not depend on wall-clock time";
}

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

} // namespace
