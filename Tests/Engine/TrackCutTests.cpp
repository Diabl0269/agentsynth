// synth::cutIntoTrackJobs puts every node in exactly one job: a strip's feeders in its Strip job, what feeds two or
// more strips in the one Shared job, and everything no strip reaches in the one Post job
// (docs/architecture/audio-engine.md#track-cut).
#include "AudioEngine/TrackCut.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/FilterModule.h"
#include "Modules/LFOModule.h"
#include "Modules/MasterModule.h"
#include "Modules/OscillatorModule.h"
#include <gtest/gtest.h>
#include <map>
#include <set>

namespace {

using NodeID = juce::AudioProcessorGraph::NodeID;
using Kind = synth::TrackJob::Kind;

struct Rig {
    juce::AudioProcessorGraph graph;
    NodeID oscA, oscB, filterA, lfo, stripA, stripB, bus, master, lone;
    Rig() {
        oscA = graph.addNode(std::make_unique<OscillatorModule>())->nodeID;
        oscB = graph.addNode(std::make_unique<OscillatorModule>())->nodeID;
        filterA = graph.addNode(std::make_unique<FilterModule>())->nodeID;
        lfo = graph.addNode(std::make_unique<LFOModule>())->nodeID;
        stripA = graph.addNode(std::make_unique<ChannelStripModule>())->nodeID;
        stripB = graph.addNode(std::make_unique<ChannelStripModule>())->nodeID;
        auto busStrip = std::make_unique<ChannelStripModule>();
        busStrip->setIsBus(true);
        bus = graph.addNode(std::move(busStrip))->nodeID;
        master = graph.addNode(std::make_unique<MasterModule>())->nodeID;
        lone = graph.addNode(std::make_unique<FilterModule>())->nodeID;
        graph.addConnection({{oscA, 0}, {filterA, 0}});
        graph.addConnection({{filterA, 0}, {stripA, 0}});
        graph.addConnection({{oscB, 0}, {stripB, 0}});
        graph.addConnection({{lfo, 0}, {filterA, 2}});
        graph.addConnection({{lfo, 0}, {oscB, 2}});
        graph.addConnection({{stripA, 8}, {bus, 0}});
        graph.addConnection({{stripB, 8}, {bus, 0}});
        graph.addConnection({{stripA, 0}, {master, 0}});
        graph.addConnection({{stripB, 0}, {master, 0}});
        graph.addConnection({{bus, 0}, {master, 0}});
    }
};

int jobOfNode(const synth::TrackCut& cut, NodeID id) {
    int found = -1, hits = 0;
    for (size_t j = 0; j < cut.jobs.size(); ++j)
        for (const auto& n : cut.jobs[j].nodes)
            if (n == id) {
                found = (int)j;
                ++hits;
            }
    EXPECT_EQ(hits, 1);
    return found;
}

void expectEveryNodeOnce(const juce::AudioProcessorGraph& graph, const synth::TrackCut& cut) {
    size_t total = 0;
    for (const auto& job : cut.jobs)
        total += job.nodes.size();
    EXPECT_EQ(total, (size_t)graph.getNumNodes());
    for (auto* node : graph.getNodes())
        EXPECT_GE(jobOfNode(cut, node->nodeID), 0);
}

} // namespace

TEST(TrackCutTest, SplitsTracksSharedAndPost) {
    Rig g;
    const auto cut = synth::cutIntoTrackJobs(g.graph);
    expectEveryNodeOnce(g.graph, cut);

    const int a = jobOfNode(cut, g.stripA), b = jobOfNode(cut, g.stripB), bus = jobOfNode(cut, g.bus);
    const int shared = jobOfNode(cut, g.lfo), post = jobOfNode(cut, g.master);
    EXPECT_EQ(jobOfNode(cut, g.oscA), a);
    EXPECT_EQ(jobOfNode(cut, g.filterA), a);
    EXPECT_EQ(jobOfNode(cut, g.oscB), b);
    EXPECT_EQ(jobOfNode(cut, g.lone), post);
    EXPECT_EQ(std::set<int>({a, b, bus, shared, post}).size(), 5u);
    EXPECT_EQ(cut.jobs.size(), 5u);
    EXPECT_EQ(cut.jobs[(size_t)a].kind, Kind::Strip);
    EXPECT_EQ(cut.jobs[(size_t)a].strip, g.stripA);
    EXPECT_EQ(cut.jobs[(size_t)bus].kind, Kind::Strip);
    EXPECT_EQ(cut.jobs[(size_t)shared].kind, Kind::Shared);
    EXPECT_EQ(cut.jobs[(size_t)post].kind, Kind::Post);

    const std::set<std::pair<int, int>> edges(cut.edges.begin(), cut.edges.end());
    EXPECT_EQ(edges.size(), cut.edges.size()); // deduplicated
    EXPECT_EQ(edges, (std::set<std::pair<int, int>>{
                         {shared, a}, {shared, b}, {a, bus}, {b, bus}, {a, post}, {b, post}, {bus, post}}));

    ASSERT_TRUE(cut.acyclic);
    ASSERT_EQ(cut.order.size(), cut.jobs.size());
    std::map<int, size_t> position;
    for (size_t i = 0; i < cut.order.size(); ++i)
        position[cut.order[i]] = i;
    for (const auto& [from, to] : cut.edges)
        EXPECT_LT(position[from], position[to]);
}

TEST(TrackCutTest, NoStripsMakesOnePostJobAndAnEmptyGraphNone) {
    juce::AudioProcessorGraph graph;
    EXPECT_TRUE(synth::cutIntoTrackJobs(graph).jobs.empty());
    graph.addNode(std::make_unique<OscillatorModule>());
    graph.addNode(std::make_unique<MasterModule>());
    const auto cut = synth::cutIntoTrackJobs(graph);
    ASSERT_EQ(cut.jobs.size(), 1u);
    EXPECT_EQ(cut.jobs[0].kind, Kind::Post);
    EXPECT_EQ(cut.jobs[0].nodes.size(), 2u);
    EXPECT_TRUE(cut.acyclic);
}

TEST(TrackCutTest, ACableBackBetweenStripsIsACycle) {
    Rig g;
    g.graph.addConnection({{g.bus, 1}, {g.stripA, 1}});
    const auto cut = synth::cutIntoTrackJobs(g.graph);
    expectEveryNodeOnce(g.graph, cut);
    EXPECT_FALSE(cut.acyclic);
    EXPECT_TRUE(cut.order.empty());
}
