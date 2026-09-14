// Default-preset wiring test for the L/R stereo split on voice modules (issue #219): confirms the
// right leg is patched all the way from the Oscillator through to the FX chain.

#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include "Modules/VCAModule.h"
#include "PresetManager.h"
#include <gtest/gtest.h>

// ---------------------------------------------------------------------------
// The default preset carries stereo all the way to the FX chain
// ---------------------------------------------------------------------------

TEST(StereoDefaultPreset, RightLegIsWiredFromOscillatorThroughToTheFX) {
    juce::AudioProcessorGraph graph;
    ASSERT_TRUE(synth::PresetManager::loadDefaultPreset(graph));

    auto nodeNamed = [&graph](const juce::String& name) -> juce::AudioProcessorGraph::NodeID {
        for (auto* node : graph.getNodes())
            if (node->getProcessor()->getName() == name)
                return node->nodeID;
        return {};
    };

    const auto osc = nodeNamed("Oscillator");
    const auto filter = nodeNamed("Filter");
    const auto vca = nodeNamed("VCA");
    const auto dist = nodeNamed("Distortion");
    ASSERT_NE(osc.uid, 0u);
    ASSERT_NE(filter.uid, 0u);
    ASSERT_NE(vca.uid, 0u);
    ASSERT_NE(dist.uid, 0u);

    // Left leg: unchanged from before #219.
    EXPECT_TRUE(graph.isConnected({{osc, 0}, {filter, 0}}));
    EXPECT_TRUE(graph.isConnected({{filter, 0}, {vca, 0}}));
    EXPECT_TRUE(graph.isConnected({{vca, 0}, {dist, 0}}));

    // Right leg: each module's own kRightBase block, never ch1 (which is CV on all three).
    EXPECT_TRUE(graph.isConnected({{osc, OscillatorModule::kRightBase}, {filter, FilterModule::kRightBase}}));
    EXPECT_TRUE(graph.isConnected({{filter, FilterModule::kRightBase}, {vca, VCAModule::kRightBase}}));
    EXPECT_TRUE(graph.isConnected({{vca, VCAModule::kRightBase}, {dist, 1}}))
        << "the FX chain takes a contiguous ch0/ch1 pair, so the right leg lands on Distortion ch1";

    // The gain/cutoff CV wires must not have been displaced onto an audio leg.
    EXPECT_FALSE(graph.isConnected({{osc, 0}, {filter, 1}}));
    EXPECT_FALSE(graph.isConnected({{filter, 0}, {vca, 1}}));
}
