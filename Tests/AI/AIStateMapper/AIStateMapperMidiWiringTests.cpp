// Merge-mode MIDI auto-connect on a two-track song: a new LFO/ADSR is cabled from the Track In of the track whose
// module it modulates, so synth::resolveTrackOwners gives that track its lanes (AIStateMapperMidiWiring.cpp).
#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/FilterModule.h"
#include "Timeline/TrackOwnership.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace {

using NodeID = juce::AudioProcessorGraph::NodeID;
constexpr int kMidi = juce::AudioProcessorGraph::midiChannelIndex;

struct Track {
    NodeID in, osc, filter;
};

// Track In -> Oscillator (MIDI), Oscillator -> Filter -> Audio Output, the shape a built track has.
Track addTrack(juce::AudioProcessorGraph& graph, NodeID out) {
    Track t;
    t.in = graph.addNode(synth::AIStateMapper::createModule("Track In"))->nodeID;
    t.osc = graph.addNode(synth::AIStateMapper::createModule("Oscillator"))->nodeID;
    t.filter = graph.addNode(synth::AIStateMapper::createModule("Filter"))->nodeID;
    EXPECT_TRUE(graph.addConnection({{t.in, kMidi}, {t.osc, kMidi}}));
    EXPECT_TRUE(graph.addConnection({{t.osc, 0}, {t.filter, 0}}));
    EXPECT_TRUE(graph.addConnection({{t.filter, 0}, {out, 0}}));
    return t;
}

NodeID midiSourceOf(juce::AudioProcessorGraph& graph, NodeID node) {
    for (const auto& conn : graph.getConnections())
        if (conn.destination.nodeID == node && conn.destination.isMIDI())
            return conn.source.nodeID;
    return NodeID{};
}

} // namespace

// Regression test for FRO1826: every new LFO was cabled from the first MIDI source (the Drums track's Track In), so
// two tracks reached the modules it modulated and their automation lanes landed under "Unassigned automation".
TEST(AIStateMapperMidiWiringTest, NewLfoRetriggersFromTheTrackItModulates) {
    juce::AudioProcessorGraph graph;
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    const auto out = graph
                         .addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
                             juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode))
                         ->nodeID;
    const Track drums = addTrack(graph, out);
    const Track lead = addTrack(graph, out);

    FilterModule probe;
    const int port = probe.getModulationTargets()[0].channelIndex;
    const auto patch = juce::JSON::parse(R"({"nodes":[{"id":9001,"type":"LFO"}],"connections":[],"modulations":[
        {"source":9001,"dest":)" + juce::String((int)lead.filter.uid) +
                                         ",\"destPort\":" + juce::String(port) + "}]}");
    std::map<int, NodeID> ids;
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(patch, graph, false, true, true, &ids));

    const NodeID lfo = ids.at(9001);
    EXPECT_EQ(midiSourceOf(graph, lfo), lead.in) << "the lead's Track In, not the first (drums) one";
    EXPECT_NE(midiSourceOf(graph, lfo), drums.in);

    // What the Timeline then asks: the lead's filter is the lead's, so its lane sits under the lead.
    synth::OwnershipGraph owned;
    auto key = [](NodeID id) { return juce::String((int)id.uid); };
    for (auto* n : graph.getNodes())
        owned.nodes.push_back(key(n->nodeID));
    for (const auto& c : graph.getConnections())
        owned.flowEdges.push_back({key(c.source.nodeID), key(c.destination.nodeID)});
    owned.modEdges.push_back({key(lfo), key(lead.filter)});
    const auto owners =
        synth::resolveTrackOwners(owned, {{synth::TrackId{1}, key(drums.in)}, {synth::TrackId{2}, key(lead.in)}});
    ASSERT_EQ(owners.count(key(lead.filter)), 1u);
    EXPECT_EQ(owners.at(key(lead.filter)), synth::TrackId{2});
    EXPECT_EQ(owners.at(key(drums.filter)), synth::TrackId{1});
}

TEST(AIStateMapperMidiWiringTest, NewLfoOnAnAmbiguousTwoTrackSongStaysUnwired) {
    juce::AudioProcessorGraph graph;
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    const auto out = graph
                         .addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
                             juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode))
                         ->nodeID;
    addTrack(graph, out);
    addTrack(graph, out);

    std::map<int, NodeID> ids;
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(juce::JSON::parse(R"({"nodes":[{"id":9002,"type":"LFO"}]})"),
                                                       graph, false, true, true, &ids));
    EXPECT_EQ(midiSourceOf(graph, ids.at(9002)).uid, 0u)
        << "no track to pick, so no cable (the first track must not claim it)";
}
