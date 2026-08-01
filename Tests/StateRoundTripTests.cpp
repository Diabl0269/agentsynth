#include "../Source/AI/AIStateMapper.h"
#include "../Source/Modules/AttenuverterModule.h"
#include "../Source/PresetManager.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

static int countAttenuverterNodes(juce::AudioProcessorGraph& graph) {
    int count = 0;
    for (auto* node : graph.getNodes()) {
        if (dynamic_cast<AttenuverterModule*>(node->getProcessor()) != nullptr)
            ++count;
    }
    return count;
}

TEST(StateRoundTripTests, PolyPadJSONRoundTripIsLossless) {
    // Step 1: Load Poly Pad (index 6) into g1, capture its JSON
    juce::AudioProcessorGraph g1;
    ASSERT_TRUE(synth::PresetManager::loadPreset(6, g1)) << "Failed to load Poly Pad preset (index 6)";
    juce::var J1 = synth::AIStateMapper::graphToJSON(g1);

    int g1_nodes = g1.getNumNodes();
    int g1_connections = (int)g1.getConnections().size();
    EXPECT_GT(g1_nodes, 0) << "Poly Pad should have nodes";
    EXPECT_GT(g1_connections, 0) << "Poly Pad should have connections";

    // Step 2: Apply J1 to fresh g2, capture its JSON
    juce::AudioProcessorGraph g2;
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(J1, g2, /*clearExisting=*/true))
        << "applyJSONToGraph J1->g2 failed";
    juce::var J2 = synth::AIStateMapper::graphToJSON(g2);

    int g2_nodes = g2.getNumNodes();
    int g2_connections = (int)g2.getConnections().size();

    // Step 3: Apply J2 to fresh g3, capture its JSON
    juce::AudioProcessorGraph g3;
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(J2, g3, /*clearExisting=*/true))
        << "applyJSONToGraph J2->g3 failed";
    juce::var J3 = synth::AIStateMapper::graphToJSON(g3);

    int g3_nodes = g3.getNumNodes();
    int g3_connections = (int)g3.getConnections().size();

    // Same node and connection counts across all three graphs
    EXPECT_EQ(g1_nodes, g2_nodes) << "g1 has " << g1_nodes << " nodes but g2 has " << g2_nodes;
    EXPECT_EQ(g1_nodes, g3_nodes) << "g1 has " << g1_nodes << " nodes but g3 has " << g3_nodes;
    EXPECT_EQ(g1_connections, g2_connections)
        << "g1 has " << g1_connections << " connections but g2 has " << g2_connections;
    EXPECT_EQ(g1_connections, g3_connections)
        << "g1 has " << g1_connections << " connections but g3 has " << g3_connections;

    // ZERO attenuverter nodes in g2 and g3 (Poly Pad has none — proves no silent promotion)
    EXPECT_EQ(countAttenuverterNodes(g2), 0)
        << "g2 unexpectedly has attenuverter nodes (silent poly-CV promotion detected)";
    EXPECT_EQ(countAttenuverterNodes(g3), 0)
        << "g3 unexpectedly has attenuverter nodes (silent poly-CV promotion detected)";

    // Byte-identical fixpoint: J2 and J3 should produce the same JSON string
    juce::String s2 = juce::JSON::toString(J2);
    juce::String s3 = juce::JSON::toString(J3);
    EXPECT_EQ(s2, s3) << "JSON is not a fixpoint: J2 != J3 (round-trip is not lossless)";
}
