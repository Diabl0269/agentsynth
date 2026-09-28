// SidechainKeyGraphTests.cpp — the graph side of the Compressor/Gate Key input.
//
//   • publication — synth::publishSidechainConnections (and the engine's graph-change listener)
//                   sets a module's connectivity flag while a cable lands on a Key jack, clears it
//                   once the cable is gone, and ignores every other input
//   • mixer walks — a key edge is not a signal edge, so a bass strip whose Compressor is keyed from a
//                   kick strip is still a channel, never a bus
//   • old patches — a 7-input-era Compressor patch loads with every cable on its original channel

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "AudioEngine/SidechainConnections.h"
#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "Mixer/MixerSends/MixerSends.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/FX/CompressorModule.h"
#include "Modules/FX/GateModule.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <set>

namespace {

using NodeID = juce::AudioProcessorGraph::NodeID;
using Connection = juce::AudioProcessorGraph::Connection;
constexpr int kRight = ChannelStripModule::kRightBase;

NodeID addFactoryNode(juce::AudioProcessorGraph& graph, const juce::String& type) {
    auto node = graph.addNode(synth::AIStateMapper::createModule(type));
    return node != nullptr ? node->nodeID : NodeID{};
}

ModuleBase* moduleAt(juce::AudioProcessorGraph& graph, NodeID id) {
    auto* node = graph.getNodeForId(id);
    return node != nullptr ? dynamic_cast<ModuleBase*>(node->getProcessor()) : nullptr;
}

} // namespace

TEST(SidechainKeyGraph, PublicationFollowsTheKeyCable) {
    juce::AudioProcessorGraph graph;
    const auto kick = addFactoryNode(graph, "Oscillator");
    const auto lfo = addFactoryNode(graph, "LFO");
    const auto comp = addFactoryNode(graph, "Compressor");
    const auto gate = addFactoryNode(graph, "Gate");
    auto* compressor = moduleAt(graph, comp);
    ASSERT_NE(compressor, nullptr);

    // Audio and CV cables are not a key.
    ASSERT_TRUE(graph.addConnection({{kick, 0}, {comp, 0}}));
    ASSERT_TRUE(graph.addConnection({{lfo, 0}, {comp, 2}}));
    synth::publishSidechainConnections(graph);
    EXPECT_FALSE(compressor->isSidechainConnected());

    const Connection key{{kick, 0}, {comp, CompressorModule::kKeyBase + 1}}; // Key R alone still keys
    ASSERT_TRUE(graph.addConnection(key));
    synth::publishSidechainConnections(graph);
    EXPECT_TRUE(compressor->isSidechainConnected());
    EXPECT_FALSE(moduleAt(graph, gate)->isSidechainConnected()) << "only the module the key reaches";

    graph.removeConnection(key);
    synth::publishSidechainConnections(graph);
    EXPECT_FALSE(compressor->isSidechainConnected()) << "unplugging the key returns to self-detection";
}

TEST(SidechainKeyGraph, TheEngineRepublishesOnEveryGraphChange) {
    // A plain cable drag never reaches publishTimeline, so the engine listens to the graph's own
    // change broadcast (async) — pump the message loop the way a running app would.
    AudioEngine engine;
    auto& graph = engine.getGraph();
    const auto kick = addFactoryNode(graph, "Oscillator");
    const auto gateId = addFactoryNode(graph, "Gate");
    auto* gate = moduleAt(graph, gateId);
    ASSERT_NE(gate, nullptr);

    const auto pumpUntil = [&](bool expected) {
        for (int i = 0; i < 100 && gate->isSidechainConnected() != expected; ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        return gate->isSidechainConnected() == expected;
    };

    const Connection key{{kick, 0}, {gateId, GateModule::kKeyBase}};
    ASSERT_TRUE(graph.addConnection(key));
    EXPECT_TRUE(pumpUntil(true));
    graph.removeConnection(key);
    EXPECT_TRUE(pumpUntil(false));
}

TEST(SidechainKeyGraph, AKeyEdgeIsNotASignalEdge) {
    juce::AudioProcessorGraph graph;
    const auto kick = addFactoryNode(graph, "Oscillator");
    const auto comp = addFactoryNode(graph, "Compressor");
    const Connection audio{{kick, 0}, {comp, 0}};
    const Connection key{{kick, 0}, {comp, CompressorModule::kKeyBase}};
    ASSERT_TRUE(graph.addConnection(audio));
    ASSERT_TRUE(graph.addConnection(key));
    const auto connections = graph.getConnections();
    EXPECT_TRUE(synth::isSignalEdge(graph, connections, audio));
    EXPECT_FALSE(synth::isSignalEdge(graph, connections, key));
    EXPECT_FALSE(isSignalPathInputRole(PortRole::Sidechain));
    EXPECT_TRUE(isSignalPathInputRole(PortRole::Audio));
}

TEST(SidechainKeyGraph, AStripKeyedFromAnotherStripIsNotABus) {
    // kick strip ─┐ (Key L/R)
    // bass osc ──► Compressor ──► bass strip
    juce::AudioProcessorGraph graph;
    const auto kickStrip = addFactoryNode(graph, "Channel Strip");
    const auto bassOsc = addFactoryNode(graph, "Oscillator");
    const auto comp = addFactoryNode(graph, "Compressor");
    const auto bassStrip = addFactoryNode(graph, "Channel Strip");
    ASSERT_TRUE(graph.addConnection({{bassOsc, 0}, {comp, 0}}));
    ASSERT_TRUE(graph.addConnection({{bassOsc, 0}, {comp, 1}}));
    ASSERT_TRUE(graph.addConnection({{comp, 0}, {bassStrip, 0}}));
    ASSERT_TRUE(graph.addConnection({{comp, 1}, {bassStrip, kRight}}));
    ASSERT_TRUE(graph.addConnection({{kickStrip, 0}, {comp, CompressorModule::kKeyBase}}));
    ASSERT_TRUE(graph.addConnection({{kickStrip, kRight}, {comp, CompressorModule::kKeyBase + 1}}));

    EXPECT_TRUE(synth::findStripsFeedingStrip(graph, bassStrip).empty());
    EXPECT_FALSE(synth::isBusStrip(graph, bassStrip)) << "a key cable must not make the bass a bus";

    // Control: the same kick strip on the compressor's AUDIO input does make it one.
    ASSERT_TRUE(graph.addConnection({{kickStrip, 0}, {comp, 0}}));
    EXPECT_TRUE(synth::isBusStrip(graph, bassStrip));
}

TEST(SidechainKeyGraph, APreKeyCompressorPatchLoadsOnTheSameChannels) {
    // A patch saved when the Compressor had 7 inputs: stereo audio on ch0/1, an LFO through an
    // attenuverter on Threshold (ch2), and a raw CV cable on Makeup (ch6) that the loader promotes.
    const auto patch = juce::JSON::parse(R"({
        "nodes":[{"id":1,"type":"Oscillator"},{"id":2,"type":"Compressor"},{"id":3,"type":"LFO"},
                 {"id":4,"type":"Attenuverter"}],
        "connections":[{"src":1,"srcPort":0,"dst":2,"dstPort":0},{"src":1,"srcPort":0,"dst":2,"dstPort":1},
                       {"src":3,"srcPort":0,"dst":4,"dstPort":0},{"src":4,"srcPort":0,"dst":2,"dstPort":2},
                       {"src":3,"srcPort":0,"dst":2,"dstPort":6}]})");
    juce::AudioProcessorGraph graph;
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(patch, graph, true, true));

    NodeID compId;
    for (auto* node : graph.getNodes())
        if (dynamic_cast<CompressorModule*>(node->getProcessor()) != nullptr)
            compId = node->nodeID;
    ASSERT_NE(compId.uid, 0u);

    std::multiset<int> landed;
    for (const auto& c : graph.getConnections())
        if (c.destination.nodeID == compId)
            landed.insert(c.destination.channelIndex);
    EXPECT_EQ(landed, (std::multiset<int>{0, 1, 2, 6})) << "no cable moved, none landed on the key pair";

    synth::publishSidechainConnections(graph);
    EXPECT_FALSE(moduleAt(graph, compId)->isSidechainConnected()) << "an old patch is never keyed";
}
