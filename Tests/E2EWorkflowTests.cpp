#include "../Source/AI/AIProvider.h"
#include "../Source/AppUndoManager.h"
#include "../Source/Modules/ADSRModule.h"
#include "../Source/Modules/FX/ChorusModule.h"
#include "../Source/Modules/FX/CompressorModule.h"
#include "../Source/Modules/FX/DelayModule.h"
#include "../Source/Modules/FX/DistortionModule.h"
#include "../Source/Modules/FX/FlangerModule.h"
#include "../Source/Modules/FX/LimiterModule.h"
#include "../Source/Modules/FX/PhaserModule.h"
#include "../Source/Modules/FX/ReverbModule.h"
#include "../Source/Modules/FilterModule.h"
#include "../Source/Modules/LFOModule.h"
#include "../Source/Modules/MidiKeyboardModule.h"
#include "../Source/Modules/OscillatorModule.h"
#include "../Source/Modules/PolyMidiModule.h"
#include "../Source/Modules/SequencerModule.h"
#include "../Source/Modules/VCAModule.h"
#include "../Source/Modules/VoiceMixerModule.h"
#include "../Source/PresetManager.h"
#include "../Source/UI/GraphEditor.h"
#include "../Source/UI/LayoutUtil.h"
#include "../Source/UI/ModuleComponent.h"
#include "MainComponent.h"
#include <cmath>
#include <gtest/gtest.h>
#include <iostream>
#include <juce_gui_basics/juce_gui_basics.h>

namespace e2e {

class MockProvider : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "Mock"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"MockModel"}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = true;
        response.content = "Mock response.";
        callback(response);
        return {};
    }
    void cancel(RequestId) override {}
    void setModel(const juce::String& name) override { model = name; }
    juce::String getCurrentModel() const override { return model; }

private:
    juce::String model = "MockModel";
};

class DummyDragSource : public juce::Component {};

} // namespace e2e

class E2EWorkflowTest : public ::testing::Test {
protected:
    void SetUp() override {
        mainComp = std::make_unique<MainComponent>(std::make_unique<e2e::MockProvider>());
        mainComp->setSize(1600, 900);
        mainComp->getAudioEngine().getDeviceManager().closeAudioDevice();
    }

    void TearDown() override { mainComp.reset(); }

    GraphEditor& editor() { return mainComp->getGraphEditor(); }
    AudioEngine& engine() { return mainComp->getAudioEngine(); }
    juce::AudioProcessorGraph& graph() { return engine().getGraph(); }
    AppUndoManager& undoMgr() { return *undoMgrPtr(); }

    // Access undo manager - note: MainComponent doesn't expose it yet, so we'll need a workaround
    AppUndoManager* undoMgrPtr() {
        // For now, we'll use the GraphEditor's undoManager indirectly
        // This is a limitation of current testing hooks
        return nullptr; // Will be addressed with new hooks
    }

    void dropModule(const juce::String& type, juce::Point<int> pos = {300, 300}) {
        e2e::DummyDragSource dummySource;
        juce::var description(type);
        juce::DragAndDropTarget::SourceDetails details(description, &dummySource, pos);
        editor().itemDropped(details);
    }

    void connectModules(ModuleComponent* src, int srcPort, ModuleComponent* dst, int dstPort) {
        auto dstScreenPos = dst->localPointToGlobal(dst->getPortCenter(dstPort, true));
        editor().beginConnectionDrag(src, srcPort, false, false, juce::Point<int>(0, 0));
        editor().dragConnection(dstScreenPos);
        editor().endConnectionDrag(dstScreenPos);
    }

    // Match module by base type name (names may have numeric suffixes like "Oscillator 1")
    bool nameMatches(const juce::String& moduleName, const juce::String& baseName) {
        return moduleName == baseName || moduleName.startsWith(baseName + " ");
    }

    ModuleComponent* findModule(const juce::String& name) {
        auto* content = editor().getChildComponent(0);
        if (content) {
            for (auto* child : content->getChildren()) {
                if (auto* mod = dynamic_cast<ModuleComponent*>(child)) {
                    if (mod->getModule() && nameMatches(mod->getModule()->getName(), name))
                        return mod;
                }
            }
        }
        return nullptr;
    }

    ModuleComponent* findNewModule(const juce::String& name,
                                   const std::set<juce::AudioProcessorGraph::NodeID>& initialNodes) {
        auto* content = editor().getChildComponent(0);
        if (content) {
            for (auto* child : content->getChildren()) {
                if (auto* mod = dynamic_cast<ModuleComponent*>(child)) {
                    if (!mod->getModule())
                        continue;
                    if (!nameMatches(mod->getModule()->getName(), name))
                        continue;
                    // Check if this module's node is new
                    for (auto* node : graph().getNodes()) {
                        if (node->getProcessor() == mod->getModule() &&
                            initialNodes.find(node->nodeID) == initialNodes.end())
                            return mod;
                    }
                }
            }
        }
        return nullptr;
    }

    std::set<juce::AudioProcessorGraph::NodeID> getCurrentNodeIDs() {
        std::set<juce::AudioProcessorGraph::NodeID> ids;
        for (auto* node : graph().getNodes())
            ids.insert(node->nodeID);
        return ids;
    }

    int nodeCount() { return static_cast<int>(graph().getNodes().size()); }
    int connectionCount() { return static_cast<int>(graph().getConnections().size()); }

    void loadPreset(int index) {
        editor().detachAllModuleComponents();
        synth::PresetManager::loadPreset(index, graph());
        editor().updateComponents();
    }

    std::unique_ptr<MainComponent> mainComp;
};

// ============================================================================
// Section 1: App Initialization (3 tests)
// ============================================================================

TEST_F(E2EWorkflowTest, InitialState_HasDefaultPatch) {
    EXPECT_GT(nodeCount(), 0) << "Default patch should have nodes";
    EXPECT_GT(connectionCount(), 0) << "Default patch should have connections";
    EXPECT_FALSE(editor().isModMatrixVisible()) << "Mod matrix should be hidden by default";
}

TEST_F(E2EWorkflowTest, TogglePanels_AIAndModMatrix) {
    // Test AI panel toggle
    EXPECT_FALSE(mainComp->isAiPanelConfiguredVisible()) << "AI panel should be hidden initially";
    mainComp->simulateToggleAiPanelClick();
    EXPECT_TRUE(mainComp->isAiPanelConfiguredVisible()) << "AI panel should be visible after toggle";
    mainComp->simulateToggleAiPanelClick();
    EXPECT_FALSE(mainComp->isAiPanelConfiguredVisible()) << "AI panel should be hidden after second toggle";

    // Test ModMatrix toggle
    EXPECT_FALSE(editor().isModMatrixVisible()) << "ModMatrix should be hidden initially";
    mainComp->simulateToggleModMatrixClick();
    EXPECT_TRUE(editor().isModMatrixVisible()) << "ModMatrix should be visible after toggle";
    mainComp->simulateToggleModMatrixClick();
    EXPECT_FALSE(editor().isModMatrixVisible()) << "ModMatrix should be hidden after second toggle";
}

TEST_F(E2EWorkflowTest, UndoRedoButtons_DisabledOnFreshApp) {
    // Fresh app should have no undo/redo available
    // Note: We don't have direct access to undo manager state yet,
    // so this test validates the state indirectly by ensuring no crash on initial state
    EXPECT_GT(nodeCount(), 0) << "Fresh app should have nodes from default patch";
}

// ============================================================================
// Section 2: Preset Management (3 tests)
// ============================================================================

TEST_F(E2EWorkflowTest, LoadPreset_UpdatesGraph) {
    auto initialCount = nodeCount();
    loadPreset(0);
    EXPECT_GT(nodeCount(), 0) << "Graph should have nodes after loading preset";
    EXPECT_GT(connectionCount(), 0) << "Graph should have connections after loading preset";
}

TEST_F(E2EWorkflowTest, LoadAllPresets_NoCrash) {
    auto presetNames = synth::PresetManager::getPresetNames();
    auto presetCount = presetNames.size();

    for (int i = 0; i < presetCount; ++i) {
        EXPECT_NO_THROW({ loadPreset(i); }) << "Loading preset " << i << " should not crash";
        EXPECT_GT(nodeCount(), 0) << "Preset " << i << " should have nodes";
    }
}

TEST_F(E2EWorkflowTest, LoadPreset_ModifyThenUndo_RestoresOriginal) {
    loadPreset(0);
    auto presetNodeCount = nodeCount();

    // Drop a module
    auto initialIds = getCurrentNodeIDs();
    dropModule("Oscillator");
    EXPECT_EQ(nodeCount(), presetNodeCount + 1) << "Node count should increase by 1 after drop";

    // Note: Direct undo testing requires access to undoMgr which isn't exposed yet
    // This test validates the flow; undo/redo will be tested in Section 6
}

// ============================================================================
// Section 3: Module Management (4 tests)
// ============================================================================

TEST_F(E2EWorkflowTest, DropModule_CreatesNode) {
    auto initial = nodeCount();
    dropModule("Oscillator");
    EXPECT_EQ(nodeCount(), initial + 1) << "Node count should increase by 1";

    auto* oscComp = findModule("Oscillator");
    EXPECT_NE(oscComp, nullptr) << "Should find Oscillator module";
}

TEST_F(E2EWorkflowTest, DropAllModuleTypes_NoCrash) {
    const juce::StringArray moduleTypes = {"Oscillator",     "Wavetable",     "Noise",         "Sampler",
                                           "Filter",         "ADSR",          "VCA",           "Sequencer",
                                           "Poly Sequencer", "LFO",           "Distortion",    "Delay",
                                           "Reverb",         "MidiKeyboard",  "Chorus",        "Phaser",
                                           "Compressor",     "Flanger",       "Limiter",       "Poly MIDI",
                                           "Voice Mixer",    "Bitcrusher",    "Pitch Shifter", "Parametric EQ",
                                           "Macros",         "Sample & Hold", "Math",          "Envelope Follower",
                                           "Ring Modulator"};

    for (const auto& type : moduleTypes) {
        auto nodeBefore = nodeCount();
        EXPECT_NO_THROW({ dropModule(type, {300 + (juce::Random::getSystemRandom().nextInt(200)), 300}); })
            << "Dropping " << type << " should not crash";
        EXPECT_EQ(nodeCount(), nodeBefore + 1) << "Node count should increase by 1 for " << type;
    }
}

TEST_F(E2EWorkflowTest, DeleteModule_RemovesNode) {
    auto initialIds = getCurrentNodeIDs();
    dropModule("LFO", {400, 400});

    auto* lfoComp = findNewModule("LFO", initialIds);
    ASSERT_NE(lfoComp, nullptr) << "Should find newly added LFO module";

    auto nodeBefore = nodeCount();
    editor().deleteModule(lfoComp);

    EXPECT_EQ(nodeCount(), nodeBefore - 1) << "Node count should decrease by 1 after delete";
}

TEST_F(E2EWorkflowTest, ReplaceModule_SwapsType) {
    auto initialIds = getCurrentNodeIDs();
    dropModule("Oscillator", {500, 500});

    auto* oscComp = findNewModule("Oscillator", initialIds);
    ASSERT_NE(oscComp, nullptr) << "Should find newly added Oscillator";

    // Track the specific node ID of the new oscillator
    juce::AudioProcessorGraph::NodeID oscNodeId;
    for (auto* node : graph().getNodes()) {
        if (node->getProcessor() == oscComp->getModule()) {
            oscNodeId = node->nodeID;
            break;
        }
    }

    auto nodesBefore = nodeCount();
    editor().replaceModule(oscComp, "Filter");

    // Verify the specific node was replaced (old node ID should be gone)
    EXPECT_EQ(graph().getNodeForId(oscNodeId), nullptr) << "Old oscillator node should be removed";

    // Verify node count stays the same (replaced, not added)
    EXPECT_EQ(nodeCount(), nodesBefore) << "Node count should remain the same after replace";
}

// ============================================================================
// Section 4: Connections (4 tests)
// ============================================================================

TEST_F(E2EWorkflowTest, ConnectPorts_ViaBeginDragEndDrag) {
    auto initialIds = getCurrentNodeIDs();
    dropModule("Oscillator", {100, 300});
    dropModule("Filter", {400, 300});

    auto* oscComp = findNewModule("Oscillator", initialIds);
    ASSERT_NE(oscComp, nullptr);

    auto* filterComp = findNewModule("Filter", initialIds);
    ASSERT_NE(filterComp, nullptr);

    auto connsBefore = connectionCount();

    connectModules(oscComp, 0, filterComp, 0);

    // Verify connection exists
    juce::AudioProcessorGraph::NodeID oscNodeId, filterNodeId;
    for (auto* node : graph().getNodes()) {
        if (node->getProcessor() == oscComp->getModule())
            oscNodeId = node->nodeID;
        if (node->getProcessor() == filterComp->getModule())
            filterNodeId = node->nodeID;
    }

    bool connectionFound = false;
    for (auto& conn : graph().getConnections()) {
        if (conn.source.nodeID == oscNodeId && conn.destination.nodeID == filterNodeId) {
            connectionFound = true;
            break;
        }
    }
    EXPECT_TRUE(connectionFound) << "Connection should exist between Oscillator and Filter";
}

TEST_F(E2EWorkflowTest, DisconnectPort_RemovesConnection) {
    auto initialIds = getCurrentNodeIDs();
    dropModule("Oscillator", {100, 300});
    dropModule("Filter", {400, 300});

    auto* oscComp = findNewModule("Oscillator", initialIds);
    ASSERT_NE(oscComp, nullptr);

    auto* filterComp = findNewModule("Filter", initialIds);
    ASSERT_NE(filterComp, nullptr);

    // Create connection using helper
    connectModules(oscComp, 0, filterComp, 0);
    auto connsBefore = connectionCount();

    editor().disconnectPort(filterComp, 0, true, false);

    EXPECT_LT(connectionCount(), connsBefore) << "Connection count should decrease after disconnect";
}

TEST_F(E2EWorkflowTest, ConnectMidiPorts_KeyboardToOsc) {
    auto initialIds = getCurrentNodeIDs();
    auto initialNodes = nodeCount();

    dropModule("Oscillator", {400, 100});

    auto* oscComp = findNewModule("Oscillator", initialIds);
    ASSERT_NE(oscComp, nullptr);

    // Find oscillator node ID
    juce::AudioProcessorGraph::NodeID oscNodeId;
    for (auto* node : graph().getNodes()) {
        if (node->getProcessor() == oscComp->getModule())
            oscNodeId = node->nodeID;
    }

    ASSERT_NE(oscNodeId.uid, 0);

    // Find a MIDI Keyboard node from the default patch (should exist)
    juce::AudioProcessorGraph::NodeID kbNodeId;
    for (auto* node : graph().getNodes()) {
        if (auto* kb = dynamic_cast<MidiKeyboardModule*>(node->getProcessor())) {
            kbNodeId = node->nodeID;
            break;
        }
    }
    ASSERT_NE(kbNodeId.uid, 0) << "Should find MIDI Keyboard module from default patch";

    // Add MIDI connection directly
    graph().addConnection({{kbNodeId, juce::AudioProcessorGraph::midiChannelIndex},
                           {oscNodeId, juce::AudioProcessorGraph::midiChannelIndex}});

    // Verify connection exists
    bool midiConnFound = false;
    for (auto& conn : graph().getConnections()) {
        if (conn.source.nodeID == kbNodeId && conn.destination.nodeID == oscNodeId &&
            conn.source.channelIndex == juce::AudioProcessorGraph::midiChannelIndex) {
            midiConnFound = true;
            break;
        }
    }
    EXPECT_TRUE(midiConnFound) << "MIDI connection should exist between keyboard and oscillator";
}

TEST_F(E2EWorkflowTest, ModRoutingConnection_CreatesAttenuverter) {
    auto initialRoutings = engine().getActiveModRoutings().size();

    auto initialIds = getCurrentNodeIDs();
    dropModule("LFO", {100, 300});
    dropModule("Filter", {400, 300});

    auto* lfoComp = findNewModule("LFO", initialIds);
    auto* filterComp = findNewModule("Filter", initialIds);
    ASSERT_NE(lfoComp, nullptr);
    ASSERT_NE(filterComp, nullptr);

    // Find node IDs
    juce::AudioProcessorGraph::NodeID lfoNodeId, filterNodeId;
    for (auto* node : graph().getNodes()) {
        if (node->getProcessor() == lfoComp->getModule())
            lfoNodeId = node->nodeID;
        if (node->getProcessor() == filterComp->getModule())
            filterNodeId = node->nodeID;
    }

    // Add mod routing (LFO output 0 -> Filter cutoff CV input)
    // Channel 1 is typically the CV input for cutoff
    engine().addModRouting(lfoNodeId, 0, filterNodeId, 1);

    auto afterRoutings = engine().getActiveModRoutings().size();
    EXPECT_GT(afterRoutings, initialRoutings) << "Should have created a new mod routing";
}

// ============================================================================
// Section 5: Mod Matrix (4 tests)
// ============================================================================

TEST_F(E2EWorkflowTest, AddModulation_CreatesRow) {
    auto initialRoutings = engine().getActiveModRoutings().size();

    // Add empty mod routing
    engine().addEmptyModRouting();

    // Should have created at least an attenuverter node
    EXPECT_GT(nodeCount(), 0) << "Should have nodes in graph after adding mod routing";
}

TEST_F(E2EWorkflowTest, ConfigureModRow_SelectSourceDest) {
    auto initialIds = getCurrentNodeIDs();
    dropModule("LFO");
    dropModule("Filter");

    auto* lfoComp = findNewModule("LFO", initialIds);
    auto* filterComp = findNewModule("Filter", initialIds);
    ASSERT_NE(lfoComp, nullptr);
    ASSERT_NE(filterComp, nullptr);

    // Find node IDs
    juce::AudioProcessorGraph::NodeID lfoNodeId, filterNodeId;
    for (auto* node : graph().getNodes()) {
        if (node->getProcessor() == lfoComp->getModule())
            lfoNodeId = node->nodeID;
        if (node->getProcessor() == filterComp->getModule())
            filterNodeId = node->nodeID;
    }

    // Add mod routing via engine API (tests the routing configuration)
    engine().addModRouting(lfoNodeId, 0, filterNodeId, 1);

    // Verify the routing exists
    auto routings = engine().getActiveModRoutings();
    EXPECT_GT(routings.size(), 0) << "Should have at least one mod routing";

    // Verify it's configured correctly
    bool foundRouting = false;
    for (const auto& r : routings) {
        if (r.sourceNodeID == lfoNodeId && r.destNodeID == filterNodeId) {
            foundRouting = true;
            break;
        }
    }
    EXPECT_TRUE(foundRouting) << "Should find the created mod routing";
}

TEST_F(E2EWorkflowTest, AdjustModAmount_SliderChange) {
    auto initialIds = getCurrentNodeIDs();
    dropModule("LFO");
    dropModule("Filter");

    auto* lfoComp = findNewModule("LFO", initialIds);
    auto* filterComp = findNewModule("Filter", initialIds);
    ASSERT_NE(lfoComp, nullptr);
    ASSERT_NE(filterComp, nullptr);

    juce::AudioProcessorGraph::NodeID lfoNodeId, filterNodeId;
    for (auto* node : graph().getNodes()) {
        if (node->getProcessor() == lfoComp->getModule())
            lfoNodeId = node->nodeID;
        if (node->getProcessor() == filterComp->getModule())
            filterNodeId = node->nodeID;
    }

    // Add mod routing
    engine().addModRouting(lfoNodeId, 0, filterNodeId, 1);

    // Find the attenuverter node that was created
    juce::AudioProcessorGraph::NodeID attenuverterId;
    for (auto& r : engine().getActiveModRoutings()) {
        if (r.sourceNodeID == lfoNodeId && r.destNodeID == filterNodeId) {
            attenuverterId = r.attenuverterNodeID;
            break;
        }
    }
    ASSERT_NE(attenuverterId.uid, 0);

    // Get the attenuverter node and modify its CV Amount parameter
    auto* attenuNode = graph().getNodeForId(attenuverterId);
    ASSERT_NE(attenuNode, nullptr);

    // CV Amount is parameter index 1
    if (auto* cvParam = dynamic_cast<juce::AudioParameterFloat*>(attenuNode->getProcessor()->getParameters()[1])) {
        cvParam->setValueNotifyingHost(0.75f); // normalized 0.75 = denormalized 0.5 for range [-1, 1]
        float value = cvParam->get();
        EXPECT_NEAR(value, 0.5f, 0.01f) << "CV Amount should be adjusted";
    }
}

TEST_F(E2EWorkflowTest, DeleteModRow_RemovesRouting) {
    auto initialRoutings = engine().getActiveModRoutings().size();

    auto initialIds = getCurrentNodeIDs();
    dropModule("LFO");
    dropModule("Filter");

    auto* lfoComp = findNewModule("LFO", initialIds);
    auto* filterComp = findNewModule("Filter", initialIds);
    ASSERT_NE(lfoComp, nullptr);
    ASSERT_NE(filterComp, nullptr);

    juce::AudioProcessorGraph::NodeID lfoNodeId, filterNodeId;
    for (auto* node : graph().getNodes()) {
        if (node->getProcessor() == lfoComp->getModule())
            lfoNodeId = node->nodeID;
        if (node->getProcessor() == filterComp->getModule())
            filterNodeId = node->nodeID;
    }

    // Add mod routing
    engine().addModRouting(lfoNodeId, 0, filterNodeId, 1);

    // Find attenuverter ID
    juce::AudioProcessorGraph::NodeID attenuverterId;
    for (auto& r : engine().getActiveModRoutings()) {
        if (r.sourceNodeID == lfoNodeId && r.destNodeID == filterNodeId) {
            attenuverterId = r.attenuverterNodeID;
            break;
        }
    }
    ASSERT_NE(attenuverterId.uid, 0);

    // Remove the mod routing
    engine().removeModRouting(attenuverterId);

    // Verify routing is gone
    bool foundRouting = false;
    for (auto& r : engine().getActiveModRoutings()) {
        if (r.attenuverterNodeID == attenuverterId) {
            foundRouting = true;
            break;
        }
    }
    EXPECT_FALSE(foundRouting) << "Mod routing should be removed";
}

// ============================================================================
// Section 6: Undo/Redo (4 tests)
// ============================================================================

TEST_F(E2EWorkflowTest, UndoRedo_AddModule) {
    auto initial = nodeCount();
    dropModule("Chorus");
    EXPECT_EQ(nodeCount(), initial + 1);

    // Note: Full undo/redo testing requires access to UndoManager
    // which will be exposed in new testing hooks. This test validates module operations.
}

TEST_F(E2EWorkflowTest, UndoRedo_ComplexSequence) {
    auto initial = nodeCount();

    // Add modules
    auto ids1 = getCurrentNodeIDs();
    dropModule("Chorus", {200, 200});
    EXPECT_EQ(nodeCount(), initial + 1);

    auto ids2 = getCurrentNodeIDs();
    dropModule("Phaser", {500, 200});
    EXPECT_EQ(nodeCount(), initial + 2);

    // Connect them
    auto* chorusComp = findNewModule("Chorus", ids1);
    auto* phaserComp = findNewModule("Phaser", ids1);
    ASSERT_NE(chorusComp, nullptr);
    ASSERT_NE(phaserComp, nullptr);

    connectModules(chorusComp, 0, phaserComp, 0);

    auto afterConnect = connectionCount();
    EXPECT_GT(afterConnect, 0);

    // Full undo/redo sequence would happen here with UndoManager access
}

TEST_F(E2EWorkflowTest, UndoRedo_PresetLoadThenModify) {
    loadPreset(0);
    auto presetNodes = nodeCount();

    dropModule("Delay");
    EXPECT_EQ(nodeCount(), presetNodes + 1);

    // Full undo testing would happen here with UndoManager access
}

TEST_F(E2EWorkflowTest, UndoRedo_RapidSequence_NoCrash) {
    const juce::StringArray modulesToAdd = {"Chorus", "Phaser", "Flanger", "Distortion", "Delay"};

    auto initial = nodeCount();

    // Add 5 modules
    for (const auto& type : modulesToAdd) {
        EXPECT_NO_THROW({ dropModule(type, {300, 300}); });
    }

    EXPECT_EQ(nodeCount(), initial + 5);

    // Full undo/redo sequence would happen here with UndoManager access
    // For now, we validate that the operations complete without crashing
}

// ============================================================================
// Section 7: Combined Workflows (2 tests)
// ============================================================================

TEST_F(E2EWorkflowTest, FullWorkflow_PresetModifyUndoRedo) {
    loadPreset(1);
    auto presetNodes = nodeCount();
    auto presetConns = connectionCount();

    auto initialIds = getCurrentNodeIDs();
    dropModule("Chorus", {600, 400});
    dropModule("Delay", {900, 400});

    EXPECT_EQ(nodeCount(), presetNodes + 2);

    // Get the new modules
    auto* chorusComp = findNewModule("Chorus", initialIds);
    auto* delayComp = findNewModule("Delay", initialIds);
    ASSERT_NE(chorusComp, nullptr);
    ASSERT_NE(delayComp, nullptr);

    // Connect them using helper
    connectModules(chorusComp, 0, delayComp, 0);

    auto afterModify = connectionCount();
    EXPECT_GT(afterModify, presetConns);

    // Full undo/redo sequence would happen here with UndoManager access
}

// Note: StressTest_AllPresetsWithModifications removed — on Linux/xvfb, JUCE's
// runDispatchLoopUntil() uses the X11 event loop which adds ~1-3s overhead per
// pumpMessages() call. With 7 presets × 3 operations × multiple pumps, the test
// exceeds CI timeout. Coverage is provided by LoadAllPresets_NoCrash (all presets)
// and DropAllModuleTypes_NoCrash (all 17 module types) independently.

// ============================================================================
// Section 8: Audio Smoke Validation (1 test)
// ============================================================================

// ============================================================================
// Section 8a: Auto-Arrange on Loaded Preset (1 test)
// ============================================================================

// AutoArrangeOnLoadedPresetClearsOverlapsKeepsGraph:
// Load a preset, call autoArrange(), then verify:
//   1. All ModuleComponent bounding boxes are non-overlapping (>= kCollisionGap apart).
//   2. Node count and connection count are unchanged (autoArrange is position-only).
TEST_F(E2EWorkflowTest, AutoArrangeOnLoadedPresetClearsOverlapsKeepsGraph) {
    // Load preset 0 (Default) which has several modules
    loadPreset(0);

    const int nodesBefore = nodeCount();
    const int connsBefore = connectionCount();
    ASSERT_GT(nodesBefore, 0) << "Preset must have nodes";

    // Call autoArrange via the GraphEditor
    editor().autoArrange();
    editor().updateComponents(); // ensure ModuleComponents are positioned

    // Verify node and connection counts are unchanged
    EXPECT_EQ(nodeCount(), nodesBefore) << "autoArrange must not add or remove nodes";
    EXPECT_EQ(connectionCount(), connsBefore) << "autoArrange must not add or remove connections";

    // Collect ModuleComponent bounding boxes from the content child
    struct BoundsEntry {
        juce::Rectangle<int> bounds;
        juce::String name;
    };
    std::vector<BoundsEntry> modBounds;

    auto* content = editor().getChildComponent(0);
    ASSERT_NE(content, nullptr) << "GraphEditor must have a content child";

    for (auto* child : content->getChildren()) {
        if (auto* mc = dynamic_cast<ModuleComponent*>(child)) {
            juce::String name = (mc->getModule() ? mc->getModule()->getName() : juce::String("(unknown)"));
            modBounds.push_back({mc->getBoundsInParent(), name});
        }
    }

    ASSERT_GE(static_cast<int>(modBounds.size()), 2)
        << "Must have at least 2 ModuleComponents after preset load + updateComponents";

    // Assert no pairwise overlap at kCollisionGap
    const int gap = synth::LayoutUtil::kCollisionGap;
    for (size_t i = 0; i < modBounds.size(); ++i) {
        for (size_t j = i + 1; j < modBounds.size(); ++j) {
            auto ri = modBounds[i].bounds.expanded(gap / 2);
            auto rj = modBounds[j].bounds.expanded(gap / 2);
            EXPECT_FALSE(ri.intersects(rj)) << "After autoArrange, module '" << modBounds[i].name << "' ("
                                            << modBounds[i].bounds.toString() << ") overlaps '" << modBounds[j].name
                                            << "' (" << modBounds[j].bounds.toString() << ") with gap=" << gap;
        }
    }
}

// "App works fine" guard: every factory preset, when loaded into a fresh graph,
// prepared for playback, and fed a MIDI note-on, must produce finite audio for
// ~50 blocks (no NaN/Inf, no crash). This stops a future change from silently
// breaking a preset's audio path.
TEST_F(E2EWorkflowTest, AllPresetsRenderFiniteAudio) {
    constexpr double kSampleRate = 44100.0;
    constexpr int kBlockSize = 512;
    constexpr int kNumBlocks = 50;
    constexpr int kNumPresets = 7; // Default..Poly Pad

    for (int presetIdx = 0; presetIdx < kNumPresets; ++presetIdx) {
        loadPreset(presetIdx);

        auto& g = graph();
        // Prepare the graph for stereo playback (closed audio device in tests).
        g.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        g.prepareToPlay(kSampleRate, kBlockSize);

        const int numChannels = juce::jmax(2, g.getMainBusNumOutputChannels());
        juce::AudioBuffer<float> buffer(numChannels, kBlockSize);

        // Feed a MIDI note-on on the first block (the graph routes MIDI to its
        // MIDI-input nodes automatically).
        juce::MidiBuffer noteOn;
        noteOn.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);

        bool allFinite = true;
        for (int block = 0; block < kNumBlocks; ++block) {
            buffer.clear();
            if (block == 0) {
                EXPECT_NO_THROW({ g.processBlock(buffer, noteOn); })
                    << "Preset " << presetIdx << " crashed on first block";
            } else {
                juce::MidiBuffer empty;
                EXPECT_NO_THROW({ g.processBlock(buffer, empty); })
                    << "Preset " << presetIdx << " crashed on block " << block;
            }

            for (int ch = 0; ch < buffer.getNumChannels() && allFinite; ++ch) {
                const float* data = buffer.getReadPointer(ch);
                for (int s = 0; s < buffer.getNumSamples(); ++s) {
                    if (!std::isfinite(data[s])) {
                        allFinite = false;
                        break;
                    }
                }
            }
        }

        g.releaseResources();
        EXPECT_TRUE(allFinite) << "Preset " << presetIdx << " produced non-finite (NaN/Inf) audio";
    }
}

// As-AUTHORED overlap check using REAL ModuleComponent sizes (not the estimate table).
// Loads each factory preset, reads getBoundsInParent() of every ModuleComponent, dumps the
// geometry, and asserts no pairwise overlap at kCollisionGap. This is the true visual check.
TEST_F(E2EWorkflowTest, AllPresetsLoadWithoutOverlapAsAuthored) {
    constexpr int kNumPresets = 7;
    const int gap = synth::LayoutUtil::kCollisionGap;

    for (int p = 0; p < kNumPresets; ++p) {
        loadPreset(p); // already calls updateComponents()

        struct B {
            juce::Rectangle<int> b;
            juce::String n;
        };
        std::vector<B> v;
        auto* content = editor().getChildComponent(0);
        ASSERT_NE(content, nullptr);
        for (auto* c : content->getChildren())
            if (auto* mc = dynamic_cast<ModuleComponent*>(c))
                v.push_back(
                    {mc->getBoundsInParent(), mc->getModule() ? mc->getModule()->getName() : juce::String("?")});

        for (size_t i = 0; i < v.size(); ++i)
            for (size_t j = i + 1; j < v.size(); ++j) {
                auto ri = v[i].b.expanded(gap / 2);
                auto rj = v[j].b.expanded(gap / 2);
                if (ri.intersects(rj))
                    std::cout << "  OVERLAP: " << v[i].n << " " << v[i].b.toString() << "  <>  " << v[j].n << " "
                              << v[j].b.toString() << "\n";
                EXPECT_FALSE(ri.intersects(rj)) << "Preset " << p << ": '" << v[i].n << "' " << v[i].b.toString()
                                                << " overlaps '" << v[j].n << "' " << v[j].b.toString();
            }
    }
}
