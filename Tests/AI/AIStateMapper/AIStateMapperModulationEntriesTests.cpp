// applyJSONToGraph's "modulations" step never doubles a routing: one already in the graph, or one an earlier entry of
// the same array added, is skipped; a routing that differs in any endpoint still gets its own attenuverter. The step
// answers that from one index of the graph's routings kept current as it adds them (AIStateMapperModulations.cpp).
#include "AIStateMapperTestHelpers.h"
#include "Modules/AttenuverterModule.h"
#include "Modules/LFOModule.h"
#include "Modules/ModuleBase.h"
#include <gtest/gtest.h>

using synth::AIStateMapper;

namespace {

int attenuverterCount(juce::AudioProcessorGraph& graph) {
    int n = 0;
    for (auto* node : graph.getNodes())
        if (dynamic_cast<AttenuverterModule*>(node->getProcessor()) != nullptr)
            ++n;
    return n;
}

// Two distinct modulation channels of the Filter, so two routings into it differ only by destination port.
std::pair<int, int> twoFilterChannels() {
    FilterModule filter;
    const auto targets = filter.getModulationTargets();
    EXPECT_GE(targets.size(), 2u);
    return {targets[0].channelIndex, targets[1].channelIndex};
}

juce::var modulationPatch(const juce::String& modulations) {
    return juce::JSON::parse(R"({"nodes": [{"id": 1, "type": "LFO"}, {"id": 2, "type": "Filter"}], "connections": [],
                                 "modulations": [)" +
                             modulations + "]}");
}

juce::String entry(int source, int dest, int destPort) {
    return "{\"source\": " + juce::String(source) + ", \"dest\": " + juce::String(dest) +
           ", \"destPort\": " + juce::String(destPort) + "}";
}

} // namespace

TEST(AIStateMapperModulationEntriesTest, TheSameRoutingTwiceInOneArrayMakesOneAttenuverter) {
    juce::AudioProcessorGraph graph;
    const auto [port, other] = twoFilterChannels();
    juce::ignoreUnused(other);
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(modulationPatch(entry(1, 2, port) + ", " + entry(1, 2, port)), graph,
                                                true, true));
    EXPECT_EQ(attenuverterCount(graph), 1);
    EXPECT_EQ(graph.getConnections().size(), 2u) << "one routing: LFO -> attenuverter -> Filter";
}

TEST(AIStateMapperModulationEntriesTest, RoutingsThatDifferByDestinationPortEachGetOne) {
    juce::AudioProcessorGraph graph;
    const auto [port, other] = twoFilterChannels();
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(modulationPatch(entry(1, 2, port) + ", " + entry(1, 2, other)), graph,
                                                true, true));
    EXPECT_EQ(attenuverterCount(graph), 2);
}

TEST(AIStateMapperModulationEntriesTest, AMergePatchRestatingALiveRoutingAddsNothing) {
    juce::AudioProcessorGraph graph;
    const auto [port, other] = twoFilterChannels();
    std::map<int, juce::AudioProcessorGraph::NodeID> ids;
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(modulationPatch(entry(1, 2, port)), graph, true, true, true, &ids));
    ASSERT_EQ(attenuverterCount(graph), 1);

    const int lfo = (int)ids[1].uid, filter = (int)ids[2].uid;
    const auto merge = juce::JSON::parse(R"({"nodes": [], "connections": [], "modulations": [)" +
                                         entry(lfo, filter, port) + ", " + entry(lfo, filter, other) + "]}");
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(merge, graph, false, true, false));
    EXPECT_EQ(attenuverterCount(graph), 2) << "the restated routing is skipped, the new port gets its own";
}

TEST(AIStateMapperModulationEntriesTest, SnapshotRoundTripKeepsEveryRoutingOnce) {
    juce::AudioProcessorGraph graph;
    const auto [port, other] = twoFilterChannels();
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(modulationPatch(entry(1, 2, port) + ", " + entry(1, 2, other)), graph,
                                                true, true));
    const auto json = AIStateMapper::graphToJSON(graph);
    ASSERT_EQ(json["modulations"].size(), 2);

    juce::AudioProcessorGraph restored;
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(json, restored, true, true));
    EXPECT_EQ(attenuverterCount(restored), 2) << "the attenuverter nodes and the modulations array name the same two";
    EXPECT_EQ(AIStateMapper::graphToJSON(restored)["modulations"].size(), 2);
}
