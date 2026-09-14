#pragma once

// Shared constants, parameter helpers, render helpers and the AudioRenderingTest fixture for
// the AudioRendering test suite (Tests/Engine/AudioRendering/AudioRendering*Tests.cpp).
// Header-only; not compiled on its own and not registered in Tests/CMakeLists.txt.

#include "../../TestAudioHelpers.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/ADSRModule.h"
#include "Modules/ExternalMidiModule.h"
#include "Modules/FX/ChorusModule.h"
#include "Modules/FX/CompressorModule.h"
#include "Modules/FX/DelayModule.h"
#include "Modules/FX/DistortionModule.h"
#include "Modules/FX/FlangerModule.h"
#include "Modules/FX/LimiterModule.h"
#include "Modules/FX/PhaserModule.h"
#include "Modules/FX/ReverbModule.h"
#include "Modules/FX/RingModulatorModule.h"
#include "Modules/FilterModule.h"
#include "Modules/LFOModule.h"
#include "Modules/MidiKeyboardModule.h"
#include "Modules/OscillatorModule.h"
#include "Modules/PolyMidiModule.h"
#include "Modules/SequencerModule.h"
#include "Modules/VCAModule.h"
#include "PresetManager.h"
#include <bit>
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

// Tests/reference/, resolved from TESTS_ROOT_DIR (Tests/CMakeLists.txt) so this file's depth never matters.
juce::String getReferencePath(const juce::String& filename) {
    juce::File testsRoot(TESTS_ROOT_DIR);
    return testsRoot.getChildFile("reference").getChildFile(filename).getFullPathName();
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

} // namespace
