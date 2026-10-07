// synth::ConnectionIndex answers every per-node question in exactly the order a full getConnections() scan would, so a
// pass that swaps a scan per item for one index keeps the same first match; synth::NodeUuidCache answers as a scan of
// the graph would while nodes come, go and change uuid under it (docs/architecture/graph-queries.md).
#include "AudioEngine/ConnectionIndex.h"
#include "AudioEngine/NodeUuidCache.h"
#include "Modules/FilterModule.h"
#include "Modules/LFOModule.h"
#include "Modules/OscillatorModule.h"
#include <gtest/gtest.h>

namespace {

using Connection = juce::AudioProcessorGraph::Connection;
using NodeID = juce::AudioProcessorGraph::NodeID;

std::vector<Connection> scan(const juce::AudioProcessorGraph& graph, NodeID node, bool in, bool out) {
    std::vector<Connection> hits;
    for (const auto& c : graph.getConnections())
        if ((in && c.destination.nodeID == node) || (out && c.source.nodeID == node))
            hits.push_back(c);
    return hits;
}

struct SmallGraph {
    juce::AudioProcessorGraph graph;
    NodeID osc, lfo, filter;
    SmallGraph() {
        osc = graph.addNode(std::make_unique<OscillatorModule>())->nodeID;
        lfo = graph.addNode(std::make_unique<LFOModule>())->nodeID;
        filter = graph.addNode(std::make_unique<FilterModule>())->nodeID;
        graph.addConnection({{osc, 0}, {filter, 0}});
        graph.addConnection({{osc, 1}, {filter, 1}});
        graph.addConnection({{lfo, 0}, {filter, 2}});
        graph.addConnection({{lfo, 0}, {osc, 2}});
    }
};

} // namespace

TEST(ConnectionIndexTest, EveryListMatchesAFullScanInOrder) {
    SmallGraph g;
    const synth::ConnectionIndex cables(g.graph);
    EXPECT_EQ(cables.all(), g.graph.getConnections());
    for (const auto node : {g.osc, g.lfo, g.filter}) {
        EXPECT_EQ(cables.into(node), scan(g.graph, node, true, false));
        EXPECT_EQ(cables.outOf(node), scan(g.graph, node, false, true));
        EXPECT_EQ(cables.touching(node), scan(g.graph, node, true, true));
    }
}

TEST(ConnectionIndexTest, AnUncabledOrUnknownNodeHasEmptyLists) {
    SmallGraph g;
    const auto lone = g.graph.addNode(std::make_unique<LFOModule>())->nodeID;
    const synth::ConnectionIndex cables(g.graph);
    EXPECT_TRUE(cables.into(lone).empty());
    EXPECT_TRUE(cables.outOf(NodeID(9999)).empty());
    EXPECT_TRUE(synth::ConnectionIndex().all().empty());
}

TEST(ConnectionIndexTest, IsASnapshotOfTheGraphWhenBuilt) {
    SmallGraph g;
    const synth::ConnectionIndex cables(g.graph);
    g.graph.removeConnection({{g.lfo, 0}, {g.filter, 2}});
    EXPECT_EQ(cables.outOf(g.lfo).size(), 2u) << "a later edit is not seen";
}

TEST(NodeUuidCacheTest, FindsTheNodeCarryingTheUuidAsTheGraphChanges) {
    SmallGraph g;
    g.graph.getNodeForId(g.osc)->properties.set("uuid", "a");
    g.graph.getNodeForId(g.lfo)->properties.set("uuid", "b");
    synth::NodeUuidCache cache;
    EXPECT_EQ(cache.find(g.graph, "a")->nodeID, g.osc);
    EXPECT_EQ(cache.find(g.graph, "b")->nodeID, g.lfo);
    EXPECT_EQ(cache.find(g.graph, ""), nullptr);
    EXPECT_EQ(cache.find(g.graph, "missing"), nullptr);

    const auto added = g.graph.addNode(std::make_unique<FilterModule>());
    added->properties.set("uuid", "c");
    EXPECT_EQ(cache.find(g.graph, "c"), added.get()) << "a node added after the last fill";

    g.graph.getNodeForId(g.osc)->properties.set("uuid", "a2");
    EXPECT_EQ(cache.find(g.graph, "a"), nullptr) << "a remembered node that no longer carries the uuid";
    EXPECT_EQ(cache.find(g.graph, "a2")->nodeID, g.osc);

    g.graph.removeNode(g.lfo);
    EXPECT_EQ(cache.find(g.graph, "b"), nullptr) << "a removed node";
}

TEST(NodeUuidCacheTest, AnswersTheFirstNodeInGraphOrderWhenAUuidRepeats) {
    SmallGraph g;
    g.graph.getNodeForId(g.lfo)->properties.set("uuid", "same");
    g.graph.getNodeForId(g.filter)->properties.set("uuid", "same");
    synth::NodeUuidCache cache;
    EXPECT_EQ(cache.find(g.graph, "same")->nodeID, g.lfo);
}
