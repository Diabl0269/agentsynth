// FRO312: Oscillator's Unison/Detune CV inputs (raw ch14/15) were appended AFTER the poly shared
// mod-CV block, at the SAME raw channel numbers as the Audio R output block (kRightBase = 14) --
// see OscillatorModule.h's class-level channel-map comment. That is the exact "declared output
// count above every CV input index" pattern that already protects ch0 (Pitch CV vs Audio L) and
// the shared mod-CV block (ch8-13): a real juce::AudioProcessorGraph gives a node its own PRIVATE
// COPY of an input buffer that also fans out to another downstream consumer, whenever the node's
// own declared output-channel count is greater than that input's channel index (kNumOutputs = 22
// here, comfortably above 14/15) -- see RenderSequenceBuilder / isBufferNeededLater. Oscillator's
// own processBlock additionally caches ch14/15 (readUnisonDetuneCV) before it ever writes to those
// channels itself.
//
// This is a REGRESSION LOCK for that combined guarantee, built the same way
// Tests/Engine/IntegrationTests.cpp's PolyPad_EnvModulatesOutput_NotSilenced locks the analogous
// ch12 (Level CV) / VCA ch8 hazard: a real AudioProcessorGraph, one CV source (an ADSR, chosen for
// a predictable plateau value rather than an oscillating LFO) fanning out to BOTH Oscillator's
// Unison/Detune CV AND a plain reference tap wired straight to a graph output with nothing else in
// between -- exactly "another destination" for the same source buffer. If Oscillator's own
// output-clearing pass ever aliased and clobbered the shared source buffer (the pre-FRO314-style
// bug this guards against), the reference tap would read back zeroed/garbage samples instead of
// the ADSR's own plateaued value.
//
// Run twice (mono and poly oscillator voice mode) since Unison/Detune CV lands on the SAME raw
// channel in both (a global, not per-voice, parameter -- see the channel-map comment), so both
// voice modes exercise the identical aliasing hazard against a different processPolyMode/
// processMonoMode code path.

#include "Modules/ADSRModule.h"
#include "Modules/MidiKeyboardModule.h"
#include "Modules/OscillatorModule.h"

#include <cmath>
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int kBlockSize = 512;

float rmsOf(const float* data, int numSamples) {
    float sumSq = 0.0f;
    for (int i = 0; i < numSamples; ++i)
        sumSq += data[i] * data[i];
    return std::sqrt(sumSq / (float)numSamples);
}

// Runs the aliasing topology described in the file header for `poly` voice mode and returns the
// LAST block's reference tap / Audio L / Audio R buffers for the caller to assert on.
struct AliasingResult {
    float referenceTapLate; // ADSR ch0, read straight off a graph output -- "the other destination"
    std::vector<float> audioL;
    std::vector<float> audioR;
};

AliasingResult runUnisonDetuneCVAliasingScenario(bool poly) {
    using AudioGraphIOProcessor = juce::AudioProcessorGraph::AudioGraphIOProcessor;
    using NodePtr = juce::AudioProcessorGraph::Node::Ptr;

    juce::AudioProcessorGraph graph;
    // 3 outputs: ch0 = ADSR reference tap, ch1 = Oscillator Audio L, ch2 = Oscillator Audio R.
    graph.setPlayConfigDetails(0, 3, kSampleRate, kBlockSize);

    // This helper returns a value (AliasingResult), so gtest's ASSERT_* (which expands to a bare
    // `return;` on failure) cannot appear here -- EXPECT_NE plus an explicit early `return {}` gets
    // the same "never dereference a null we just failed to check" safety.
    NodePtr outNode = graph.addNode(std::make_unique<AudioGraphIOProcessor>(AudioGraphIOProcessor::audioOutputNode));
    EXPECT_NE(outNode, nullptr);
    if (outNode == nullptr)
        return {};

    NodePtr kbNode = graph.addNode(std::make_unique<MidiKeyboardModule>());
    EXPECT_NE(kbNode, nullptr);
    if (kbNode == nullptr)
        return {};

    auto adsrMod = std::make_unique<ADSRModule>("Unison/Detune CV Source");
    NodePtr adsrNode = graph.addNode(std::move(adsrMod));
    EXPECT_NE(adsrNode, nullptr);
    if (adsrNode == nullptr)
        return {};
    {
        auto* attack = findParameterByID(adsrNode->getProcessor(), "attack");
        auto* sustain = findParameterByID(adsrNode->getProcessor(), "sustain");
        EXPECT_NE(attack, nullptr);
        EXPECT_NE(sustain, nullptr);
        if (attack == nullptr || sustain == nullptr)
            return {};
        attack->setValueNotifyingHost(attack->convertTo0to1(0.05f));  // short attack: plateaus fast
        sustain->setValueNotifyingHost(sustain->convertTo0to1(1.0f)); // full sustain: stays at 1.0
    }

    NodePtr oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    EXPECT_NE(oscNode, nullptr);
    if (oscNode == nullptr)
        return {};
    {
        auto* polyParam = findParameterByID(oscNode->getProcessor(), "poly");
        auto* panParam = findParameterByID(oscNode->getProcessor(), "pan");
        EXPECT_NE(polyParam, nullptr);
        EXPECT_NE(panParam, nullptr);
        if (polyParam == nullptr || panParam == nullptr)
            return {};
        polyParam->setValueNotifyingHost(poly ? 1.0f : 0.0f);
        panParam->setValueNotifyingHost(panParam->convertTo0to1(0.0f)); // centred: R must equal L exactly
    }

    const int MIDI = juce::AudioProcessorGraph::midiChannelIndex;
    EXPECT_TRUE(graph.addConnection({{kbNode->nodeID, MIDI}, {adsrNode->nodeID, MIDI}}));
    EXPECT_TRUE(graph.addConnection({{kbNode->nodeID, MIDI}, {oscNode->nodeID, MIDI}}));

    // THE ALIASING TOPOLOGY: ADSR ch0 fans out to Oscillator's Unison AND Detune CV (ch14/15,
    // both aliasing the Audio R output block at kRightBase = 14) AND to a plain reference tap.
    EXPECT_TRUE(graph.addConnection({{adsrNode->nodeID, 0}, {oscNode->nodeID, OscillatorModule::kUnisonCVChannel}}));
    EXPECT_TRUE(graph.addConnection({{adsrNode->nodeID, 0}, {oscNode->nodeID, OscillatorModule::kDetuneCVChannel}}));
    EXPECT_TRUE(graph.addConnection({{adsrNode->nodeID, 0}, {outNode->nodeID, 0}}));

    EXPECT_TRUE(graph.addConnection({{oscNode->nodeID, 0}, {outNode->nodeID, 1}}));
    EXPECT_TRUE(graph.addConnection({{oscNode->nodeID, OscillatorModule::kRightBase}, {outNode->nodeID, 2}}));

    graph.prepareToPlay(kSampleRate, kBlockSize);

    // Note-on is delivered entirely through the keyboard module's own internal state (fanned to
    // ADSR/Oscillator via their MIDI connections above) -- the graph has no MidiInputNode, so a
    // MidiBuffer passed to graph.processBlock itself would never reach either of them.
    if (auto* kb = dynamic_cast<MidiKeyboardModule*>(kbNode->getProcessor()))
        kb->getKeyboardState().noteOn(1, 60, 1.0f);

    AliasingResult result;

    // Advance until the envelope plateaus at sustain (1.0): 0.05 s attack is well within 20 blocks
    // at 512/44100 ~= 0.23 s.
    for (int i = 0; i < 20; ++i) {
        juce::AudioBuffer<float> buf(3, kBlockSize);
        buf.clear();
        juce::MidiBuffer emptyMidi;
        graph.processBlock(buf, emptyMidi);
        if (i == 19) {
            result.referenceTapLate = buf.getReadPointer(0)[kBlockSize - 1];
            result.audioL.assign(buf.getReadPointer(1), buf.getReadPointer(1) + kBlockSize);
            result.audioR.assign(buf.getReadPointer(2), buf.getReadPointer(2) + kBlockSize);
        }
    }
    return result;
}

} // namespace

TEST(OscillatorUnisonDetuneCVAliasing, MonoModeReferenceTapAndAudioRSurviveTheSharedChannels) {
    const auto result = runUnisonDetuneCVAliasingScenario(/*poly*/ false);

    // KEY ASSERTION 1: the reference tap -- fed from the exact same ADSR output that also feeds
    // Oscillator's ch14/15 -- must show the plateaued envelope (~1.0), not zero/garbage. A
    // regression here (Oscillator's clear aliasing and clobbering the shared source buffer) would
    // silently zero this channel exactly like the pre-fix ch12/VCA-ch8 bug did.
    EXPECT_NEAR(result.referenceTapLate, 1.0f, 0.02f)
        << "reference tap read " << result.referenceTapLate
        << " -- the ADSR source feeding Oscillator's Unison/Detune CV appears to have been "
           "clobbered by Oscillator's own output-clearing pass (buffer-aliasing regression)";

    // KEY ASSERTION 2: Oscillator's own audio output is real (not silent, not NaN/Inf) --
    // Oscillator ran to completion and produced a genuine waveform despite the aliased channels.
    const float rmsL = rmsOf(result.audioL.data(), (int)result.audioL.size());
    EXPECT_GT(rmsL, 0.05f) << "Oscillator Audio L is near-silent (rms=" << rmsL << ")";
    for (float s : result.audioL)
        ASSERT_TRUE(std::isfinite(s)) << "Oscillator Audio L produced a non-finite sample";

    // KEY ASSERTION 3: at Pan = 0, Audio R must be a bit-identical copy of Audio L
    // (placeVoiceInStereo's documented guarantee) -- proves Audio R (kRightBase, the very channel
    // Unison/Detune CV alias) was rendered correctly, not corrupted by the aliasing.
    ASSERT_EQ(result.audioL.size(), result.audioR.size());
    for (size_t i = 0; i < result.audioL.size(); ++i)
        EXPECT_FLOAT_EQ(result.audioL[i], result.audioR[i]) << "Audio L/R diverged at sample " << i;
}

TEST(OscillatorUnisonDetuneCVAliasing, PolyModeReferenceTapAndAudioRSurviveTheSharedChannels) {
    const auto result = runUnisonDetuneCVAliasingScenario(/*poly*/ true);

    EXPECT_NEAR(result.referenceTapLate, 1.0f, 0.02f)
        << "reference tap read " << result.referenceTapLate
        << " -- the ADSR source feeding Oscillator's Unison/Detune CV appears to have been "
           "clobbered by Oscillator's own output-clearing pass (buffer-aliasing regression)";

    const float rmsL = rmsOf(result.audioL.data(), (int)result.audioL.size());
    EXPECT_GT(rmsL, 0.05f) << "Oscillator Audio L (voice 0) is near-silent (rms=" << rmsL << ")";
    for (float s : result.audioL)
        ASSERT_TRUE(std::isfinite(s)) << "Oscillator Audio L produced a non-finite sample";

    ASSERT_EQ(result.audioL.size(), result.audioR.size());
    for (size_t i = 0; i < result.audioL.size(); ++i)
        EXPECT_FLOAT_EQ(result.audioL[i], result.audioR[i]) << "Audio L/R diverged at sample " << i;
}

// The CV actually took effect: with Unison/Detune CV driven to their max (modulateNormalised maps
// a CV of 1.0 to the top of each parameter's own range: unison -> 8 voices, detune -> 100 cents),
// the rendered waveform should differ measurably from the same note with no CV connected at all
// (unison stuck at its default of 1, detune at 0) -- otherwise the aliasing fix could be hiding a
// SEPARATE bug where the CV reaches the channel but readUnisonDetuneCV silently no-ops.
TEST(OscillatorUnisonDetuneCVAliasing, MaxCVProducesADifferentWaveformThanNoCV) {
    const auto withCV = runUnisonDetuneCVAliasingScenario(/*poly*/ false);

    OscillatorModule bare;
    bare.prepareToPlay(kSampleRate, kBlockSize);
    juce::MidiBuffer noteOnMidi;
    noteOnMidi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);

    std::vector<float> lastBlock;
    juce::MidiBuffer emptyMidi;
    for (int i = 0; i < 20; ++i) {
        juce::AudioBuffer<float> buf(OscillatorModule::kNumOutputs, kBlockSize);
        buf.clear();
        bare.processBlock(buf, i == 0 ? noteOnMidi : emptyMidi); // no CV connected anywhere
        if (i == 19)
            lastBlock.assign(buf.getReadPointer(0), buf.getReadPointer(0) + kBlockSize);
    }

    ASSERT_EQ(lastBlock.size(), withCV.audioL.size());
    float maxAbsDiff = 0.0f;
    for (size_t i = 0; i < lastBlock.size(); ++i)
        maxAbsDiff = std::max(maxAbsDiff, std::abs(lastBlock[i] - withCV.audioL[i]));
    EXPECT_GT(maxAbsDiff, 0.01f) << "max|no-CV - CV| = " << maxAbsDiff
                                 << " -- Unison/Detune CV at max should audibly change the "
                                    "waveform (more unison voices, detuned), but it did not";
}
