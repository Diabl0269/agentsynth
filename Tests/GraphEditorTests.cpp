#include "../Source/GravisynthUndoManager.h"
#include "../Source/Modules/ADSRModule.h"
#include "../Source/Modules/FilterModule.h"
#include "../Source/Modules/LFOModule.h"
#include "../Source/Modules/OscillatorModule.h"
#include "../Source/Modules/SequencerModule.h"
#include "../Source/Modules/VCAModule.h"
#include "../Source/UI/GraphEditor.h"
#include "../Source/UI/ModuleComponent.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

// Define a dummy component to act as the drag source
class DummyDragSource : public juce::Component {};

class GraphEditorTest : public ::testing::Test {};

TEST_F(GraphEditorTest, InitializationAndResizing) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    EXPECT_FALSE(editor.isModMatrixVisible());
    EXPECT_NO_THROW(editor.resized());
}
TEST_F(GraphEditorTest, ToggleModMatrixVisibility) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    EXPECT_FALSE(editor.isModMatrixVisible());
    editor.toggleModMatrixVisibility();
    EXPECT_TRUE(editor.isModMatrixVisible());
    editor.toggleModMatrixVisibility();
    EXPECT_FALSE(editor.isModMatrixVisible());
}

TEST_F(GraphEditorTest, DropModuleCreatesNode) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    DummyDragSource dummySource;
    juce::var description("Oscillator");
    juce::DragAndDropTarget::SourceDetails details(description, &dummySource, juce::Point<int>(100, 100));

    EXPECT_TRUE(editor.isInterestedInDragSource(details));

    auto initialNodeCount = engine.getGraph().getNodes().size();
    editor.itemDropped(details);

    EXPECT_EQ(engine.getGraph().getNodes().size(), initialNodeCount + 1);

    bool foundOsc = false;
    for (auto* node : engine.getGraph().getNodes()) {
        if (node->getProcessor()->getName() == "Oscillator") {
            foundOsc = true;
            EXPECT_EQ(static_cast<int>(node->properties.getWithDefault("x", 0)), 100);
            EXPECT_EQ(static_cast<int>(node->properties.getWithDefault("y", 0)), 100);
            break;
        }
    }
    EXPECT_TRUE(foundOsc);
}

TEST_F(GraphEditorTest, DragConnectionCreatesLink) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto oscNode = engine.getGraph().addNode(std::make_unique<OscillatorModule>());
    auto filterNode = engine.getGraph().addNode(std::make_unique<FilterModule>());

    editor.updateComponents();

    ModuleComponent* oscComp = nullptr;
    ModuleComponent* filterComp = nullptr;

    // Find modules via content component (first child of GraphEditor)
    auto* content = editor.getChildComponent(0);
    if (content) {
        for (auto* contentChild : content->getChildren()) {
            if (auto* mod = dynamic_cast<ModuleComponent*>(contentChild)) {
                if (mod->getModule() == oscNode->getProcessor())
                    oscComp = mod;
                if (mod->getModule() == filterNode->getProcessor())
                    filterComp = mod;
            }
        }
    }

    ASSERT_NE(oscComp, nullptr);
    ASSERT_NE(filterComp, nullptr);

    oscComp->setBounds(0, 0, 100, 100);
    filterComp->setBounds(200, 0, 100, 100);

    editor.beginConnectionDrag(oscComp, 0, false, false, juce::Point<int>(0, 0));
    editor.dragConnection(juce::Point<int>(50, 0));

    auto filterTargetPoint = filterComp->getBounds().getPosition() + filterComp->getPortCenter(0, true);
    editor.endConnectionDrag(filterTargetPoint);

    bool connectionFound = false;
    for (auto& conn : engine.getGraph().getConnections()) {
        if (conn.source.nodeID == oscNode->nodeID && conn.destination.nodeID == filterNode->nodeID) {
            connectionFound = true;
            break;
        }
    }

    EXPECT_TRUE(connectionFound);
}

TEST_F(GraphEditorTest, ReplaceModulePreservesPosition) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    oscNode->properties.set("x", 200);
    oscNode->properties.set("y", 300);
    editor.updateComponents();

    // Find the ModuleComponent for the oscillator
    ModuleComponent* oscComp = nullptr;
    auto* content = editor.getChildComponent(0);
    if (content) {
        for (auto* child : content->getChildren()) {
            if (auto* mod = dynamic_cast<ModuleComponent*>(child)) {
                if (mod->getModule() == oscNode->getProcessor())
                    oscComp = mod;
            }
        }
    }
    ASSERT_NE(oscComp, nullptr);

    editor.replaceModule(oscComp, "Filter");

    // Verify: oscillator is gone, filter exists at same position
    bool foundOsc = false, foundFilter = false;
    int filterX = 0, filterY = 0;
    for (auto* node : graph.getNodes()) {
        if (dynamic_cast<OscillatorModule*>(node->getProcessor()))
            foundOsc = true;
        if (dynamic_cast<FilterModule*>(node->getProcessor())) {
            foundFilter = true;
            filterX = node->properties.getWithDefault("x", 0);
            filterY = node->properties.getWithDefault("y", 0);
        }
    }
    EXPECT_FALSE(foundOsc);
    EXPECT_TRUE(foundFilter);
    EXPECT_EQ(filterX, 200);
    EXPECT_EQ(filterY, 300);
}

TEST_F(GraphEditorTest, ReplaceModulePreservesAudioConnections) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());

    // Connect oscillator output 0 -> filter input 0
    graph.addConnection({{oscNode->nodeID, 0}, {filterNode->nodeID, 0}});
    editor.updateComponents();

    // Find the filter ModuleComponent
    ModuleComponent* filterComp = nullptr;
    auto* content = editor.getChildComponent(0);
    if (content) {
        for (auto* child : content->getChildren()) {
            if (auto* mod = dynamic_cast<ModuleComponent*>(child)) {
                if (mod->getModule() == filterNode->getProcessor())
                    filterComp = mod;
            }
        }
    }
    ASSERT_NE(filterComp, nullptr);

    // Replace filter with VCA (both have input on channel 0)
    editor.replaceModule(filterComp, "VCA");

    // Find the new VCA node
    juce::AudioProcessorGraph::NodeID vcaNodeId;
    for (auto* node : graph.getNodes()) {
        if (dynamic_cast<VCAModule*>(node->getProcessor()))
            vcaNodeId = node->nodeID;
    }
    EXPECT_NE(vcaNodeId.uid, 0u);

    // Verify connection Osc -> VCA on channel 0
    bool connectionFound = false;
    for (auto& conn : graph.getConnections()) {
        if (conn.source.nodeID == oscNode->nodeID && conn.source.channelIndex == 0 &&
            conn.destination.nodeID == vcaNodeId && conn.destination.channelIndex == 0) {
            connectionFound = true;
            break;
        }
    }
    EXPECT_TRUE(connectionFound);
}

TEST_F(GraphEditorTest, ReplaceModuleDropsIncompatibleConnections) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    auto lfoNode = graph.addNode(std::make_unique<LFOModule>());
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());

    // Connect LFO output 0 -> Oscillator input 13 (Oscillator has 14 inputs)
    graph.addConnection({{lfoNode->nodeID, 0}, {oscNode->nodeID, 13}});
    editor.updateComponents();

    // Find the oscillator ModuleComponent
    ModuleComponent* oscComp = nullptr;
    auto* content = editor.getChildComponent(0);
    if (content) {
        for (auto* child : content->getChildren()) {
            if (auto* mod = dynamic_cast<ModuleComponent*>(child)) {
                if (mod->getModule() == oscNode->getProcessor())
                    oscComp = mod;
            }
        }
    }
    ASSERT_NE(oscComp, nullptr);

    // Replace Oscillator (14 inputs) with LFO (1 input) — channel 13 is incompatible
    editor.replaceModule(oscComp, "LFO");

    // Find the new LFO node (replacement)
    juce::AudioProcessorGraph::NodeID newNodeId;
    for (auto* node : graph.getNodes()) {
        if (node->nodeID != lfoNode->nodeID && dynamic_cast<LFOModule*>(node->getProcessor()))
            newNodeId = node->nodeID;
    }
    EXPECT_NE(newNodeId.uid, 0u);

    // Verify NO connection from LFO to replacement (channel 13 doesn't exist on LFO)
    bool connectionFound = false;
    for (auto& conn : graph.getConnections()) {
        if (conn.source.nodeID == lfoNode->nodeID && conn.destination.nodeID == newNodeId) {
            connectionFound = true;
            break;
        }
    }
    EXPECT_FALSE(connectionFound);
}

TEST_F(GraphEditorTest, ReplaceModulePreservesMidiConnections) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    auto seqNode = graph.addNode(std::make_unique<SequencerModule>());
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());

    // Connect Sequencer MIDI out -> Oscillator MIDI in
    graph.addConnection({{seqNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                         {oscNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex}});
    editor.updateComponents();

    // Find the oscillator ModuleComponent
    ModuleComponent* oscComp = nullptr;
    auto* content = editor.getChildComponent(0);
    if (content) {
        for (auto* child : content->getChildren()) {
            if (auto* mod = dynamic_cast<ModuleComponent*>(child)) {
                if (mod->getModule() == oscNode->getProcessor())
                    oscComp = mod;
            }
        }
    }
    ASSERT_NE(oscComp, nullptr);

    // Replace Oscillator with ADSR (both accept MIDI)
    editor.replaceModule(oscComp, "ADSR");

    // Find the new ADSR node
    juce::AudioProcessorGraph::NodeID adsrNodeId;
    for (auto* node : graph.getNodes()) {
        if (dynamic_cast<ADSRModule*>(node->getProcessor()))
            adsrNodeId = node->nodeID;
    }
    EXPECT_NE(adsrNodeId.uid, 0u);

    // Verify MIDI connection Sequencer -> ADSR
    bool midiConnFound = false;
    for (auto& conn : graph.getConnections()) {
        if (conn.source.nodeID == seqNode->nodeID &&
            conn.source.channelIndex == juce::AudioProcessorGraph::midiChannelIndex &&
            conn.destination.nodeID == adsrNodeId &&
            conn.destination.channelIndex == juce::AudioProcessorGraph::midiChannelIndex) {
            midiConnFound = true;
            break;
        }
    }
    EXPECT_TRUE(midiConnFound);
}

TEST_F(GraphEditorTest, ReplaceModuleIsUndoable) {
    AudioEngine engine;
    GravisynthUndoManager undoMgr;
    GraphEditor editor(engine, &undoMgr);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    oscNode->properties.set("x", 100);
    oscNode->properties.set("y", 200);
    auto oscNodeId = oscNode->nodeID;
    editor.updateComponents();

    // Find the oscillator ModuleComponent
    ModuleComponent* oscComp = nullptr;
    auto* content = editor.getChildComponent(0);
    if (content) {
        for (auto* child : content->getChildren()) {
            if (auto* mod = dynamic_cast<ModuleComponent*>(child)) {
                if (mod->getModule() == oscNode->getProcessor())
                    oscComp = mod;
            }
        }
    }
    ASSERT_NE(oscComp, nullptr);

    // Replace oscillator with filter
    editor.replaceModule(oscComp, "Filter");

    // Verify filter exists, oscillator gone
    bool hasFilter = false, hasOsc = false;
    for (auto* node : graph.getNodes()) {
        if (dynamic_cast<FilterModule*>(node->getProcessor()))
            hasFilter = true;
        if (dynamic_cast<OscillatorModule*>(node->getProcessor()))
            hasOsc = true;
    }
    EXPECT_TRUE(hasFilter);
    EXPECT_FALSE(hasOsc);

    // Undo
    EXPECT_TRUE(undoMgr.undo());

    // Verify oscillator is back, filter gone
    hasFilter = false;
    hasOsc = false;
    for (auto* node : graph.getNodes()) {
        if (dynamic_cast<FilterModule*>(node->getProcessor()))
            hasFilter = true;
        if (dynamic_cast<OscillatorModule*>(node->getProcessor()))
            hasOsc = true;
    }
    EXPECT_FALSE(hasFilter);
    EXPECT_TRUE(hasOsc);
}
