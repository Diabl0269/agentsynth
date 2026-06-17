#include "../Source/AudioEngine.h"
#include "../Source/Modules/ADSRModule.h"
#include "../Source/Modules/AttenuverterModule.h"
#include "../Source/Modules/FilterModule.h"
#include "../Source/Modules/LFOModule.h"
#include "../Source/Modules/OscillatorModule.h"
#include "../Source/Modules/VCAModule.h"
#include "../Source/UI/ModMatrixComponent.h"
#include <gtest/gtest.h>

class ModMatrixTest : public ::testing::Test {
protected:
    void SetUp() override { engine.initialise(); }

    AudioEngine engine;
};

TEST_F(ModMatrixTest, AddModRoutingWorks) {
    auto& graph = engine.getGraph();

    // Clear graph to have a clean state
    graph.clear();

    auto lfoNode = graph.addNode(std::make_unique<LFOModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());

    ASSERT_NE(lfoNode, nullptr);
    ASSERT_NE(filterNode, nullptr);

    engine.addModRouting(lfoNode->nodeID, 0, filterNode->nodeID, 1); // 1 = Cutoff

    auto routings = engine.getActiveModRoutings();
    ASSERT_EQ(routings.size(), 1);
    EXPECT_EQ(routings[0].sourceNodeID, lfoNode->nodeID);
    EXPECT_EQ(routings[0].destNodeID, filterNode->nodeID);
    EXPECT_EQ(routings[0].destChannelIndex, 1);
}

TEST_F(ModMatrixTest, RemoveModRoutingWorks) {
    auto& graph = engine.getGraph();
    graph.clear();

    auto lfoNode = graph.addNode(std::make_unique<LFOModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());

    engine.addModRouting(lfoNode->nodeID, 0, filterNode->nodeID, 1);
    auto routings = engine.getActiveModRoutings();
    ASSERT_EQ(routings.size(), 1);

    engine.removeModRouting(routings[0].attenuverterNodeID);
    EXPECT_EQ(engine.getActiveModRoutings().size(), 0);
}

TEST_F(ModMatrixTest, MetadataPopulation) {
    OscillatorModule osc;
    auto targets = osc.getModulationTargets();

    bool foundPitch = false;
    for (const auto& t : targets) {
        if (t.name == "Pitch")
            foundPitch = true;
    }
    EXPECT_TRUE(foundPitch);
}
TEST_F(ModMatrixTest, SidechainModulationWorks) {
    auto& graph = engine.getGraph();
    graph.clear();

    auto lfoNode = graph.addNode(std::make_unique<LFOModule>());
    auto envNode = graph.addNode(std::make_unique<ADSRModule>("Env"));
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());

    // 1. Primary Routing: LFO -> Attenuverter A -> Filter Cutoff
    engine.addModRouting(lfoNode->nodeID, 0, filterNode->nodeID, 1);
    auto routings = engine.getActiveModRoutings();
    ASSERT_EQ(routings.size(), 1);
    auto attenuverterA = routings[0].attenuverterNodeID;

    // 2. Sidechain Routing: Env -> Attenuverter B -> Attenuverter A (Amount)
    engine.addModRouting(envNode->nodeID, 0, attenuverterA, 1); // 1 = Amount target
    routings = engine.getActiveModRoutings();
    ASSERT_EQ(routings.size(), 2);

    // Verify connections
    bool foundSidechain = false;
    for (const auto& r : routings) {
        if (r.destNodeID == attenuverterA && r.destChannelIndex == 1) {
            foundSidechain = true;
            EXPECT_EQ(r.sourceNodeID, envNode->nodeID);
        }
    }
    EXPECT_TRUE(foundSidechain);

    // 3. Process verification (Mock)
    // We could push samples through the graph here, but a connectivity test
    // already proves the Matrix is doing its job. The rest is internal to AttenuverterModule.
}

TEST_F(ModMatrixTest, BypassModRoutingWorks) {
    auto& graph = engine.getGraph();
    graph.clear();

    auto lfoNode = graph.addNode(std::make_unique<LFOModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());

    engine.addModRouting(lfoNode->nodeID, 0, filterNode->nodeID, 1);
    auto routings = engine.getActiveModRoutings();
    ASSERT_EQ(routings.size(), 1);
    auto attNodeID = routings[0].attenuverterNodeID;

    EXPECT_FALSE(engine.isModBypassed(attNodeID));

    engine.toggleModBypass(attNodeID);
    EXPECT_TRUE(engine.isModBypassed(attNodeID));

    routings = engine.getActiveModRoutings();
    EXPECT_TRUE(routings[0].isBypassed);

    engine.toggleModBypass(attNodeID);
    EXPECT_FALSE(engine.isModBypassed(attNodeID));
}

TEST_F(ModMatrixTest, UpdateModuleNamesAssignsUniqueNames) {
    auto& graph = engine.getGraph();
    graph.clear();

    // Add multiple modules of the same type
    auto lfo1Node = graph.addNode(std::make_unique<LFOModule>());
    auto lfo2Node = graph.addNode(std::make_unique<LFOModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());

    ASSERT_NE(lfo1Node, nullptr);
    ASSERT_NE(lfo2Node, nullptr);
    ASSERT_NE(filterNode, nullptr);

    // Update module names to assign counters
    engine.updateModuleNames();

    // Verify that LFO modules have been renamed with indices
    auto lfo1 = dynamic_cast<LFOModule*>(lfo1Node->getProcessor());
    auto lfo2 = dynamic_cast<LFOModule*>(lfo2Node->getProcessor());
    auto filter = dynamic_cast<FilterModule*>(filterNode->getProcessor());

    ASSERT_NE(lfo1, nullptr);
    ASSERT_NE(lfo2, nullptr);
    ASSERT_NE(filter, nullptr);

    juce::String lfo1Name = lfo1->getName();
    juce::String lfo2Name = lfo2->getName();
    juce::String filterName = filter->getName();

    // Check that names contain the base type and a number
    EXPECT_TRUE(lfo1Name.contains("LFO"));
    EXPECT_TRUE(lfo2Name.contains("LFO"));
    EXPECT_TRUE(filterName.contains("Filter"));

    // They should have different numbers
    EXPECT_NE(lfo1Name, lfo2Name);
}

TEST_F(ModMatrixTest, UpdateModuleNamesStripsExistingNumbers) {
    auto& graph = engine.getGraph();
    graph.clear();

    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    ASSERT_NE(oscNode, nullptr);

    auto osc = dynamic_cast<OscillatorModule*>(oscNode->getProcessor());
    ASSERT_NE(osc, nullptr);

    // Manually set a name with a number
    osc->setModuleName("Oscillator 5");

    // Call updateModuleNames (should renumber from 1)
    engine.updateModuleNames();

    juce::String newName = osc->getName();
    // Should be renumbered to "Oscillator 1" (since it's the only one)
    EXPECT_TRUE(newName.contains("Oscillator"));
    EXPECT_TRUE(newName.endsWith("1"));
}

TEST_F(ModMatrixTest, UpdateModuleNamesHandlesAttenuvertersAsModSlots) {
    auto& graph = engine.getGraph();
    graph.clear();

    auto lfoNode = graph.addNode(std::make_unique<LFOModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());

    ASSERT_NE(lfoNode, nullptr);
    ASSERT_NE(filterNode, nullptr);

    // Add a routing, which creates an Attenuverter module
    engine.addModRouting(lfoNode->nodeID, 0, filterNode->nodeID, 1);

    auto routings = engine.getActiveModRoutings();
    ASSERT_EQ(routings.size(), 1);

    auto attNodeID = routings[0].attenuverterNodeID;
    auto* attNode = graph.getNodeForId(attNodeID);
    ASSERT_NE(attNode, nullptr);

    auto att = dynamic_cast<AttenuverterModule*>(attNode->getProcessor());
    ASSERT_NE(att, nullptr);

    // Update module names
    engine.updateModuleNames();

    juce::String attName = att->getName();
    // Attenuverter modules should be renamed to "Mod Slot"
    EXPECT_TRUE(attName.contains("Mod Slot"));
}

TEST_F(ModMatrixTest, AddEmptyModRoutingCreatesAttenuverter) {
    auto& graph = engine.getGraph();
    graph.clear();

    EXPECT_EQ(engine.getActiveModRoutings().size(), 0);

    engine.addEmptyModRouting();

    // Should have created an Attenuverter node with no connections
    auto routings = engine.getActiveModRoutings();
    EXPECT_EQ(routings.size(), 1);
    // Empty routing should have null/zero node IDs for source and dest
    EXPECT_EQ(routings[0].sourceNodeID, juce::AudioProcessorGraph::NodeID());
    EXPECT_EQ(routings[0].destNodeID, juce::AudioProcessorGraph::NodeID());
}

TEST_F(ModMatrixTest, GetActiveModRoutingsWithMultipleRoutings) {
    auto& graph = engine.getGraph();
    graph.clear();

    auto lfoNode = graph.addNode(std::make_unique<LFOModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    auto vcaNode = graph.addNode(std::make_unique<VCAModule>());
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());

    ASSERT_NE(lfoNode, nullptr);
    ASSERT_NE(filterNode, nullptr);
    ASSERT_NE(vcaNode, nullptr);
    ASSERT_NE(oscNode, nullptr);

    // Add multiple routings (using valid channel indices for each module)
    engine.addModRouting(lfoNode->nodeID, 0, filterNode->nodeID, 1);
    engine.addModRouting(oscNode->nodeID, 0, vcaNode->nodeID, 1);
    engine.addModRouting(lfoNode->nodeID, 0, filterNode->nodeID, 2);

    auto routings = engine.getActiveModRoutings();

    // Should have 3 active routings
    ASSERT_EQ(routings.size(), 3);

    // Verify all routings are present
    bool foundLfoToFilterCutoff = false;
    bool foundOscToVca = false;
    bool foundLfoToFilterRes = false;

    for (const auto& routing : routings) {
        if (routing.sourceNodeID == lfoNode->nodeID && routing.destNodeID == filterNode->nodeID &&
            routing.destChannelIndex == 1) {
            foundLfoToFilterCutoff = true;
        }
        if (routing.sourceNodeID == oscNode->nodeID && routing.destNodeID == vcaNode->nodeID &&
            routing.destChannelIndex == 1) {
            foundOscToVca = true;
        }
        if (routing.sourceNodeID == lfoNode->nodeID && routing.destNodeID == filterNode->nodeID &&
            routing.destChannelIndex == 2) {
            foundLfoToFilterRes = true;
        }
    }

    EXPECT_TRUE(foundLfoToFilterCutoff);
    EXPECT_TRUE(foundOscToVca);
    EXPECT_TRUE(foundLfoToFilterRes);
}

TEST_F(ModMatrixTest, RemoveModRoutingWithNonExistentID) {
    auto& graph = engine.getGraph();
    graph.clear();

    auto lfoNode = graph.addNode(std::make_unique<LFOModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());

    engine.addModRouting(lfoNode->nodeID, 0, filterNode->nodeID, 1);
    auto routings = engine.getActiveModRoutings();
    ASSERT_EQ(routings.size(), 1);

    // Try to remove a routing with a non-existent ID
    // Create a fake ID that doesn't exist
    juce::AudioProcessorGraph::NodeID fakeID(9999);
    engine.removeModRouting(fakeID);

    // Original routing should still be there
    routings = engine.getActiveModRoutings();
    EXPECT_EQ(routings.size(), 1);
}

TEST_F(ModMatrixTest, IsModBypassedWithNonExistentID) {
    auto& graph = engine.getGraph();
    graph.clear();

    // Query bypass state for a node that doesn't exist
    juce::AudioProcessorGraph::NodeID fakeID(9999);
    bool bypassed = engine.isModBypassed(fakeID);

    // Should return false (default state) for non-existent node
    EXPECT_FALSE(bypassed);
}

TEST_F(ModMatrixTest, ToggleModBypassWithNonExistentID) {
    auto& graph = engine.getGraph();
    graph.clear();

    auto lfoNode = graph.addNode(std::make_unique<LFOModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());

    engine.addModRouting(lfoNode->nodeID, 0, filterNode->nodeID, 1);
    auto routings = engine.getActiveModRoutings();
    ASSERT_EQ(routings.size(), 1);

    // Try to toggle bypass on a non-existent node (should not crash)
    juce::AudioProcessorGraph::NodeID fakeID(9999);
    engine.toggleModBypass(fakeID);

    // Original routing should still have bypass state unchanged
    routings = engine.getActiveModRoutings();
    EXPECT_FALSE(routings[0].isBypassed);
}

TEST_F(ModMatrixTest, AttenuverterAmountParameterIsAtIndex1) {
    auto& graph = engine.getGraph();
    graph.clear();

    auto lfoNode = graph.addNode(std::make_unique<LFOModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());

    ASSERT_NE(lfoNode, nullptr);
    ASSERT_NE(filterNode, nullptr);

    engine.addModRouting(lfoNode->nodeID, 0, filterNode->nodeID, 1);

    auto routings = engine.getActiveModRoutings();
    ASSERT_EQ(routings.size(), 1);

    auto attNodeID = routings[0].attenuverterNodeID;
    auto* attNode = graph.getNodeForId(attNodeID);
    ASSERT_NE(attNode, nullptr);

    auto* attProcessor = attNode->getProcessor();
    ASSERT_NE(attProcessor, nullptr);

    auto params = attProcessor->getParameters();
    ASSERT_GE(params.size(), 2);

    // Verify params[1] is AudioParameterFloat
    auto* amountParam = dynamic_cast<juce::AudioParameterFloat*>(params[1]);
    ASSERT_NE(amountParam, nullptr);

    // Verify parameter ID contains "amount"
    juce::String paramID = amountParam->getParameterID();
    EXPECT_TRUE(paramID.containsIgnoreCase("amount"));
}

TEST_F(ModMatrixTest, ModAmountDefaultIsOne) {
    auto& graph = engine.getGraph();
    graph.clear();

    auto lfoNode = graph.addNode(std::make_unique<LFOModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());

    ASSERT_NE(lfoNode, nullptr);
    ASSERT_NE(filterNode, nullptr);

    engine.addModRouting(lfoNode->nodeID, 0, filterNode->nodeID, 1);

    auto routings = engine.getActiveModRoutings();
    ASSERT_EQ(routings.size(), 1);

    auto attNodeID = routings[0].attenuverterNodeID;
    auto* attNode = graph.getNodeForId(attNodeID);
    ASSERT_NE(attNode, nullptr);

    auto* attProcessor = attNode->getProcessor();
    ASSERT_NE(attProcessor, nullptr);

    auto params = attProcessor->getParameters();
    ASSERT_GE(params.size(), 2);

    auto* amountParam = dynamic_cast<juce::AudioParameterFloat*>(params[1]);
    ASSERT_NE(amountParam, nullptr);

    // Default amount should be 1.0
    EXPECT_FLOAT_EQ(amountParam->get(), 1.0f);
}

TEST_F(ModMatrixTest, AmountParameterRoundTrip) {
    auto& graph = engine.getGraph();
    graph.clear();

    auto lfoNode = graph.addNode(std::make_unique<LFOModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());

    ASSERT_NE(lfoNode, nullptr);
    ASSERT_NE(filterNode, nullptr);

    engine.addModRouting(lfoNode->nodeID, 0, filterNode->nodeID, 1);

    auto routings = engine.getActiveModRoutings();
    ASSERT_EQ(routings.size(), 1);

    auto attNodeID = routings[0].attenuverterNodeID;
    auto* attNode = graph.getNodeForId(attNodeID);
    ASSERT_NE(attNode, nullptr);

    auto* attProcessor = attNode->getProcessor();
    ASSERT_NE(attProcessor, nullptr);

    auto params = attProcessor->getParameters();
    ASSERT_GE(params.size(), 2);

    auto* amountParam = dynamic_cast<juce::AudioParameterFloat*>(params[1]);
    ASSERT_NE(amountParam, nullptr);

    // Test various values
    float testValues[] = {-1.0f, 0.0f, 0.5f, 1.0f};

    for (float testValue : testValues) {
        amountParam->setValueNotifyingHost(amountParam->convertTo0to1(testValue));
        EXPECT_NEAR(amountParam->get(), testValue, 0.01f);
    }
}

TEST_F(ModMatrixTest, BypassParameterIsAtIndex0) {
    auto& graph = engine.getGraph();
    graph.clear();

    auto lfoNode = graph.addNode(std::make_unique<LFOModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());

    ASSERT_NE(lfoNode, nullptr);
    ASSERT_NE(filterNode, nullptr);

    engine.addModRouting(lfoNode->nodeID, 0, filterNode->nodeID, 1);

    auto routings = engine.getActiveModRoutings();
    ASSERT_EQ(routings.size(), 1);

    auto attNodeID = routings[0].attenuverterNodeID;
    auto* attNode = graph.getNodeForId(attNodeID);
    ASSERT_NE(attNode, nullptr);

    auto* attProcessor = attNode->getProcessor();
    ASSERT_NE(attProcessor, nullptr);

    auto params = attProcessor->getParameters();
    ASSERT_GE(params.size(), 1);

    // Verify params[0] is AudioParameterBool (inherited from ModuleBase)
    auto* bypassParam = dynamic_cast<juce::AudioParameterBool*>(params[0]);
    ASSERT_NE(bypassParam, nullptr);

    // Verify initial value is false
    EXPECT_FALSE(bypassParam->get());

    // Set to true and verify
    bypassParam->setValueNotifyingHost(1.0f);
    EXPECT_TRUE(bypassParam->get());

    // Set back to false and verify
    bypassParam->setValueNotifyingHost(0.0f);
    EXPECT_FALSE(bypassParam->get());
}

// ---------------------------------------------------------------------------
// ModMatrixComponent UI — zebra, hover, row height
// ---------------------------------------------------------------------------

TEST_F(ModMatrixTest, RowHeightIs48) {
    // kRowHeight is a static constexpr — verify it equals 48.
    EXPECT_EQ(ModMatrixComponent::kRowHeight, 48);
}

TEST_F(ModMatrixTest, ZebraAlternates) {
    // Even rows (0, 2, …) are NOT zebra; odd rows (1, 3, …) ARE zebra.
    EXPECT_FALSE(ModMatrixComponent::isZebraRow(0));
    EXPECT_TRUE(ModMatrixComponent::isZebraRow(1));
    EXPECT_FALSE(ModMatrixComponent::isZebraRow(2));
    EXPECT_TRUE(ModMatrixComponent::isZebraRow(3));
    EXPECT_FALSE(ModMatrixComponent::isZebraRow(100));
    EXPECT_TRUE(ModMatrixComponent::isZebraRow(101));
}

TEST_F(ModMatrixTest, HoverStateUpdates) {
    ModMatrixComponent matrix(engine);

    // Default: no row hovered.
    EXPECT_EQ(matrix.getHoveredRow(), -1);

    // Setting a row index stores it.
    matrix.setHoveredRow(2);
    EXPECT_EQ(matrix.getHoveredRow(), 2);

    // Setting the same value is a no-op (no crash).
    matrix.setHoveredRow(2);
    EXPECT_EQ(matrix.getHoveredRow(), 2);

    // Clearing hover (mouseExit equivalent) resets to -1.
    matrix.setHoveredRow(-1);
    EXPECT_EQ(matrix.getHoveredRow(), -1);
}

TEST_F(ModMatrixTest, ModMatrixComponentPaintSmokeTest) {
    // Construct a component with a couple of routings and exercise updateRowsFromGraph
    // to confirm no crash during the layout pass (kRowHeight applied).
    ModMatrixComponent matrix(engine);

    auto& graph = engine.getGraph();
    graph.clear();

    auto lfoNode = graph.addNode(std::make_unique<LFOModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    ASSERT_NE(lfoNode, nullptr);
    ASSERT_NE(filterNode, nullptr);

    engine.addModRouting(lfoNode->nodeID, 0, filterNode->nodeID, 1);
    engine.addModRouting(lfoNode->nodeID, 0, filterNode->nodeID, 2);

    // setBounds triggers resized() which applies kRowHeight layout.
    matrix.setBounds(0, 0, 600, 400);
    matrix.updateRowsFromGraph();

    // Verify hover state is still clean after update.
    EXPECT_EQ(matrix.getHoveredRow(), -1);
}

TEST_F(ModMatrixTest, HoverResetsAfterRowRemoval) {
    // Regression: when a row is erased and remaining rows shift indices, hoveredRow_
    // must be cleared so the wrong (now-shifted) row is not painted as hovered.
    ModMatrixComponent matrix(engine);

    auto& graph = engine.getGraph();
    graph.clear();

    auto lfoNode = graph.addNode(std::make_unique<LFOModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    ASSERT_NE(lfoNode, nullptr);
    ASSERT_NE(filterNode, nullptr);

    engine.addModRouting(lfoNode->nodeID, 0, filterNode->nodeID, 1);
    engine.addModRouting(lfoNode->nodeID, 0, filterNode->nodeID, 2);

    matrix.setBounds(0, 0, 600, 400);
    matrix.updateRowsFromGraph();

    // Hover over row 1 (the second row).
    matrix.setHoveredRow(1);
    ASSERT_EQ(matrix.getHoveredRow(), 1);

    // Remove one of the routings so the row count drops from 2 to 1.
    auto routings = engine.getActiveModRoutings();
    ASSERT_GE(routings.size(), 1u);
    engine.removeModRouting(routings[0].attenuverterNodeID);

    // updateRowsFromGraph detects the removed row (componentsChanged = true) and
    // must reset hoveredRow_ to -1 before the layout pass.
    matrix.updateRowsFromGraph();

    EXPECT_EQ(matrix.getHoveredRow(), -1);
}
