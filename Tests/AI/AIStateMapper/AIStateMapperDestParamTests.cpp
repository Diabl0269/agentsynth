// A modulation names its destination channel by "destPort", by "destParam" (a parameter id,
// resolved the way the canvas modulator picker resolves it), or by both when they agree; neither
// is a rejection on the untrusted path. Also the optional PatchIdScope an edit plan passes so a
// merge patch can address nodes an earlier plan step built.
#include "AIStateMapperTestHelpers.h"
#include "Modules/AttenuverterModule.h"
#include "Modules/LFOModule.h"
#include "Modules/ModuleBase.h"
#include <gtest/gtest.h>

using synth::AIStateMapper;
using synth::PatchValidationError;

namespace {

juce::var parse(const juce::String& json) {
    const juce::var value = juce::JSON::parse(json);
    EXPECT_FALSE(value.isVoid()) << "test JSON did not parse: " << json;
    return value;
}

// The attenuverter carrying source -> dest, and the dest channel it feeds; -1 when there is none.
int modulatedChannel(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID source,
                     juce::AudioProcessorGraph::NodeID dest) {
    for (auto* node : graph.getNodes()) {
        if (dynamic_cast<AttenuverterModule*>(node->getProcessor()) == nullptr)
            continue;
        if (!graph.isConnected({{source, 0}, {node->nodeID, 0}}))
            continue;
        for (const auto& c : graph.getConnections())
            if (c.source.nodeID == node->nodeID && c.destination.nodeID == dest)
                return c.destination.channelIndex;
    }
    return -1;
}

int cutoffChannel(juce::AudioProcessor* processor) {
    return dynamic_cast<ModuleBase*>(processor)->modulationChannelForParam("cutoff");
}

constexpr const char* kNewNodes = R"("nodes": [{"id": 1, "type": "LFO"}, {"id": 2, "type": "Filter"}],
                                     "connections": [])";

} // namespace

TEST(AIStateMapperDestParamTest, DestParamRoutesToTheParametersModulationChannel) {
    juce::AudioProcessorGraph graph;
    std::map<int, juce::AudioProcessorGraph::NodeID> ids;
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(
        parse(juce::String("{") + kNewNodes + R"(, "modulations": [{"source": 1, "dest": 2, "destParam": "cutoff"}]})"),
        graph, true, false, true, &ids));
    auto* filter = graph.getNodeForId(ids[2]);
    ASSERT_NE(filter, nullptr);
    EXPECT_EQ(modulatedChannel(graph, ids[1], ids[2]), cutoffChannel(filter->getProcessor()));
    EXPECT_EQ(cutoffChannel(filter->getProcessor()), 1);
}

TEST(AIStateMapperDestParamTest, ModulationWithNeitherPortNorParamIsRejected) {
    juce::AudioProcessorGraph graph;
    const auto result = AIStateMapper::validatePatch(
        parse(juce::String("{") + kNewNodes + R"(, "modulations": [{"source": 1, "dest": 2}]})"), graph, true, false);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error, PatchValidationError::ModulationInvalidPort);
    EXPECT_TRUE(result.message.contains("One of \"destParam\"")) << result.message;
    EXPECT_TRUE(result.message.contains("is required")) << result.message;
}

TEST(AIStateMapperDestParamTest, UnresolvableDestParamIsRejectedNamingTheTargets) {
    juce::AudioProcessorGraph graph;
    const auto result = AIStateMapper::validatePatch(
        parse(juce::String("{") + kNewNodes + R"(, "modulations": [{"source": 1, "dest": 2, "destParam": "wobble"}]})"),
        graph, true, false);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error, PatchValidationError::ModulationInvalidPort);
    EXPECT_TRUE(result.message.contains("\"wobble\"")) << result.message;
    EXPECT_TRUE(result.message.contains("cutoff")) << result.message;
}

TEST(AIStateMapperDestParamTest, DisagreeingPortAndParamAreRejectedAgreeingOnesAccepted) {
    juce::AudioProcessorGraph graph;
    const auto disagree = AIStateMapper::validatePatch(
        parse(juce::String("{") + kNewNodes +
              R"(, "modulations": [{"source": 1, "dest": 2, "destParam": "cutoff", "destPort": 2}]})"),
        graph, true, false);
    EXPECT_FALSE(disagree.ok);
    EXPECT_EQ(disagree.error, PatchValidationError::ModulationInvalidPort);
    EXPECT_TRUE(disagree.message.contains("disagree")) << disagree.message;

    const auto agree = AIStateMapper::validatePatch(
        parse(juce::String("{") + kNewNodes +
              R"(, "modulations": [{"source": 1, "dest": 2, "destParam": "cutoff", "destPort": 1}]})"),
        graph, true, false);
    EXPECT_TRUE(agree.ok) << agree.message;
}

// A node only the patch creates is resolved on a factory instance with the patch's params applied,
// so a poly Filter's cutoff jack (channel 8) is found, not the mono one.
TEST(AIStateMapperDestParamTest, PatchCreatedNodeIsResolvedWithItsParams) {
    juce::AudioProcessorGraph graph;
    const auto json =
        parse(R"({"nodes": [{"id": 1, "type": "LFO"}, {"id": 2, "type": "Filter", "params": {"poly": true}}],
        "connections": [], "modulations": [{"source": 1, "dest": 2, "destParam": "cutoff", "destPort": 8}]})");
    EXPECT_TRUE(AIStateMapper::validatePatch(json, graph, true, false).ok);
}

// An existing node in merge mode is resolved against the LIVE processor: a live poly Filter's
// cutoff is channel 8, so destPort 1 now disagrees.
TEST(AIStateMapperDestParamTest, ExistingNodeIsResolvedAgainstTheLiveProcessor) {
    juce::AudioProcessorGraph graph;
    auto lfo = graph.addNode(std::make_unique<LFOModule>());
    auto filter = graph.addNode(std::make_unique<FilterModule>());
    auto* poly = findParameterByID(filter->getProcessor(), "poly");
    ASSERT_NE(poly, nullptr);
    poly->setValueNotifyingHost(1.0f);
    ASSERT_EQ(cutoffChannel(filter->getProcessor()), 8);

    const juce::String modulation = "{\"source\": " + juce::String((int)lfo->nodeID.uid) +
                                    ", \"dest\": " + juce::String((int)filter->nodeID.uid) +
                                    ", \"destParam\": \"cutoff\"";
    EXPECT_FALSE(
        AIStateMapper::validatePatch(parse("{\"nodes\": [], \"modulations\": [" + modulation + ", \"destPort\": 1}]}"),
                                     graph, false, false)
            .ok);
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(parse("{\"nodes\": [], \"modulations\": [" + modulation + "}]}"), graph,
                                                false));
    EXPECT_EQ(modulatedChannel(graph, lfo->nodeID, filter->nodeID), 8);
}

// The trusted path is unchanged: our own snapshots always carry destPort, and an absent one is
// still read as port 0 there.
TEST(AIStateMapperDestParamTest, TrustedPathStillReadsAMissingDestPortAsZero) {
    juce::AudioProcessorGraph graph;
    std::map<int, juce::AudioProcessorGraph::NodeID> ids;
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(
        parse(juce::String("{") + kNewNodes + R"(, "modulations": [{"source": 1, "dest": 2}]})"), graph, true, true,
        true, &ids));
    EXPECT_EQ(modulatedChannel(graph, ids[1], ids[2]), 0);
}
