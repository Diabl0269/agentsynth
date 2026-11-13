// PatchIdScope: the optional input an edit plan hands validatePatch/applyJSONToGraph so a MERGE
// patch can address nodes an earlier step of the same plan built, by the plan ids the model gave
// them, while those nodes' own auto-assigned uids stay out of the patch's namespace. Without a
// scope (every other caller) nothing changes.
#include "AIStateMapperTestHelpers.h"
#include "Modules/LFOModule.h"
#include <gtest/gtest.h>

using synth::AIStateMapper;
using synth::PatchIdScope;
using synth::PatchValidationError;

namespace {

juce::var parse(const juce::String& json) {
    const juce::var value = juce::JSON::parse(json);
    EXPECT_FALSE(value.isVoid()) << "test JSON did not parse: " << json;
    return value;
}

// A live LFO the patch may address by uid, and a "built" Filter bound to plan id 7002.
struct ScopedGraph {
    juce::AudioProcessorGraph graph;
    juce::AudioProcessorGraph::NodeID lfo, built;
    PatchIdScope scope;

    ScopedGraph() {
        lfo = graph.addNode(std::make_unique<LFOModule>())->nodeID;
        built = graph.addNode(std::make_unique<FilterModule>())->nodeID;
        scope.boundIds[7002] = built;
    }
};

} // namespace

TEST(AIStateMapperIdScopeTest, BoundIdResolvesToTheBuiltNode) {
    ScopedGraph g;
    const auto json =
        parse("{\"nodes\": [], \"connections\": [], \"modulations\": [{\"source\": " + juce::String((int)g.lfo.uid) +
              ", \"dest\": 7002, \"destParam\": \"cutoff\"}]}");
    EXPECT_FALSE(AIStateMapper::validatePatch(json, g.graph, false, false).ok) << "7002 means nothing without a scope";
    ASSERT_TRUE(AIStateMapper::validatePatch(json, g.graph, false, false, false, &g.scope).ok);
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(json, g.graph, false, false, true, nullptr, &g.scope));
    bool routed = false;
    for (const auto& c : g.graph.getConnections())
        routed = routed || (c.destination.nodeID == g.built && c.destination.channelIndex == 1);
    EXPECT_TRUE(routed) << "the attenuverter feeds the built Filter's cutoff jack";
}

TEST(AIStateMapperIdScopeTest, BuiltNodesRawUidIsNotAddressable) {
    ScopedGraph g;
    const auto json = parse("{\"nodes\": [], \"connections\": [{\"src\": " + juce::String((int)g.built.uid) +
                            ", \"srcPort\": 0, \"dst\": " + juce::String((int)g.lfo.uid) + ", \"dstPort\": 0}]}");
    EXPECT_TRUE(AIStateMapper::validatePatch(json, g.graph, false, false).ok);
    const auto scoped = AIStateMapper::validatePatch(json, g.graph, false, false, false, &g.scope);
    EXPECT_FALSE(scoped.ok);
    EXPECT_EQ(scoped.error, PatchValidationError::ConnectionUnknownNode);
}

TEST(AIStateMapperIdScopeTest, PatchNodeReusingABoundIdIsRejected) {
    ScopedGraph g;
    const auto result = AIStateMapper::validatePatch(parse(R"({"nodes": [{"id": 7002, "type": "LFO"}]})"), g.graph,
                                                     false, false, false, &g.scope);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error, PatchValidationError::DuplicateNodeId);
    EXPECT_TRUE(result.message.contains("7002")) << result.message;
}

// A patch node whose id happens to equal a hidden node's uid is a NEW node, never an edit of the
// hidden one - the same outcome a preview (where the hidden node does not exist) reports.
TEST(AIStateMapperIdScopeTest, PatchNodeOnAHiddenUidCreatesANewNode) {
    ScopedGraph g;
    const int hiddenUid = (int)g.built.uid;
    const int before = g.graph.getNumNodes();
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(
        parse("{\"nodes\": [{\"id\": " + juce::String(hiddenUid) + ", \"type\": \"LFO\"}], \"connections\": []}"),
        g.graph, false, false, false, nullptr, &g.scope));
    EXPECT_EQ(g.graph.getNumNodes(), before + 1);
    EXPECT_NE(dynamic_cast<FilterModule*>(g.graph.getNodeForId(g.built)->getProcessor()), nullptr);
}

TEST(AIStateMapperIdScopeTest, RemoveGoesThroughTheBoundIdAndNeverReachesAHiddenUid) {
    ScopedGraph g;
    g.scope.hiddenNodes.insert(g.graph.addNode(std::make_unique<LFOModule>())->nodeID);
    const auto hidden = *g.scope.hiddenNodes.begin();
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(parse("{\"remove\": [" + juce::String((int)hidden.uid) + "]}"), g.graph,
                                                false, false, false, nullptr, &g.scope));
    EXPECT_NE(g.graph.getNodeForId(hidden), nullptr) << "a hidden node is not removable by raw uid";
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(parse(R"({"remove": [7002]})"), g.graph, false, false, false, nullptr,
                                                &g.scope));
    EXPECT_EQ(g.graph.getNodeForId(g.built), nullptr) << "the bound id removes the node it names";
}

// Nodes applyJSONToGraph creates carry a uuid the moment it returns, so a caller can address them.
TEST(AIStateMapperIdScopeTest, CreatedNodesCarryAUuidImmediately) {
    juce::AudioProcessorGraph graph;
    std::map<int, juce::AudioProcessorGraph::NodeID> ids;
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(parse(R"({"nodes": [{"id": 1, "type": "LFO"}], "connections": []})"),
                                                graph, true, false, true, &ids));
    auto* node = graph.getNodeForId(ids[1]);
    ASSERT_NE(node, nullptr);
    EXPECT_TRUE(node->properties["uuid"].toString().isNotEmpty());
}
