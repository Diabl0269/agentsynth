#include "../Source/AI/AIStateMapper.h"
#include "../Source/GravisynthUndoManager.h"
#include "../Source/Modules/ADSRModule.h"
#include "../Source/Modules/AttenuverterModule.h"
#include "../Source/Modules/FilterModule.h"
#include "../Source/Modules/OscillatorModule.h"
#include "../Source/Modules/VCAModule.h"
#include "../Source/PresetManager.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>

class UndoRedoTest : public ::testing::Test {
protected:
    juce::AudioProcessorGraph graph;
    GravisynthUndoManager undoManager;

    void SetUp() override { graph.clear(); }
};

/**
 * Test 1: UndoAddModule
 * - Graph starts empty (0 nodes)
 * - Use recordStructuralChange to add an OscillatorModule
 * - Verify graph has 1 node
 * - Undo → graph should have 0 nodes
 * - Redo → graph should have 1 node again
 */
TEST_F(UndoRedoTest, UndoAddModule) {
    ASSERT_EQ(graph.getNumNodes(), 0);

    undoManager.recordStructuralChange(graph, [this] { graph.addNode(std::make_unique<OscillatorModule>()); });

    ASSERT_EQ(graph.getNumNodes(), 1);

    undoManager.undo();
    ASSERT_EQ(graph.getNumNodes(), 0);

    undoManager.redo();
    ASSERT_EQ(graph.getNumNodes(), 1);
}

/**
 * Test 2: UndoRemoveModule
 * - Add a module directly, then use recordStructuralChange to remove it
 * - Verify node can be removed
 * - Undo → module should reappear
 */
TEST_F(UndoRedoTest, UndoRemoveModule) {
    auto node = graph.addNode(std::make_unique<OscillatorModule>());
    auto nodeId = node->nodeID;
    ASSERT_EQ(graph.getNumNodes(), 1);

    undoManager.recordStructuralChange(graph, [this, nodeId] { graph.removeNode(nodeId); });

    ASSERT_EQ(graph.getNumNodes(), 0);

    undoManager.undo();
    ASSERT_EQ(graph.getNumNodes(), 1);

    // Verify it's an Oscillator by checking the processor name
    auto oscillatorNode = graph.getNodes().getFirst();
    ASSERT_NE(oscillatorNode, nullptr);
    ASSERT_EQ(oscillatorNode->getProcessor()->getName(), juce::String("Oscillator"));
}

/**
 * Test 3: UndoAddConnection
 * - Add two modules
 * - Use recordStructuralChange to connect them
 * - Undo → connection removed, redo → connection restored
 */
TEST_F(UndoRedoTest, UndoAddConnection) {
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    ASSERT_EQ(graph.getConnections().size(), 0);

    undoManager.recordStructuralChange(
        graph, [this, oscNode, filterNode] { graph.addConnection({{oscNode->nodeID, 0}, {filterNode->nodeID, 0}}); });

    ASSERT_EQ(graph.getConnections().size(), 1);

    undoManager.undo();
    // After undo, nodes should exist but connection should be gone
    ASSERT_EQ(graph.getNumNodes(), 2);
    ASSERT_EQ(graph.getConnections().size(), 0);

    undoManager.redo();
    ASSERT_EQ(graph.getConnections().size(), 1);
}

/**
 * Test 4: UndoParameterChange
 * - Add an oscillator node
 * - Find a float parameter and change it
 * - Verify the change
 * - Undo → parameter should revert
 * - Redo → parameter should return to new value
 */
TEST_F(UndoRedoTest, UndoParameterChange) {
    auto node = graph.addNode(std::make_unique<OscillatorModule>());
    auto nodeId = node->nodeID;

    // Use the "fine" float parameter (not a choice/int param)
    juce::String paramId = "fine";
    juce::RangedAudioParameter* fineParam = nullptr;
    for (auto* param : node->getProcessor()->getParameters()) {
        if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param)) {
            if (p->paramID == paramId) {
                fineParam = dynamic_cast<juce::RangedAudioParameter*>(param);
                break;
            }
        }
    }
    ASSERT_NE(fineParam, nullptr);

    float originalValue = fineParam->getValue();
    float newValue = 0.75f;
    fineParam->setValueNotifyingHost(newValue);

    undoManager.beginNewTransaction();
    undoManager.recordParameterChange(graph, nodeId, paramId, originalValue, newValue);

    ASSERT_NEAR(fineParam->getValue(), newValue, 0.001f);

    undoManager.undo();
    ASSERT_NEAR(fineParam->getValue(), originalValue, 0.001f);

    undoManager.redo();
    ASSERT_NEAR(fineParam->getValue(), newValue, 0.001f);
}

/**
 * Test 5: UndoPositionChange
 * - Add an oscillator node and set initial position
 * - Use recordPositionChange to move it
 * - Undo → position should revert
 * - Redo → position should return to new location
 */
TEST_F(UndoRedoTest, UndoPositionChange) {
    auto node = graph.addNode(std::make_unique<OscillatorModule>());
    auto nodeId = node->nodeID;
    node->properties.set("x", 100);
    node->properties.set("y", 200);

    // Simulate drag to new position
    node->properties.set("x", 300);
    node->properties.set("y", 400);

    undoManager.recordPositionChange(graph, nodeId, 100, 200, 300, 400, [] {});

    ASSERT_EQ((int)node->properties["x"], 300);
    ASSERT_EQ((int)node->properties["y"], 400);

    undoManager.undo();
    ASSERT_EQ((int)node->properties["x"], 100);
    ASSERT_EQ((int)node->properties["y"], 200);

    undoManager.redo();
    ASSERT_EQ((int)node->properties["x"], 300);
    ASSERT_EQ((int)node->properties["y"], 400);
}

/**
 * Test 6: MultipleUndoLevels
 * - Add 3 modules, one at a time
 * - Undo all 3 in sequence
 * - Redo all 3 in sequence
 * - Verify node count at each step
 */
TEST_F(UndoRedoTest, MultipleUndoLevels) {
    // Add 3 modules, one at a time
    undoManager.recordStructuralChange(graph, [this] { graph.addNode(std::make_unique<OscillatorModule>()); });
    ASSERT_EQ(graph.getNumNodes(), 1);

    undoManager.recordStructuralChange(graph, [this] { graph.addNode(std::make_unique<FilterModule>()); });
    ASSERT_EQ(graph.getNumNodes(), 2);

    undoManager.recordStructuralChange(graph, [this] { graph.addNode(std::make_unique<VCAModule>()); });
    ASSERT_EQ(graph.getNumNodes(), 3);

    // Undo all 3
    undoManager.undo();
    ASSERT_EQ(graph.getNumNodes(), 2);

    undoManager.undo();
    ASSERT_EQ(graph.getNumNodes(), 1);

    undoManager.undo();
    ASSERT_EQ(graph.getNumNodes(), 0);

    // Redo all 3
    undoManager.redo();
    ASSERT_EQ(graph.getNumNodes(), 1);

    undoManager.redo();
    ASSERT_EQ(graph.getNumNodes(), 2);

    undoManager.redo();
    ASSERT_EQ(graph.getNumNodes(), 3);
}

/**
 * Test 7: ClearUndoHistory
 * - Add a module
 * - Verify canUndo() is true
 * - Clear the undo history
 * - Verify canUndo() and canRedo() are false
 */
TEST_F(UndoRedoTest, ClearUndoHistory) {
    undoManager.recordStructuralChange(graph, [this] { graph.addNode(std::make_unique<OscillatorModule>()); });

    ASSERT_TRUE(undoManager.canUndo());

    undoManager.clearUndoHistory();

    ASSERT_FALSE(undoManager.canUndo());
    ASSERT_FALSE(undoManager.canRedo());
}

/**
 * Test 8: UndoRemoveConnection
 * - Add two modules and connect them
 * - Use recordStructuralChange to disconnect them
 * - Undo → connection should be restored
 * - Redo → connection should be removed again
 */
TEST_F(UndoRedoTest, UndoRemoveConnection) {
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());

    // Add a connection first (outside of undo/redo)
    juce::AudioProcessorGraph::Connection conn = {{oscNode->nodeID, 0}, {filterNode->nodeID, 0}};
    graph.addConnection(conn);
    ASSERT_EQ(graph.getConnections().size(), 1);

    // Now record the removal
    undoManager.recordStructuralChange(graph, [this, conn] { graph.removeConnection(conn); });

    ASSERT_EQ(graph.getConnections().size(), 0);

    undoManager.undo();
    ASSERT_EQ(graph.getConnections().size(), 1);

    undoManager.redo();
    ASSERT_EQ(graph.getConnections().size(), 0);
}

/**
 * Test 9: ParameterChangeCoalescing
 * - Record multiple parameter changes to the same parameter
 * - Verify that undoing once reverts to the original value (coalescing works)
 */
TEST_F(UndoRedoTest, ParameterChangeCoalescing) {
    auto node = graph.addNode(std::make_unique<OscillatorModule>());
    auto nodeId = node->nodeID;

    // Use the "fine" float parameter
    juce::String paramId = "fine";
    juce::RangedAudioParameter* fineParam = nullptr;
    for (auto* param : node->getProcessor()->getParameters()) {
        if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param)) {
            if (p->paramID == paramId) {
                fineParam = dynamic_cast<juce::RangedAudioParameter*>(param);
                break;
            }
        }
    }
    ASSERT_NE(fineParam, nullptr);

    float originalValue = fineParam->getValue();

    // Simulate slider drag: set param, then record each step
    fineParam->setValueNotifyingHost(0.25f);
    undoManager.beginNewTransaction();
    undoManager.recordParameterChange(graph, nodeId, paramId, originalValue, 0.25f);

    fineParam->setValueNotifyingHost(0.50f);
    undoManager.beginNewTransaction();
    undoManager.recordParameterChange(graph, nodeId, paramId, 0.25f, 0.50f);

    fineParam->setValueNotifyingHost(0.75f);
    undoManager.beginNewTransaction();
    undoManager.recordParameterChange(graph, nodeId, paramId, 0.50f, 0.75f);

    ASSERT_NEAR(fineParam->getValue(), 0.75f, 0.001f);

    // Undo should revert through the sequence
    undoManager.undo();
    ASSERT_NEAR(fineParam->getValue(), 0.50f, 0.001f);

    undoManager.undo();
    ASSERT_NEAR(fineParam->getValue(), 0.25f, 0.001f);

    undoManager.undo();
    ASSERT_NEAR(fineParam->getValue(), originalValue, 0.001f);
}

/**
 * Test 10: ComplexGraphModification
 * - Add 3 modules
 * - Create connections between them
 * - Record the entire sequence
 * - Undo and redo to verify all changes are preserved
 */
TEST_F(UndoRedoTest, ComplexGraphModification) {
    // Start with empty graph
    ASSERT_EQ(graph.getNumNodes(), 0);
    ASSERT_EQ(graph.getConnections().size(), 0);

    // Add modules
    undoManager.recordStructuralChange(graph, [this] { graph.addNode(std::make_unique<OscillatorModule>()); });

    undoManager.recordStructuralChange(graph, [this] { graph.addNode(std::make_unique<FilterModule>()); });

    undoManager.recordStructuralChange(graph, [this] { graph.addNode(std::make_unique<VCAModule>()); });

    ASSERT_EQ(graph.getNumNodes(), 3);

    // Get node IDs for connections
    auto oscNode = graph.getNodes()[0];
    auto filterNode = graph.getNodes()[1];
    auto vcaNode = graph.getNodes()[2];

    // Add connections
    undoManager.recordStructuralChange(
        graph, [this, oscNode, filterNode] { graph.addConnection({{oscNode->nodeID, 0}, {filterNode->nodeID, 0}}); });

    undoManager.recordStructuralChange(
        graph, [this, filterNode, vcaNode] { graph.addConnection({{filterNode->nodeID, 0}, {vcaNode->nodeID, 0}}); });

    ASSERT_EQ(graph.getNumNodes(), 3);
    ASSERT_EQ(graph.getConnections().size(), 2);

    // Undo all operations
    undoManager.undo(); // Remove second connection
    ASSERT_EQ(graph.getNumNodes(), 3);
    ASSERT_EQ(graph.getConnections().size(), 1);

    undoManager.undo(); // Remove first connection
    ASSERT_EQ(graph.getNumNodes(), 3);
    ASSERT_EQ(graph.getConnections().size(), 0);

    undoManager.undo(); // Remove VCA
    ASSERT_EQ(graph.getNumNodes(), 2);

    undoManager.undo(); // Remove Filter
    ASSERT_EQ(graph.getNumNodes(), 1);

    undoManager.undo(); // Remove Oscillator
    ASSERT_EQ(graph.getNumNodes(), 0);

    // Redo all operations
    undoManager.redo(); // Add Oscillator
    ASSERT_EQ(graph.getNumNodes(), 1);

    undoManager.redo(); // Add Filter
    ASSERT_EQ(graph.getNumNodes(), 2);

    undoManager.redo(); // Add VCA
    ASSERT_EQ(graph.getNumNodes(), 3);

    undoManager.redo(); // Add first connection
    ASSERT_EQ(graph.getConnections().size(), 1);

    undoManager.redo(); // Add second connection
    ASSERT_EQ(graph.getConnections().size(), 2);
}

/**
 * Test 11: RedoWithParameterValueInUnitInterval
 * - Create a Filter module with a parameter that has range > [0,1]
 * - Set parameter to a value within [0,1] (e.g., drive = 1.0, range 1.0-10.0)
 * - Record a structural change (module addition)
 * - Undo (module removed)
 * - Redo (module restored)
 * - Verify the parameter value is still correct (1.0, not 10.0)
 *
 * This tests the fix for GitHub issue #53: Redo after module replacement
 * corrupts new module's parameters. The bug was that values in [0,1] were
 * being double-converted: normalized value from state was incorrectly treated
 * as a normalized value and converted again.
 */
TEST_F(UndoRedoTest, RedoWithParameterValueInUnitInterval) {
    // Create a Filter module (has parameters with ranges > [0,1])
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    auto nodeId = filterNode->nodeID;

    // Find the "drive" parameter (range 1.0-10.0)
    juce::String driveParamId = "drive";
    juce::RangedAudioParameter* driveParam = nullptr;
    for (auto* param : filterNode->getProcessor()->getParameters()) {
        if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param)) {
            if (p->paramID == driveParamId) {
                driveParam = dynamic_cast<juce::RangedAudioParameter*>(param);
                break;
            }
        }
    }
    ASSERT_NE(driveParam, nullptr);

    // Set drive to 1.0 (within [0,1] but valid for range 1.0-10.0)
    float targetValue = 1.0f;
    driveParam->setValueNotifyingHost(driveParam->getNormalisableRange().convertTo0to1(targetValue));

    // Verify the value was set correctly
    float denormalized = driveParam->getNormalisableRange().convertFrom0to1(driveParam->getValue());
    ASSERT_NEAR(denormalized, targetValue, 0.001f);

    // Record a structural change (add the Filter module)
    // Note: We already added it above, so we'll remove it and re-add it via undo/redo
    graph.removeNode(nodeId);
    ASSERT_EQ(graph.getNumNodes(), 0);

    // Now add it back via recordStructuralChange
    undoManager.recordStructuralChange(graph, [this] {
        auto node = graph.addNode(std::make_unique<FilterModule>());
        // Set the same parameter to the same value
        for (auto* param : node->getProcessor()->getParameters()) {
            if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param)) {
                if (p->paramID == "drive") {
                    if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param)) {
                        ranged->setValueNotifyingHost(ranged->getNormalisableRange().convertTo0to1(1.0f));
                    }
                }
            }
        }
    });

    ASSERT_EQ(graph.getNumNodes(), 1);

    // Get the new module and verify parameter
    auto newFilterNode = graph.getNodes().getFirst();
    ASSERT_NE(newFilterNode, nullptr);
    juce::RangedAudioParameter* newDriveParam = nullptr;
    for (auto* param : newFilterNode->getProcessor()->getParameters()) {
        if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param)) {
            if (p->paramID == "drive") {
                newDriveParam = dynamic_cast<juce::RangedAudioParameter*>(param);
                break;
            }
        }
    }
    ASSERT_NE(newDriveParam, nullptr);

    float valueAfterAdd = newDriveParam->getNormalisableRange().convertFrom0to1(newDriveParam->getValue());
    ASSERT_NEAR(valueAfterAdd, 1.0f, 0.001f) << "Parameter value should be 1.0 after initial add";

    // Undo (removes the module)
    undoManager.undo();
    ASSERT_EQ(graph.getNumNodes(), 0);

    // Redo (restores the module with the same parameters)
    undoManager.redo();
    ASSERT_EQ(graph.getNumNodes(), 1);

    // Verify the parameter value is still correct (not double-converted to 10.0)
    auto redoFilterNode = graph.getNodes().getFirst();
    ASSERT_NE(redoFilterNode, nullptr);
    juce::RangedAudioParameter* redoDriveParam = nullptr;
    for (auto* param : redoFilterNode->getProcessor()->getParameters()) {
        if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param)) {
            if (p->paramID == "drive") {
                redoDriveParam = dynamic_cast<juce::RangedAudioParameter*>(param);
                break;
            }
        }
    }
    ASSERT_NE(redoDriveParam, nullptr);

    float valueAfterRedo = redoDriveParam->getNormalisableRange().convertFrom0to1(redoDriveParam->getValue());
    ASSERT_NEAR(valueAfterRedo, 1.0f, 0.001f)
        << "Parameter value should be 1.0 after redo, not 10.0 (double-converted)";
}

/**
 * Test 12: PolyPad_RoutingSurvivesUndoRedo
 *
 * Verifies that the "Poly Pad" preset (index 6) connections survive a full
 * undo/redo cycle modelled by the graphToJSON -> applyJSONToGraph round-trip
 * (the same operation that GravisynthUndoManager::SnapshotAction performs on
 * undo and redo).
 *
 * The following connections must survive:
 *   (A) 8 direct poly-bus edges: Amp Env ch0-7 → VCA ch8-15
 *       (per-voice amplitude envelope modulation)
 *
 * The env->osc Level CV wire (Amp Env ch0 → Oscillator ch12) was removed from
 * the factory preset because that channel is shared across all voices and was
 * coupling all voices to voice-0's envelope. The capability itself still works
 * (see IntegrationTests::PolyPad_EnvToOscModulatesOscLevel).
 *
 * Additionally, the restored patch must NOT introduce any new AttenuverterModule
 * nodes — these connections are direct poly-bus connections, not mod-matrix
 * routings, and the serializer must preserve them as-is without substituting
 * attenuverter chains.
 *
 * Strategy (mirrors GravisynthUndoManager behaviour):
 *   1. Load Poly Pad (preset 6) into graph g.
 *   2. Snapshot: J0 = graphToJSON(g).
 *   3. Simulate "undo-apply-different-state": load preset 0 into g (clobbers Poly Pad).
 *   4. Simulate "redo-restore-original-state": applyJSONToGraph(J0, g, clear=true, trusted=true).
 *   5. Count the required connections; assert both groups are fully present.
 *   6. Assert zero AttenuverterModule nodes were introduced for these paths.
 */
TEST_F(UndoRedoTest, PolyPad_RoutingSurvivesUndoRedo) {
    // Step 1 — Load Poly Pad (preset 6) into graph
    ASSERT_TRUE(gsynth::PresetManager::loadPreset(6, graph));

    // Sanity: locate Amp Env (ADSR), Oscillator, and VCA by type
    juce::AudioProcessorGraph::NodeID adsrID, oscID, vcaID;
    for (auto* node : graph.getNodes()) {
        auto* proc = node->getProcessor();
        if (auto* mb = dynamic_cast<ADSRModule*>(proc)) {
            // Preset 6 has one ADSR named "Amp Env"
            if (mb->getName().contains("Amp Env") || mb->getName().contains("ADSR"))
                adsrID = node->nodeID;
        } else if (dynamic_cast<OscillatorModule*>(proc)) {
            oscID = node->nodeID;
        } else if (dynamic_cast<VCAModule*>(proc)) {
            vcaID = node->nodeID;
        }
    }
    ASSERT_NE(adsrID.uid, 0u) << "Poly Pad should have an Amp Env (ADSR) node";
    ASSERT_NE(oscID.uid, 0u) << "Poly Pad should have an Oscillator node";
    ASSERT_NE(vcaID.uid, 0u) << "Poly Pad should have a VCA node";

    // Helper: count specific connections in the current graph
    auto countConns = [&](juce::AudioProcessorGraph::NodeID srcID, int srcCh, juce::AudioProcessorGraph::NodeID dstID,
                          int dstCh) -> int {
        int count = 0;
        for (const auto& conn : graph.getConnections()) {
            if (conn.source.nodeID == srcID && conn.source.channelIndex == srcCh && conn.destination.nodeID == dstID &&
                conn.destination.channelIndex == dstCh)
                ++count;
        }
        return count;
    };

    // Verify the connections exist in the freshly loaded preset (pre-cycle baseline)
    int envVcaConnsBaseline = 0;
    for (int v = 0; v < 8; ++v)
        envVcaConnsBaseline += countConns(adsrID, v, vcaID, 8 + v);

    ASSERT_EQ(envVcaConnsBaseline, 8) << "Poly Pad baseline: expected 8 ADSR->VCA poly-bus connections";

    // Step 2 — Snapshot the loaded Poly Pad state
    juce::var J0 = gsynth::AIStateMapper::graphToJSON(graph);

    // Step 3 — Overwrite with a completely different preset (simulates the
    // modification that would be "undone")
    ASSERT_TRUE(gsynth::PresetManager::loadPreset(0, graph)); // Default preset — no poly modules
    EXPECT_EQ(countConns(adsrID, 0, vcaID, 8), 0)
        << "After loading preset 0, original Poly Pad connections should be gone";

    // Step 4 — Re-apply the Poly Pad snapshot (simulates undo/redo restore)
    ASSERT_TRUE(gsynth::AIStateMapper::applyJSONToGraph(J0, graph, /*clearExisting=*/true, /*trusted=*/true))
        << "applyJSONToGraph should succeed for a freshly-captured Poly Pad snapshot";

    // Re-locate nodes by type after round-trip (node IDs may differ after clear+rebuild)
    adsrID = vcaID = oscID = {};
    for (auto* node : graph.getNodes()) {
        auto* proc = node->getProcessor();
        if (auto* mb = dynamic_cast<ADSRModule*>(proc)) {
            if (mb->getName().contains("Amp Env") || mb->getName().contains("ADSR"))
                adsrID = node->nodeID;
        } else if (dynamic_cast<OscillatorModule*>(proc)) {
            oscID = node->nodeID;
        } else if (dynamic_cast<VCAModule*>(proc)) {
            vcaID = node->nodeID;
        }
    }
    ASSERT_NE(adsrID.uid, 0u) << "Post-roundtrip: Amp Env node should exist";
    ASSERT_NE(oscID.uid, 0u) << "Post-roundtrip: Oscillator node should exist";
    ASSERT_NE(vcaID.uid, 0u) << "Post-roundtrip: VCA node should exist";

    // Step 5 — Verify all 8 ADSR->VCA poly-bus connections survived
    int envVcaConnsAfter = 0;
    for (int v = 0; v < 8; ++v)
        envVcaConnsAfter += countConns(adsrID, v, vcaID, 8 + v);
    EXPECT_EQ(envVcaConnsAfter, 8)
        << "Post-undo/redo: expected 8 ADSR->VCA per-voice connections (ch0->ch8, ch1->ch9 ... ch7->ch15)";

    // Step 6 — Assert NO AttenuverterModule nodes were introduced for these paths.
    // Direct poly-bus connections must remain direct — serialization must not
    // silently convert them to attenuverter-chain routings.
    int attenuverterCount = 0;
    for (auto* node : graph.getNodes()) {
        if (dynamic_cast<AttenuverterModule*>(node->getProcessor()))
            ++attenuverterCount;
    }
    EXPECT_EQ(attenuverterCount, 0) << "Post-undo/redo: Poly Pad should have ZERO AttenuverterModule nodes; "
                                       "found "
                                    << attenuverterCount
                                    << " — direct poly connections were incorrectly "
                                       "converted to attenuverter chains during serialization round-trip";
}
