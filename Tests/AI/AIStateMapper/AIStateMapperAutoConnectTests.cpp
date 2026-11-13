// ---------------------------------------------------------------------------------------
// Merge-mode auto-connect (the `autoConnectNewNodes` parameter)
// ---------------------------------------------------------------------------------------
#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/OscillatorModule.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace {

// Builds a minimal patch with a usable Audio Output to merge into.
// setPlayConfigDetails MUST come first: the Audio Output node's input channel count derives from
// the graph's output channel count, and on an unconfigured graph it is zero — every addConnection
// into it then fails silently.
juce::AudioProcessorGraph::NodeID addAudioOutput(juce::AudioProcessorGraph& graph) {
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    auto node = graph.addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
        juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));
    return node->nodeID;
}

bool hasConnectionTo(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID dest,
                     const juce::String& sourceTypeName) {
    for (const auto& conn : graph.getConnections()) {
        if (conn.destination.nodeID != dest)
            continue;
        if (auto* src = graph.getNodeForId(conn.source.nodeID))
            if (src->getProcessor()->getName() == sourceTypeName)
                return true;
    }
    return false;
}

} // namespace

TEST(AIStateMapperTest, MergeAutoConnectsNewAudioNodesToOutputByDefault) {
    // This is a deliberate affordance for AI-authored merge patches: a model that adds an
    // Oscillator to an existing patch means for it to be audible. Locked here because snippet
    // insertion opts OUT of it, and the two behaviours must not drift into each other.
    juce::AudioProcessorGraph graph;
    auto out = addAudioOutput(graph);

    juce::var patch = juce::JSON::parse(R"({"nodes":[{"id":4001,"type":"Oscillator"}]})");
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(patch, graph, /*clearExisting=*/false));

    EXPECT_TRUE(hasConnectionTo(graph, out, "Oscillator"))
        << "an AI merge patch's new audio node should reach the output";
}

TEST(AIStateMapperTest, MergeSkipsAutoConnectWhenTheCallerOptsOut) {
    juce::AudioProcessorGraph graph;
    auto out = addAudioOutput(graph);

    juce::var patch = juce::JSON::parse(R"({"nodes":[{"id":4002,"type":"Oscillator"}]})");
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(patch, graph, /*clearExisting=*/false, /*trusted=*/false,
                                                       /*autoConnectNewNodes=*/false));

    EXPECT_EQ(graph.getNumNodes(), 2) << "the node is still added";
    EXPECT_FALSE(hasConnectionTo(graph, out, "Oscillator"))
        << "opting out must leave the new node exactly as the patch described it";
}

// Regression test for FRO1019: an AI-added ADSR never got notes, so as a mod source it stayed at zero.
TEST(AIStateMapperTest, MergeAutoConnectsMidiToNewAdsrAndLfo) {
    juce::AudioProcessorGraph graph;
    addAudioOutput(graph);

    // An existing MIDI source: a MIDI input node wired to an existing Oscillator.
    auto midiIn = graph.addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
        juce::AudioProcessorGraph::AudioGraphIOProcessor::midiInputNode));
    ASSERT_NE(midiIn, nullptr);
    juce::var base = juce::JSON::parse(R"({"nodes":[{"id":4003,"type":"Oscillator"}]})");
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(base, graph, /*clearExisting=*/false));
    juce::AudioProcessorGraph::NodeID oscId;
    bool foundOsc = false;
    for (auto* n : graph.getNodes())
        if (n->getProcessor()->getName() == "Oscillator") {
            oscId = n->nodeID;
            foundOsc = true;
        }
    ASSERT_TRUE(foundOsc);
    ASSERT_TRUE(graph.addConnection({{midiIn->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                                     {oscId, juce::AudioProcessorGraph::midiChannelIndex}}));

    juce::var patch = juce::JSON::parse(R"({"nodes":[{"id":4004,"type":"ADSR"},{"id":4005,"type":"LFO"}]})");
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(patch, graph, /*clearExisting=*/false));

    for (const juce::String type : {"ADSR", "LFO"}) {
        bool gotMidi = false;
        for (const auto& conn : graph.getConnections()) {
            auto* dst = graph.getNodeForId(conn.destination.nodeID);
            if (dst != nullptr && dst->getProcessor()->getName() == type && conn.destination.isMIDI() &&
                conn.source.nodeID == midiIn->nodeID)
                gotMidi = true;
        }
        EXPECT_TRUE(gotMidi) << type << " should receive MIDI from the existing MIDI source";
    }
}
