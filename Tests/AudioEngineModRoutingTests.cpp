#include "../Source/AudioEngine.h"
#include "../Source/Modules/ADSRModule.h"
#include "../Source/Modules/AttenuverterModule.h"
#include "../Source/Modules/LFOModule.h"
#include "../Source/Modules/OscillatorModule.h"
#include "../Source/Modules/PolyMidiModule.h"
#include "../Source/Modules/VCAModule.h"
#include "../Source/PresetManager.h"
#include <gtest/gtest.h>

class AudioEngineModRoutingTests : public ::testing::Test {
protected:
    void SetUp() override {
        engine.initialise();
        engine.getGraph().clear();
    }

    void TearDown() override { engine.shutdown(); }

    AudioEngine engine;
};

TEST_F(AudioEngineModRoutingTests, GetModulationRoutings_AttenuverterChainSurfaces) {
    auto& graph = engine.getGraph();

    auto lfoNode = graph.addNode(std::make_unique<LFOModule>());
    auto vcaNode = graph.addNode(std::make_unique<VCAModule>());
    ASSERT_NE(lfoNode, nullptr);
    ASSERT_NE(vcaNode, nullptr);

    // Wire LFO -> atten -> VCA.ch1 (mirror AudioEngine::addModRouting)
    engine.addModRouting(lfoNode->nodeID, 0, vcaNode->nodeID, 1);

    auto routings = engine.getModulationRoutings();
    ASSERT_EQ(routings.size(), 1u);

    const auto& r = routings[0];
    EXPECT_EQ(r.kind, AudioEngine::RoutingKind::AttenuverterChain);
    EXPECT_EQ(r.voiceCount, 1);
    EXPECT_TRUE(r.hasSource);
    EXPECT_TRUE(r.hasDest);
    EXPECT_EQ(r.sourceNodeID, lfoNode->nodeID);
    EXPECT_EQ(r.destNodeID, vcaNode->nodeID);
    EXPECT_EQ(r.destChannelIndex, 1);
}

TEST_F(AudioEngineModRoutingTests, PolyEnvToVCA_CollapsesToOneRouting) {
    auto& graph = engine.getGraph();
    graph.clear();
    bool loaded = synth::PresetManager::loadPreset(6, graph);
    ASSERT_TRUE(loaded) << "Preset 6 (Poly Pad) failed to load";

    auto routings = engine.getModulationRoutings();

    // Now that Pitch and Gate fans are also collapsed into PolyBus routings,
    // filter to only the ModCV PolyBus (the ADSR->VCA envelope fan).
    int modCVPolyBusCount = 0;
    AudioEngine::ModulationRouting modCVPolyBusRouting;
    for (const auto& r : routings) {
        if (r.kind == AudioEngine::RoutingKind::PolyBus && r.role == PortRole::ModCV) {
            ++modCVPolyBusCount;
            modCVPolyBusRouting = r;
        }
    }
    EXPECT_EQ(modCVPolyBusCount, 1) << "Expected exactly 1 PolyBus routing with role==ModCV";
    EXPECT_EQ(modCVPolyBusRouting.voiceCount, 8) << "Expected voiceCount == 8 for ADSR->VCA poly bus";

    // Find ADSR (Amp Env) and VCA nodes by type
    juce::AudioProcessorGraph::NodeID adsrNodeID, vcaNodeID;
    bool foundAdsr = false, foundVca = false;
    for (auto* node : graph.getNodes()) {
        if (dynamic_cast<ADSRModule*>(node->getProcessor()) && !foundAdsr) {
            adsrNodeID = node->nodeID;
            foundAdsr = true;
        }
        if (dynamic_cast<VCAModule*>(node->getProcessor()) && !foundVca) {
            vcaNodeID = node->nodeID;
            foundVca = true;
        }
    }
    ASSERT_TRUE(foundAdsr) << "ADSR (Amp Env) node not found in graph";
    ASSERT_TRUE(foundVca) << "VCA node not found in graph";

    EXPECT_EQ(modCVPolyBusRouting.sourceNodeID, adsrNodeID) << "PolyBus routing source should be ADSR";
    EXPECT_EQ(modCVPolyBusRouting.destNodeID, vcaNodeID) << "PolyBus routing dest should be VCA";
    EXPECT_EQ(modCVPolyBusRouting.destChannelIndex, 8)
        << "PolyBus routing dest channel should be 8 (start of VCA ModCV fan)";
}

TEST_F(AudioEngineModRoutingTests, NoDoubleCountAttenuverterPlusDirect) {
    auto& graph = engine.getGraph();
    graph.clear();

    auto lfoNode = graph.addNode(std::make_unique<LFOModule>());
    auto vcaNode = graph.addNode(std::make_unique<VCAModule>());
    ASSERT_NE(lfoNode, nullptr);
    ASSERT_NE(vcaNode, nullptr);

    // Wire LFO -> atten -> VCA.ch1 (standard attenuverter chain)
    engine.addModRouting(lfoNode->nodeID, 0, vcaNode->nodeID, 1);

    auto routings = engine.getModulationRoutings();

    // Should be exactly 1 routing total (the attenuverter chain)
    EXPECT_EQ(routings.size(), 1u)
        << "Expected exactly 1 routing; attenuverter connections must not also appear as DirectCV";

    EXPECT_EQ(routings[0].kind, AudioEngine::RoutingKind::AttenuverterChain)
        << "The single routing should be AttenuverterChain";

    int directOrPolyCount = 0;
    for (const auto& r : routings) {
        if (r.kind == AudioEngine::RoutingKind::DirectCV || r.kind == AudioEngine::RoutingKind::PolyBus)
            ++directOrPolyCount;
    }
    EXPECT_EQ(directOrPolyCount, 0) << "Attenuverter-mediated connections must not appear as DirectCV/PolyBus";
}

TEST_F(AudioEngineModRoutingTests, PolyPitchAndGateFansCollapseToBuses) {
    auto& graph = engine.getGraph();
    graph.clear();
    bool loaded = synth::PresetManager::loadPreset(6, graph);
    ASSERT_TRUE(loaded) << "Preset 6 (Poly Pad) failed to load";

    auto routings = engine.getModulationRoutings();

    // Find relevant nodes by type
    juce::AudioProcessorGraph::NodeID polyMidiNodeID, oscNodeID, adsrNodeID;
    bool foundPolyMidi = false, foundOsc = false, foundAdsr = false;
    for (auto* node : graph.getNodes()) {
        if (dynamic_cast<PolyMidiModule*>(node->getProcessor()) && !foundPolyMidi) {
            polyMidiNodeID = node->nodeID;
            foundPolyMidi = true;
        }
        if (dynamic_cast<OscillatorModule*>(node->getProcessor()) && !foundOsc) {
            oscNodeID = node->nodeID;
            foundOsc = true;
        }
        if (dynamic_cast<ADSRModule*>(node->getProcessor()) && !foundAdsr) {
            adsrNodeID = node->nodeID;
            foundAdsr = true;
        }
    }
    ASSERT_TRUE(foundPolyMidi) << "PolyMidi node not found in graph";
    ASSERT_TRUE(foundOsc) << "Oscillator node not found in graph";
    ASSERT_TRUE(foundAdsr) << "ADSR (Amp Env) node not found in graph";

    // Assert there is a PolyBus routing role==Pitch: PolyMidi -> Oscillator, voiceCount==8
    bool foundPitchBus = false;
    for (const auto& r : routings) {
        if (r.kind == AudioEngine::RoutingKind::PolyBus && r.role == PortRole::Pitch) {
            EXPECT_EQ(r.sourceNodeID, polyMidiNodeID) << "Pitch bus source should be PolyMidi";
            EXPECT_EQ(r.destNodeID, oscNodeID) << "Pitch bus dest should be Oscillator";
            EXPECT_EQ(r.voiceCount, 8) << "Pitch bus should have voiceCount == 8";
            foundPitchBus = true;
            break;
        }
    }
    EXPECT_TRUE(foundPitchBus) << "Expected a PolyBus routing with role==Pitch (PolyMidi -> Oscillator)";

    // Assert there is a PolyBus routing role==Gate: PolyMidi -> ADSR, voiceCount==8
    bool foundGateBus = false;
    for (const auto& r : routings) {
        if (r.kind == AudioEngine::RoutingKind::PolyBus && r.role == PortRole::Gate) {
            EXPECT_EQ(r.sourceNodeID, polyMidiNodeID) << "Gate bus source should be PolyMidi";
            EXPECT_EQ(r.destNodeID, adsrNodeID) << "Gate bus dest should be ADSR (Amp Env)";
            EXPECT_EQ(r.voiceCount, 8) << "Gate bus should have voiceCount == 8";
            foundGateBus = true;
            break;
        }
    }
    EXPECT_TRUE(foundGateBus) << "Expected a PolyBus routing with role==Gate (PolyMidi -> ADSR)";
}

TEST_F(AudioEngineModRoutingTests, PitchGateRoutingsDoNotProduceRingDisplayInfo) {
    auto& graph = engine.getGraph();
    graph.clear();
    bool loaded = synth::PresetManager::loadPreset(6, graph);
    ASSERT_TRUE(loaded) << "Preset 6 (Poly Pad) failed to load";

    // Find relevant nodes by type
    juce::AudioProcessorGraph::NodeID oscNodeID, adsrNodeID, vcaNodeID;
    bool foundOsc = false, foundAdsr = false, foundVca = false;
    for (auto* node : graph.getNodes()) {
        if (dynamic_cast<OscillatorModule*>(node->getProcessor()) && !foundOsc) {
            oscNodeID = node->nodeID;
            foundOsc = true;
        }
        if (dynamic_cast<ADSRModule*>(node->getProcessor()) && !foundAdsr) {
            adsrNodeID = node->nodeID;
            foundAdsr = true;
        }
        if (dynamic_cast<VCAModule*>(node->getProcessor()) && !foundVca) {
            vcaNodeID = node->nodeID;
            foundVca = true;
        }
    }
    ASSERT_TRUE(foundOsc) << "Oscillator node not found in graph";
    ASSERT_TRUE(foundAdsr) << "ADSR node not found in graph";
    ASSERT_TRUE(foundVca) << "VCA node not found in graph";

    auto displayInfos = engine.getModulationDisplayInfo();

    // Pitch fan (PolyMidi -> Oscillator, dest channels 0-7) must NOT produce display info entries
    // for the Oscillator node at those channels (Pitch role -> no ring).
    for (const auto& info : displayInfos) {
        if (info.destNodeID == oscNodeID) {
            // Channel 0-7 are Pitch fan channels — none should appear
            EXPECT_GE(info.destChannelIndex, 8) << "Oscillator pitch channels (0-7) must not produce ring display info";
        }
    }

    // Gate fan (PolyMidi -> ADSR, dest channels 0-7) must NOT produce display info entries
    // for the ADSR node at those channels (Gate role -> no ring).
    for (const auto& info : displayInfos) {
        if (info.destNodeID == adsrNodeID) {
            FAIL() << "ADSR node must not have any ring display info (gate fan should not produce rings)";
        }
    }

    // The env->VCA ModCV bus MUST still produce display info for VCA destChannelIndex==8
    bool foundVcaEntry = false;
    for (const auto& info : displayInfos) {
        if (info.destNodeID == vcaNodeID && info.destChannelIndex == 8) {
            foundVcaEntry = true;
            break;
        }
    }
    EXPECT_TRUE(foundVcaEntry)
        << "Expected a ModulationDisplayInfo entry for VCA destChannelIndex==8 (ADSR->VCA ModCV poly bus)";
}

TEST_F(AudioEngineModRoutingTests, DirectCVRoutingProducesDisplayInfo) {
    auto& graph = engine.getGraph();
    graph.clear();
    bool loaded = synth::PresetManager::loadPreset(6, graph);
    ASSERT_TRUE(loaded) << "Preset 6 (Poly Pad) failed to load";

    auto displayInfos = engine.getModulationDisplayInfo();

    // Find VCA node
    juce::AudioProcessorGraph::NodeID vcaNodeID;
    bool foundVca = false;
    for (auto* node : graph.getNodes()) {
        if (dynamic_cast<VCAModule*>(node->getProcessor()) && !foundVca) {
            vcaNodeID = node->nodeID;
            foundVca = true;
        }
    }
    ASSERT_TRUE(foundVca) << "VCA node not found in graph";

    // At least one ModulationDisplayInfo should target VCA channel 8 (ADSR->VCA poly bus)
    bool found = false;
    for (const auto& info : displayInfos) {
        if (info.destNodeID == vcaNodeID && info.destChannelIndex == 8) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found)
        << "Expected at least one ModulationDisplayInfo for VCA destChannelIndex==8 (ADSR->VCA poly bus)";
}
