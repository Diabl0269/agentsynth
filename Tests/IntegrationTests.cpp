#include "AI/AIStateMapper.h"
#include "AudioEngine.h"
#include "Modules/ADSRModule.h"
#include "Modules/FilterModule.h"
#include "Modules/LFOModule.h"
#include "Modules/MidiKeyboardModule.h"
#include "Modules/OscillatorModule.h"
#include "Modules/VCAModule.h"
#include "PresetManager.h"
#include <cmath>
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>

class IntegrationTest : public ::testing::Test {
protected:
    void SetUp() override { engine.initialise(); }

    AudioEngine engine;
};

TEST_F(IntegrationTest, OscToFilterToVCA) {
    auto& graph = engine.getGraph();
    graph.clear();

    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    auto vcaNode = graph.addNode(std::make_unique<VCAModule>());

    ASSERT_NE(oscNode, nullptr);
    ASSERT_NE(filterNode, nullptr);
    ASSERT_NE(vcaNode, nullptr);

    // Osc out 0 -> Filter in 0
    EXPECT_TRUE(graph.addConnection({{oscNode->nodeID, 0}, {filterNode->nodeID, 0}}));
    // Filter out 0 -> VCA in 0
    EXPECT_TRUE(graph.addConnection({{filterNode->nodeID, 0}, {vcaNode->nodeID, 0}}));
}

TEST_F(IntegrationTest, LFOModulatesFilterCutoff) {
    auto& graph = engine.getGraph();
    graph.clear();

    auto lfoNode = graph.addNode(std::make_unique<LFOModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());

    ASSERT_NE(lfoNode, nullptr);
    ASSERT_NE(filterNode, nullptr);

    engine.addModRouting(lfoNode->nodeID, 0, filterNode->nodeID, 1);

    auto routings = engine.getActiveModRoutings();
    ASSERT_EQ(routings.size(), 1);
    EXPECT_EQ(routings[0].sourceNodeID, lfoNode->nodeID);
    EXPECT_EQ(routings[0].destNodeID, filterNode->nodeID);
    EXPECT_EQ(routings[0].destChannelIndex, 1);
}

TEST_F(IntegrationTest, ADSREnvelopeToVCA) {
    auto& graph = engine.getGraph();
    graph.clear();

    auto adsrNode = graph.addNode(std::make_unique<ADSRModule>());
    auto vcaNode = graph.addNode(std::make_unique<VCAModule>());

    ASSERT_NE(adsrNode, nullptr);
    ASSERT_NE(vcaNode, nullptr);

    // ADSR out 0 -> VCA CV in (channel 1)
    EXPECT_TRUE(graph.addConnection({{adsrNode->nodeID, 0}, {vcaNode->nodeID, 1}}));
}

TEST_F(IntegrationTest, MultipleModulationsOnSameTarget) {
    auto& graph = engine.getGraph();
    graph.clear();

    auto lfo1Node = graph.addNode(std::make_unique<LFOModule>());
    auto lfo2Node = graph.addNode(std::make_unique<LFOModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());

    ASSERT_NE(lfo1Node, nullptr);
    ASSERT_NE(lfo2Node, nullptr);
    ASSERT_NE(filterNode, nullptr);

    engine.addModRouting(lfo1Node->nodeID, 0, filterNode->nodeID, 1);
    engine.addModRouting(lfo2Node->nodeID, 0, filterNode->nodeID, 1);

    auto routings = engine.getActiveModRoutings();
    ASSERT_EQ(routings.size(), 2);
}

TEST_F(IntegrationTest, PresetLoadRestoresModRoutings) {
    auto& graph = engine.getGraph();
    ASSERT_TRUE(synth::PresetManager::loadDefaultPreset(graph));

    // The default preset has Attenuverter nodes for mod routing
    bool hasAttenuverter = false;
    for (auto* node : graph.getNodes()) {
        if (node->getProcessor()->getName().contains("Attenuverter") ||
            node->getProcessor()->getName().contains("Mod Slot")) {
            hasAttenuverter = true;
            break;
        }
    }
    EXPECT_TRUE(hasAttenuverter) << "Default preset should contain Attenuverter nodes for modulation routing";

    // Verify connections exist
    EXPECT_GT(graph.getConnections().size(), 0);
}

TEST_F(IntegrationTest, SaveLoadRoundTripPreservesState) {
    auto& graph = engine.getGraph();

    // Load a preset
    ASSERT_TRUE(synth::PresetManager::loadPreset(1, graph)); // Simple Lead

    int originalNodeCount = graph.getNumNodes();
    int originalConnectionCount = (int)graph.getConnections().size();
    ASSERT_GT(originalNodeCount, 0);
    ASSERT_GT(originalConnectionCount, 0);

    // Serialize to JSON
    auto json = synth::AIStateMapper::graphToJSON(graph);

    // Round-trip: reload into the same prepared graph (fresh graphs lack IO channel config).
    // trusted=true because this IS our own graphToJSON output — the untrusted apply path is for
    // model-authored patches and would both rescale exact values and refuse the preset's internal
    // Attenuverter nodes. Every real caller replaying a save (PresetManager, undo, session state)
    // applies trusted; see docs/layout.md §12.5.
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, graph, /*clearExisting=*/true, /*trusted=*/true));

    // Verify node and connection counts match
    EXPECT_EQ(graph.getNumNodes(), originalNodeCount);
    EXPECT_EQ((int)graph.getConnections().size(), originalConnectionCount);

    // Verify node types match
    for (int i = 0; i < originalNodeCount; ++i) {
        auto origNode = graph.getNodes().getUnchecked(i);
        EXPECT_FALSE(origNode->getProcessor()->getName().isEmpty()) << "Node at index " << i << " has empty name";
    }
}

TEST_F(IntegrationTest, AllPresetsLoadAndHaveSignalPath) {
    auto& graph = engine.getGraph();
    auto presetNames = synth::PresetManager::getPresetNames();

    for (int i = 0; i < presetNames.size(); ++i) {
        graph.clear();
        ASSERT_TRUE(synth::PresetManager::loadPreset(i, graph))
            << "Failed to load preset: " << presetNames[i].toStdString();

        // Every preset should have an audio output
        bool hasAudioOutput = false;
        for (auto* node : graph.getNodes()) {
            if (node->getProcessor()->getName() == "Audio Output")
                hasAudioOutput = true;
        }
        EXPECT_TRUE(hasAudioOutput) << "Preset '" << presetNames[i].toStdString() << "' missing Audio Output";

        // Every preset should have connections
        EXPECT_GT(graph.getConnections().size(), 0)
            << "Preset '" << presetNames[i].toStdString() << "' has no connections";
    }
}

// ---------------------------------------------------------------------------
// PolyPad_EnvToOscModulatesOscLevel
//
// Proves that ADSR envelope output fed into Oscillator channel 12 (Level CV)
// raises oscillator output amplitude as the envelope rises.
//
// Strategy:
//   1. Create an ADSRModule (mono, so MIDI note-on drives it) + OscillatorModule
//      (mono mode, Sine wave, base level = 0.5).
//   2. Process them manually: ADSR output is written into the oscillator's
//      ch12 before each processBlock call, exactly as the JUCE graph would do
//      when the connection {"src": Amp Env, "srcPort": 0, "dst": Osc, "dstPort": 12}
//      is present.
//   3. Measure output RMS during an early block (env still rising from near 0)
//      versus a later block (env has risen substantially).
//   4. Assert late-block RMS > early-block RMS with a clear margin (2x).
//
//  Attack is set to 0.5s so there is a long, measurable ramp across many blocks.
//  Base level is set to 0.0 (purely CV-driven) to make the effect unambiguous.
// ---------------------------------------------------------------------------
TEST(PolyPad_EnvToOscModulatesOscLevel, RisingEnvelopeRaisesOscillatorLevel) {
    constexpr double kSampleRate = 44100.0;
    constexpr int kBlockSize = 512;

    // --- Setup ADSR (mono) ---
    ADSRModule adsr;
    adsr.prepareToPlay(kSampleRate, kBlockSize);

    // Long attack (0.5s) so the ramp is clearly visible across blocks.
    // Sustain = 1.0, decay/release don't matter for this test.
    // Parameter indices (ModuleBase adds bypassed at 0, then ADSR adds attack=1, decay=2, sustain=3, release=4, poly=5)
    auto* attackParam = dynamic_cast<juce::AudioParameterFloat*>(adsr.getParameters()[1]);
    auto* sustainParam = dynamic_cast<juce::AudioParameterFloat*>(adsr.getParameters()[3]);
    ASSERT_NE(attackParam, nullptr);
    ASSERT_NE(sustainParam, nullptr);
    *attackParam = 0.5f;
    *sustainParam = 1.0f;

    // --- Setup Oscillator in POLY mode (ch12 = Level CV in poly mode), Saw wave ---
    OscillatorModule osc;
    osc.prepareToPlay(kSampleRate, kBlockSize);

    // Parameter indices (ModuleBase adds bypassed at 0):
    //   0=bypassed, 1=waveform, 2=octave, 3=coarse, 4=fine, 5=level, 6=poly, 7=unison, 8=detune, 9=muted
    auto* levelParam = dynamic_cast<juce::AudioParameterFloat*>(osc.getParameters()[5]);
    auto* polyParam = dynamic_cast<juce::AudioParameterBool*>(osc.getParameters()[6]);
    ASSERT_NE(levelParam, nullptr);
    ASSERT_NE(polyParam, nullptr);
    *levelParam = 0.0f;                     // base level zero; output is purely CV-driven
    polyParam->setValueNotifyingHost(1.0f); // poly mode: Level CV = ch12

    // In poly mode, voice 0 uses MIDI fallback (lastMidiNote) when pitch CV < 20 Hz.
    // Prime lastMidiNote via a mono-mode processBlock with noteOn before switching to poly.
    {
        juce::MidiBuffer noteOn;
        noteOn.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
        // Temporarily process in mono mode (poly not yet true in the param flush);
        // the note-on is captured in voices[0].lastMidiNote regardless of poly flag.
        juce::AudioBuffer<float> primeBuf(14, kBlockSize);
        primeBuf.clear();
        osc.processBlock(primeBuf, noteOn);
    }

    // --- Trigger ADSR with a note-on ---
    juce::MidiBuffer noteOnMidi;
    noteOnMidi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);

    // Helper: run one block through ADSR (mono) and Oscillator (poly), routing ADSR ch0 → Osc ch12.
    // Returns the RMS of voice 0 audio output (ch0).
    auto runBlock = [&](juce::MidiBuffer& midiForAdsr) -> float {
        // ADSR: mono mode, fill gate with 1.0, apply envelope to ch0
        juce::AudioBuffer<float> adsrBuf(8, kBlockSize);
        for (int ch = 0; ch < 8; ++ch)
            juce::FloatVectorOperations::fill(adsrBuf.getWritePointer(ch), 1.0f, kBlockSize);
        adsr.processBlock(adsrBuf, midiForAdsr);
        // adsrBuf.ch0 = envelope [0..1] rising during attack

        // Oscillator: 14 channels — ch0 = voice 0 pitch CV (0 means MIDI fallback), ch12 = Level CV
        juce::AudioBuffer<float> oscBuf(14, kBlockSize);
        oscBuf.clear();
        // ch0 = 0.0 Hz → poly voice 0 uses MIDI fallback (note 60 → ~261 Hz)
        juce::FloatVectorOperations::copy(oscBuf.getWritePointer(12), adsrBuf.getReadPointer(0), kBlockSize);
        juce::MidiBuffer emptyMidi;
        osc.processBlock(oscBuf, emptyMidi);

        // Compute RMS of voice 0 audio output (ch0 in poly mode)
        const float* out = oscBuf.getReadPointer(0);
        float sumSq = 0.0f;
        for (int i = 0; i < kBlockSize; ++i)
            sumSq += out[i] * out[i];
        return std::sqrt(sumSq / static_cast<float>(kBlockSize));
    };

    // Block 1 (immediately after note-on): env is near zero, expect very low RMS
    float earlyRMS = runBlock(noteOnMidi);

    // Advance ~0.4s (34 blocks * 512 / 44100 ≈ 0.39s) — env has risen substantially
    juce::MidiBuffer emptyMidi;
    for (int i = 0; i < 34; ++i)
        runBlock(emptyMidi);

    // Block at ~0.4s into attack: env should be around 0.8, osc level much higher
    float lateRMS = runBlock(emptyMidi);

    // Sanity: early RMS should be small (envelope near 0, so near-silent output)
    EXPECT_LT(earlyRMS, 0.05f) << "earlyRMS=" << earlyRMS << " — expected near-silence at env start";

    // Key assertion: rising envelope raises oscillator output clearly (at least 5x louder)
    EXPECT_GT(lateRMS, earlyRMS * 5.0f) << "lateRMS=" << lateRMS << " earlyRMS=" << earlyRMS
                                        << " — rising envelope should raise oscillator level significantly";

    // Also assert lateRMS is non-trivial (env has actually produced audio)
    EXPECT_GT(lateRMS, 0.1f) << "lateRMS=" << lateRMS << " — expected audible signal after envelope rises";
}

// ---------------------------------------------------------------------------
// PolyPad_EnvModulatesOutput_NotSilenced
//
// REGRESSION LOCK: Guards against the JUCE AudioProcessorGraph buffer-aliasing
// hazard that was fixed by giving OscillatorModule 14 output channels.
//
// THE BUG (now fixed):
//   Preset "Poly Pad" routes ADSR(8) output 0 to BOTH:
//     (a) OscillatorModule(5) input 12  [Level CV]
//     (b) VCAModule(7)        input 8   [voice-0 amplitude CV]
//
//   When OscillatorModule had only 8 output channels, JUCE's render graph saw
//   that input channel 12 >= numOuts (8) and reused the ADSR output buffer for
//   Oscillator's input 12 (no copy / aliased pointer).
//
//   Oscillator's processBlock (poly mode) then called:
//     buffer.clear(ch, 0, numSamples)  for ch = 0 .. numOutputs-1
//   With 14 outputs this loop runs ch 0..13, clearing the aliased ch12 buffer —
//   which IS the ADSR output buffer.  VCA then read 0.0 from its input 8,
//   multiplied every voice by 0, and the graph output went silent.
//
//   The fix: declare 14 output channels.  JUCE now allocates a unique buffer for
//   ch12, so zeroing it does NOT destroy the ADSR source buffer.
//
// HOW TO VERIFY THE TEST CATCHES A REGRESSION:
//   Temporarily change OscillatorModule("Oscillator", 14, 14) back to (14, 8),
//   rebuild, and run this test — it must FAIL (lateRMS ≈ 0 with aliasing).
//   Restore to 14,14, rebuild → test passes.
//
// APPROACH: Minimal hand-built graph that exactly replicates the two-consumer
// aliasing topology (a single ADSR source feeding both Osc.in12 and VCA.in8).
// Using a full JUCE AudioProcessorGraph so JUCE's buffer-allocation logic runs.
// ---------------------------------------------------------------------------
TEST(PolyPad_EnvModulatesOutput_NotSilenced, EnvToVcaNotZeroedByOscillatorClear) {
    constexpr double kSampleRate = 44100.0;
    constexpr int kBlockSize = 512;

    // Build a minimal graph that reproduces the aliasing topology.
    // Graph I/O: 0 inputs, 2 outputs (stereo, matching VCA poly sum on ch0/ch1).
    using AudioGraphIOProcessor = juce::AudioProcessorGraph::AudioGraphIOProcessor;
    using NodePtr = juce::AudioProcessorGraph::Node::Ptr;
    juce::AudioProcessorGraph graph;

    // Configure play settings BEFORE adding connections — the Audio Output node's
    // input channel count is determined by the graph's output channel count.
    graph.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);

    // Add IO nodes
    NodePtr outNode = graph.addNode(std::make_unique<AudioGraphIOProcessor>(AudioGraphIOProcessor::audioOutputNode));
    ASSERT_NE(outNode, nullptr);

    // Add a MidiKeyboard so we can trigger MIDI note-on through the graph
    NodePtr kbNode = graph.addNode(std::make_unique<MidiKeyboardModule>());
    ASSERT_NE(kbNode, nullptr);

    // Add ADSR (mono: responds to MIDI note-on, produces rising envelope on ch0)
    auto adsrMod = std::make_unique<ADSRModule>("Amp Env");
    // Short attack (0.1 s) so envelope rises within ~9 blocks at 512/44100 ≈ 0.094 s
    // Use parameter setters after adding so prepareToPlay inherits them.
    NodePtr adsrNode = graph.addNode(std::move(adsrMod));
    ASSERT_NE(adsrNode, nullptr);
    {
        // Parameters: 0=bypassed, 1=attack, 2=decay, 3=sustain, 4=release, 5=poly, 6=muted
        auto* aParam = dynamic_cast<juce::AudioParameterFloat*>(adsrNode->getProcessor()->getParameters()[1]);
        auto* sParam = dynamic_cast<juce::AudioParameterFloat*>(adsrNode->getProcessor()->getParameters()[3]);
        ASSERT_NE(aParam, nullptr);
        ASSERT_NE(sParam, nullptr);
        *aParam = 0.1f; // 0.1 s attack
        *sParam = 1.0f; // full sustain so envelope holds up during late blocks
    }

    // Add Oscillator in POLY mode — this is the "hazardous" consumer of ADSR ch0.
    // Its Level CV input is channel 12. Having 14 output channels prevents aliasing.
    auto oscMod = std::make_unique<OscillatorModule>();
    NodePtr oscNode = graph.addNode(std::move(oscMod));
    ASSERT_NE(oscNode, nullptr);
    {
        // Parameters: 0=bypassed, 1=waveform, 2=octave, 3=coarse, 4=fine, 5=level, 6=poly, 7=unison, 8=detune, 9=muted
        auto* polyParam = dynamic_cast<juce::AudioParameterBool*>(oscNode->getProcessor()->getParameters()[6]);
        auto* levelParam = dynamic_cast<juce::AudioParameterFloat*>(oscNode->getProcessor()->getParameters()[5]);
        ASSERT_NE(polyParam, nullptr);
        ASSERT_NE(levelParam, nullptr);
        polyParam->setValueNotifyingHost(1.0f); // poly mode: Level CV = input 12
        *levelParam = 0.7f;
    }

    // Add VCA in POLY mode — voice 0 amplitude CV is channel 8 (the aliasing victim).
    // If Osc clears the shared ADSR buffer (ch12 aliased), VCA.in8 becomes 0 and output is silent.
    auto vcaMod = std::make_unique<VCAModule>();
    NodePtr vcaNode = graph.addNode(std::move(vcaMod));
    ASSERT_NE(vcaNode, nullptr);
    {
        // Parameters: 0=bypassed, 1=gain, 2=poly, 3=muted
        auto* polyParam = dynamic_cast<juce::AudioParameterBool*>(vcaNode->getProcessor()->getParameters()[2]);
        auto* gainParam = dynamic_cast<juce::AudioParameterFloat*>(vcaNode->getProcessor()->getParameters()[1]);
        ASSERT_NE(polyParam, nullptr);
        ASSERT_NE(gainParam, nullptr);
        polyParam->setValueNotifyingHost(1.0f); // poly mode
        *gainParam = 0.8f;
    }

    // --- Connections ---
    // MIDI: keyboard -> ADSR (so note-on triggers envelope) and -> Oscillator (voice 0 MIDI fallback)
    const int MIDI = juce::AudioProcessorGraph::midiChannelIndex;
    EXPECT_TRUE(graph.addConnection({{kbNode->nodeID, MIDI}, {adsrNode->nodeID, MIDI}}));
    EXPECT_TRUE(graph.addConnection({{kbNode->nodeID, MIDI}, {oscNode->nodeID, MIDI}}));

    // Audio: Oscillator poly voice 0 output (ch0) -> VCA poly audio input voice 0 (ch0)
    EXPECT_TRUE(graph.addConnection({{oscNode->nodeID, 0}, {vcaNode->nodeID, 0}}));

    // THE ALIASING TOPOLOGY: ADSR ch0 fans out to BOTH Osc ch12 AND VCA ch8.
    // With Osc having only 8 outputs, JUCE would alias the ADSR buffer to Osc.in12.
    // Osc's processBlock clears ch0..numOuts-1, clobbering the ADSR buffer, so VCA reads 0.
    // With 14 outputs, JUCE gives Osc.in12 its own buffer — aliasing cannot occur.
    EXPECT_TRUE(graph.addConnection({{adsrNode->nodeID, 0}, {oscNode->nodeID, 12}})); // ADSR -> Osc Level CV
    EXPECT_TRUE(graph.addConnection({{adsrNode->nodeID, 0}, {vcaNode->nodeID, 8}}));  // ADSR -> VCA voice-0 CV

    // VCA stereo output -> graph audio output
    EXPECT_TRUE(graph.addConnection({{vcaNode->nodeID, 0}, {outNode->nodeID, 0}}));
    EXPECT_TRUE(graph.addConnection({{vcaNode->nodeID, 1}, {outNode->nodeID, 1}}));

    // --- Prepare ---
    // (setPlayConfigDetails was already called above, before connections were added)
    graph.prepareToPlay(kSampleRate, kBlockSize);

    // Trigger note-on via the keyboard module's state (same pattern as AudioRenderingTests)
    if (auto* kb = dynamic_cast<MidiKeyboardModule*>(kbNode->getProcessor()))
        kb->getKeyboardState().noteOn(1, 60, 1.0f);

    // --- Process blocks ---
    // Block 1 (note-on just fired): envelope still near zero, expect near-silence
    {
        juce::AudioBuffer<float> buf(2, kBlockSize);
        buf.clear();
        juce::MidiBuffer emptyMidi;
        graph.processBlock(buf, emptyMidi);
    }

    // Advance ~0.15 s (13 blocks) — attack (0.1 s) completes, envelope at full sustain (1.0)
    for (int i = 0; i < 12; ++i) {
        juce::AudioBuffer<float> buf(2, kBlockSize);
        buf.clear();
        juce::MidiBuffer emptyMidi;
        graph.processBlock(buf, emptyMidi);
    }

    // Measure the LATE block (envelope should be fully risen)
    juce::AudioBuffer<float> lateBuf(2, kBlockSize);
    lateBuf.clear();
    {
        juce::MidiBuffer emptyMidi;
        graph.processBlock(lateBuf, emptyMidi);
    }

    float sumSq = 0.0f;
    const float* outData = lateBuf.getReadPointer(0);
    for (int i = 0; i < kBlockSize; ++i)
        sumSq += outData[i] * outData[i];
    float lateRMS = std::sqrt(sumSq / static_cast<float>(kBlockSize));

    // KEY ASSERTION: if the aliasing bug is present, lateRMS ≈ 0 (VCA multiplies by 0).
    // With the fix (14 output channels), ADSR buffer survives Osc.processBlock and
    // VCA reads a real envelope value (~1.0), producing audible output.
    EXPECT_GT(lateRMS, 0.01f) << "lateRMS=" << lateRMS
                              << " — VCA output is near-silent; the ADSR→VCA envelope buffer may have been "
                                 "zeroed by Oscillator's processBlock (buffer-aliasing regression). "
                                 "Check OscillatorModule output channel count (must be 14, not 8).";
}
