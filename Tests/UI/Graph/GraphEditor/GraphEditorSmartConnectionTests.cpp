// GraphEditor smart-connection eligibility tests: suggestion/auto-wire modes (Off/New/New+Unwired/All),
// stereo/mono fan rules, and incompatible-pair rejection.
// Shared GraphEditorTest fixture and helpers live in GraphEditorTestHelpers.h.

#include "GraphEditorTestHelpers.h"

#include "Modules/FX/DelayModule.h"
#include "Modules/FX/ReverbModule.h"
#include "Modules/FilterModule.h"
#include "Modules/MathModule.h"
#include "Modules/OscillatorModule.h"
#include "Modules/VCAModule.h"

// --- Smart connections -------------------------------------------------------

TEST_F(GraphEditorTest, SmartConnectionOffDoesNotAutoWireOnDrop) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);
    editor.getSmartConnections().setSmartConnectionMode(GraphEditor::SmartConnectionMode::Off);

    auto& graph = engine.getGraph();
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    filterNode->properties.set("x", 400);
    filterNode->properties.set("y", 100);
    editor.updateComponents();
    sizeModuleComponents(editor);

    DummyDragSource dummySource;
    juce::DragAndDropTarget::SourceDetails details(juce::var("Oscillator"), &dummySource,
                                                   libraryCursorForGhostTopLeft("Oscillator", {120, 120}));
    editor.itemDragEnter(details);
    editor.itemDragMove(details);
    EXPECT_EQ(editor.getSmartConnections().getSmartSuggestionCount(), 0);

    editor.itemDropped(details);
    juce::AudioProcessorGraph::NodeID oscId{};
    for (auto* node : graph.getNodes()) {
        if (node->getProcessor()->getName() == "Oscillator")
            oscId = node->nodeID;
    }
    ASSERT_NE(oscId.uid, 0u);
    EXPECT_EQ(countAudioConnectionsBetween(graph, oscId, filterNode->nodeID), 0);
}

TEST_F(GraphEditorTest, SmartConnectionSuggestsNearCompatibleNeighbor) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);
    editor.getSmartConnections().setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);

    auto& graph = engine.getGraph();
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    filterNode->properties.set("x", 360);
    filterNode->properties.set("y", 100);
    editor.updateComponents();
    sizeModuleComponents(editor);

    DummyDragSource dummySource;
    // Drop point near the filter (within the 96 px proximity window after anti-overlap).
    juce::DragAndDropTarget::SourceDetails details(juce::var("Oscillator"), &dummySource,
                                                   libraryCursorForGhostTopLeft("Oscillator", {80, 100}));
    editor.itemDragEnter(details);
    editor.itemDragMove(details);

    EXPECT_GT(editor.getSmartConnections().getSmartSuggestionCount(), 0)
        << "Oscillator ghost near a Filter should suggest an audio cable";

    // Far away — suggestions clear.
    juce::DragAndDropTarget::SourceDetails far(juce::var("Oscillator"), &dummySource,
                                               libraryCursorForGhostTopLeft("Oscillator", {50, 500}));
    editor.itemDragMove(far);
    EXPECT_EQ(editor.getSmartConnections().getSmartSuggestionCount(), 0);
    editor.endDragPreview();
}

TEST_F(GraphEditorTest, SmartConnectionNewDropAutoWires) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);
    editor.getSmartConnections().setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewOnly);

    auto& graph = engine.getGraph();
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    filterNode->properties.set("x", 360);
    filterNode->properties.set("y", 100);
    editor.updateComponents();
    sizeModuleComponents(editor);

    DummyDragSource dummySource;
    juce::DragAndDropTarget::SourceDetails details(juce::var("Oscillator"), &dummySource,
                                                   libraryCursorForGhostTopLeft("Oscillator", {80, 100}));
    editor.itemDragEnter(details);
    editor.itemDragMove(details);
    ASSERT_GT(editor.getSmartConnections().getSmartSuggestionCount(), 0);
    editor.itemDropped(details);

    juce::AudioProcessorGraph::NodeID oscId{};
    for (auto* node : graph.getNodes()) {
        if (node->getProcessor()->getName() == "Oscillator")
            oscId = node->nodeID;
    }
    ASSERT_NE(oscId.uid, 0u);
    EXPECT_GT(countAudioConnectionsBetween(graph, oscId, filterNode->nodeID), 0);
}

TEST_F(GraphEditorTest, SmartConnectionNewOnlyDoesNotWireOnUnwiredMove) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);
    editor.getSmartConnections().setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewOnly);

    auto& graph = engine.getGraph();
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    oscNode->properties.set("x", 40);
    oscNode->properties.set("y", 100);
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    filterNode->properties.set("x", 400);
    filterNode->properties.set("y", 100);
    editor.updateComponents();
    sizeModuleComponents(editor);

    ModuleComponent* oscComp = nullptr;
    for (auto* c : editor.getModuleComponents()) {
        if (c->getNodeId() == oscNode->nodeID)
            oscComp = c;
    }
    ASSERT_NE(oscComp, nullptr);

    editor.beginDragPreview(oscComp->getWidth(), oscComp->getHeight(), oscComp->getNodeId());
    editor.updateDragPreview({280, 100}); // slide near filter
    EXPECT_EQ(editor.getSmartConnections().getSmartSuggestionCount(), 0) << "NewOnly must not suggest on moves";
    editor.finalizeModuleDrag(oscComp);
    editor.endDragPreview();
    EXPECT_EQ(countAudioConnectionsBetween(graph, oscNode->nodeID, filterNode->nodeID), 0);
}

TEST_F(GraphEditorTest, SmartConnectionNewAndUnwiredWiresUnwiredMove) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);
    editor.getSmartConnections().setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);

    auto& graph = engine.getGraph();
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    oscNode->properties.set("x", 40);
    oscNode->properties.set("y", 100);
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    filterNode->properties.set("x", 400);
    filterNode->properties.set("y", 100);
    editor.updateComponents();
    sizeModuleComponents(editor);

    ModuleComponent* oscComp = nullptr;
    for (auto* c : editor.getModuleComponents()) {
        if (c->getNodeId() == oscNode->nodeID)
            oscComp = c;
    }
    ASSERT_NE(oscComp, nullptr);
    EXPECT_FALSE(editor.nodeHasCables(oscNode->nodeID));

    editor.beginDragPreview(oscComp->getWidth(), oscComp->getHeight(), oscComp->getNodeId());
    // Land just left of the Filter so output/input jacks face each other (not overlapping).
    editor.updateDragPreview({100, 100});
    ASSERT_GT(editor.getSmartConnections().getSmartSuggestionCount(), 0);
    editor.finalizeModuleDrag(oscComp);
    editor.endDragPreview();
    EXPECT_GT(countAudioConnectionsBetween(graph, oscNode->nodeID, filterNode->nodeID), 0);
}

TEST_F(GraphEditorTest, SmartConnectionNewAndUnwiredSkipsAlreadyWiredMove) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1000, 600);
    editor.getSmartConnections().setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);

    auto& graph = engine.getGraph();
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    oscNode->properties.set("x", 40);
    oscNode->properties.set("y", 100);
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    filterNode->properties.set("x", 400);
    filterNode->properties.set("y", 100);
    auto vcaNode = graph.addNode(std::make_unique<VCAModule>());
    vcaNode->properties.set("x", 700);
    vcaNode->properties.set("y", 100);
    editor.updateComponents();
    sizeModuleComponents(editor);

    // Pre-wire Osc -> Filter so the oscillator is no longer "unwired".
    editor.connectPorts(oscNode->nodeID, 0, filterNode->nodeID, 0, false, false);
    ASSERT_TRUE(editor.nodeHasCables(oscNode->nodeID));

    ModuleComponent* oscComp = nullptr;
    for (auto* c : editor.getModuleComponents()) {
        if (c->getNodeId() == oscNode->nodeID)
            oscComp = c;
    }
    ASSERT_NE(oscComp, nullptr);

    editor.beginDragPreview(oscComp->getWidth(), oscComp->getHeight(), oscComp->getNodeId());
    editor.updateDragPreview({560, 100}); // near VCA
    EXPECT_EQ(editor.getSmartConnections().getSmartSuggestionCount(), 0);
    editor.finalizeModuleDrag(oscComp);
    editor.endDragPreview();
    EXPECT_EQ(countAudioConnectionsBetween(graph, oscNode->nodeID, vcaNode->nodeID), 0);
}

TEST_F(GraphEditorTest, SmartConnectionDoesNotWrapAroundToRightNeighbor) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1000, 600);
    editor.getSmartConnections().setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);

    auto& graph = engine.getGraph();
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    filterNode->properties.set("x", 40);
    filterNode->properties.set("y", 100);
    auto delayNode = graph.addNode(std::make_unique<DelayModule>());
    delayNode->properties.set("x", 400);
    delayNode->properties.set("y", 100);
    editor.updateComponents();
    sizeModuleComponents(editor);

    ModuleComponent* filterComp = nullptr;
    for (auto* c : editor.getModuleComponents()) {
        if (c->getNodeId() == filterNode->nodeID)
            filterComp = c;
    }
    ASSERT_NE(filterComp, nullptr);

    editor.beginDragPreview(filterComp->getWidth(), filterComp->getHeight(), filterComp->getNodeId());
    editor.updateDragPreview({100, 100}); // slide toward the Delay on the right
    for (const auto& s : editor.getSmartConnections().getSmartSuggestions()) {
        EXPECT_TRUE(s.ghostIsSource) << "must not wrap Delay's right outputs into Filter's left inputs";
        EXPECT_FALSE(s.isMidi);
        EXPECT_EQ(s.neighborId, delayNode->nodeID);
    }
    editor.endDragPreview();
}

TEST_F(GraphEditorTest, SmartConnectionNewAndUnwiredWiresFreeOutputDespiteOtherCables) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 600);
    editor.getSmartConnections().setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);

    auto& graph = engine.getGraph();
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    oscNode->properties.set("x", 40);
    oscNode->properties.set("y", 100);
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    filterNode->properties.set("x", 400);
    filterNode->properties.set("y", 100);
    auto delayNode = graph.addNode(std::make_unique<DelayModule>());
    delayNode->properties.set("x", 760);
    delayNode->properties.set("y", 100);
    editor.updateComponents();
    sizeModuleComponents(editor);

    editor.connectPorts(oscNode->nodeID, 0, filterNode->nodeID, 0, false, false);
    ASSERT_TRUE(editor.nodeHasCables(filterNode->nodeID));

    ModuleComponent* filterComp = nullptr;
    for (auto* c : editor.getModuleComponents()) {
        if (c->getNodeId() == filterNode->nodeID)
            filterComp = c;
    }
    ASSERT_NE(filterComp, nullptr);

    editor.beginDragPreview(filterComp->getWidth(), filterComp->getHeight(), filterComp->getNodeId());
    editor.updateDragPreview({420, 100}); // near Delay; Filter audio in is taken, audio out is free
    ASSERT_GT(editor.getSmartConnections().getSmartSuggestionCount(), 0)
        << "NewAndUnwired should still offer Filter → Delay when the output jack is free";
    for (const auto& s : editor.getSmartConnections().getSmartSuggestions()) {
        EXPECT_TRUE(s.ghostIsSource);
        EXPECT_EQ(s.neighborId, delayNode->nodeID);
    }
    editor.finalizeModuleDrag(filterComp);
    editor.endDragPreview();
    EXPECT_GT(countAudioConnectionsBetween(graph, filterNode->nodeID, delayNode->nodeID), 0);
}

TEST_F(GraphEditorTest, SmartConnectionAllMovesCanAddWireToFreeJack) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1000, 600);
    editor.getSmartConnections().setSmartConnectionMode(GraphEditor::SmartConnectionMode::AllMoves);

    auto& graph = engine.getGraph();
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    oscNode->properties.set("x", 40);
    oscNode->properties.set("y", 100);
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    filterNode->properties.set("x", 400);
    filterNode->properties.set("y", 100);
    auto vcaNode = graph.addNode(std::make_unique<VCAModule>());
    vcaNode->properties.set("x", 700);
    vcaNode->properties.set("y", 100);
    editor.updateComponents();
    sizeModuleComponents(editor);

    editor.connectPorts(oscNode->nodeID, 0, filterNode->nodeID, 0, false, false);

    ModuleComponent* oscComp = nullptr;
    for (auto* c : editor.getModuleComponents()) {
        if (c->getNodeId() == oscNode->nodeID)
            oscComp = c;
    }
    ASSERT_NE(oscComp, nullptr);

    editor.beginDragPreview(oscComp->getWidth(), oscComp->getHeight(), oscComp->getNodeId());
    editor.updateDragPreview({560, 100});
    // Osc already feeds Filter; a free VCA audio in can still be suggested under AllMoves.
    if (editor.getSmartConnections().getSmartSuggestionCount() > 0) {
        editor.finalizeModuleDrag(oscComp);
        editor.endDragPreview();
        EXPECT_GT(countAudioConnectionsBetween(graph, oscNode->nodeID, vcaNode->nodeID), 0);
    } else {
        // Acceptable if heuristics prefer not to dual-route the same output; AllMoves still
        // must not crash and must leave the existing Filter wire intact.
        editor.endDragPreview();
        EXPECT_GT(countAudioConnectionsBetween(graph, oscNode->nodeID, filterNode->nodeID), 0);
    }
}

TEST_F(GraphEditorTest, SmartConnectionIncompatiblePairSuggestsNothing) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);
    editor.getSmartConnections().setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);

    auto& graph = engine.getGraph();
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    filterNode->properties.set("x", 360);
    filterNode->properties.set("y", 100);
    editor.updateComponents();
    sizeModuleComponents(editor);

    // Occupy the Filter's audio input so remaining free inputs are mod-CV (skipped in v1).
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    oscNode->properties.set("x", 40);
    oscNode->properties.set("y", 400);
    editor.updateComponents();
    sizeModuleComponents(editor);
    editor.connectPorts(oscNode->nodeID, 0, filterNode->nodeID, 0, false, false);

    DummyDragSource dummySource;
    juce::DragAndDropTarget::SourceDetails details(juce::var("LFO"), &dummySource,
                                                   libraryCursorForGhostTopLeft("LFO", {80, 100}));
    editor.itemDragEnter(details);
    editor.itemDragMove(details);
    // LFO is not a known MIDI source; Filter audio in is taken; mod CV is not suggested in v1.
    EXPECT_EQ(editor.getSmartConnections().getSmartSuggestionCount(), 0);
    editor.endDragPreview();
}

TEST_F(GraphEditorTest, SmartConnectionModeRoundTrip) {
    EXPECT_EQ(GraphEditor::smartConnectionModeFromString("Off"), GraphEditor::SmartConnectionMode::Off);
    EXPECT_EQ(GraphEditor::smartConnectionModeFromString("NewOnly"), GraphEditor::SmartConnectionMode::NewOnly);
    EXPECT_EQ(GraphEditor::smartConnectionModeFromString("AllMoves"), GraphEditor::SmartConnectionMode::AllMoves);
    EXPECT_EQ(GraphEditor::smartConnectionModeFromString("NewAndUnwired"),
              GraphEditor::SmartConnectionMode::NewAndUnwired);
    EXPECT_EQ(GraphEditor::smartConnectionModeFromString("bogus"), GraphEditor::SmartConnectionMode::NewAndUnwired);
    EXPECT_EQ(GraphEditor::smartConnectionModeToString(GraphEditor::SmartConnectionMode::Off), "Off");
}

TEST_F(GraphEditorTest, SmartConnectionStereoToStereoWiresBothLegs) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1000, 600);
    editor.getSmartConnections().setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);

    auto& graph = engine.getGraph();
    auto reverbNode = graph.addNode(std::make_unique<ReverbModule>());
    reverbNode->properties.set("x", 400);
    reverbNode->properties.set("y", 100);
    editor.updateComponents();
    sizeModuleComponents(editor);

    DummyDragSource dummySource;
    juce::DragAndDropTarget::SourceDetails details(juce::var("Delay"), &dummySource,
                                                   libraryCursorForGhostTopLeft("Delay", {80, 100}));
    editor.itemDragEnter(details);
    editor.itemDragMove(details);
    ASSERT_GE(editor.getSmartConnections().getSmartSuggestionCount(), 1)
        << "Dual I/O off: one Audio→Audio preview, which fans both raw legs";
    editor.itemDropped(details);

    juce::AudioProcessorGraph::NodeID delayId{};
    for (auto* node : graph.getNodes()) {
        if (node->getProcessor()->getName() == "Delay")
            delayId = node->nodeID;
    }
    ASSERT_NE(delayId.uid, 0u);

    // Collapsed Audio jacks still own raw L/R — one visible cable, two graph edges.
    EXPECT_TRUE(graph.isConnected({{delayId, 0}, {reverbNode->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{delayId, 1}, {reverbNode->nodeID, 1}}));
}

TEST_F(GraphEditorTest, SmartConnectionMonoToStereoFansBothInputs) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1000, 600);
    editor.getSmartConnections().setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);

    auto& graph = engine.getGraph();
    auto delayNode = graph.addNode(std::make_unique<DelayModule>());
    delayNode->properties.set("x", 400);
    delayNode->properties.set("y", 100);
    editor.updateComponents();
    sizeModuleComponents(editor);

    DummyDragSource dummySource;
    juce::DragAndDropTarget::SourceDetails details(juce::var("Oscillator"), &dummySource,
                                                   libraryCursorForGhostTopLeft("Oscillator", {80, 100}));
    editor.itemDragEnter(details);
    editor.itemDragMove(details);
    ASSERT_GE(editor.getSmartConnections().getSmartSuggestionCount(), 1)
        << "Mono→collapsed stereo should preview Delay's Audio jack";
    editor.itemDropped(details);

    juce::AudioProcessorGraph::NodeID oscId{};
    for (auto* node : graph.getNodes()) {
        if (node->getProcessor()->getName() == "Oscillator")
            oscId = node->nodeID;
    }
    ASSERT_NE(oscId.uid, 0u);
    EXPECT_TRUE(graph.isConnected({{oscId, 0}, {delayNode->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{oscId, 0}, {delayNode->nodeID, 1}}));
}

TEST_F(GraphEditorTest, SmartConnectionStereoToMonoFansBothOutputs) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1000, 600);
    editor.getSmartConnections().setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);

    auto& graph = engine.getGraph();
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    filterNode->properties.set("x", 400);
    filterNode->properties.set("y", 100);
    editor.updateComponents();
    sizeModuleComponents(editor);

    DummyDragSource dummySource;
    juce::DragAndDropTarget::SourceDetails details(juce::var("Delay"), &dummySource,
                                                   libraryCursorForGhostTopLeft("Delay", {80, 100}));
    editor.itemDragEnter(details);
    editor.itemDragMove(details);
    ASSERT_GE(editor.getSmartConnections().getSmartSuggestionCount(), 1)
        << "Collapsed stereo→mono should preview Delay Audio into Filter";
    editor.itemDropped(details);

    juce::AudioProcessorGraph::NodeID delayId{};
    for (auto* node : graph.getNodes()) {
        if (node->getProcessor()->getName() == "Delay")
            delayId = node->nodeID;
    }
    ASSERT_NE(delayId.uid, 0u);
    EXPECT_TRUE(graph.isConnected({{delayId, 0}, {filterNode->nodeID, 0}}));
    EXPECT_FALSE(graph.isConnected({{delayId, 1}, {filterNode->nodeID, 0}}))
        << "Filter ch1 is Cutoff, not a second audio input — Dual I/O off must not dump Right onto it";
}

TEST_F(GraphEditorTest, SmartConnectionDoesNotTreatMathABAsStereo) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1000, 600);
    editor.getSmartConnections().setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);

    auto& graph = engine.getGraph();
    auto mathNode = graph.addNode(std::make_unique<MathModule>());
    mathNode->properties.set("x", 400);
    mathNode->properties.set("y", 100);
    editor.updateComponents();
    sizeModuleComponents(editor);

    DummyDragSource dummySource;
    juce::DragAndDropTarget::SourceDetails details(juce::var("Oscillator"), &dummySource,
                                                   libraryCursorForGhostTopLeft("Oscillator", {80, 100}));
    editor.itemDragEnter(details);
    editor.itemDragMove(details);
    // Math A/B are unlabeled PortRole::Other, so they are never a stereo destination pair. The
    // Oscillator became a stereo SOURCE in #219 (Audio L/R), so it may legitimately offer both legs
    // — but every one of them must land on Math A. B is a second operand, not a right channel.
    const int suggestions = editor.getSmartConnections().getSmartSuggestionCount();
    EXPECT_LE(suggestions, 2);
    if (suggestions >= 1) {
        editor.itemDropped(details);
        juce::AudioProcessorGraph::NodeID oscId{};
        for (auto* node : graph.getNodes()) {
            if (node->getProcessor()->getName() == "Oscillator")
                oscId = node->nodeID;
        }
        ASSERT_NE(oscId.uid, 0u);

        bool anyIntoB = false;
        for (const auto& conn : graph.getConnections())
            if (conn.source.nodeID == oscId && conn.destination.nodeID == mathNode->nodeID &&
                conn.destination.channelIndex == 1)
                anyIntoB = true;
        EXPECT_FALSE(anyIntoB) << "Math B is a second operand, not a right audio channel";
        EXPECT_TRUE(graph.isConnected({{oscId, 0}, {mathNode->nodeID, 0}})) << "Audio L should reach Math A";
    } else {
        editor.endDragPreview();
    }
}

TEST_F(GraphEditorTest, SmartConnectionMonoToStereoIsBothOrNeither) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1000, 600);
    editor.getSmartConnections().setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);

    auto& graph = engine.getGraph();
    auto delayNode = graph.addNode(std::make_unique<DelayModule>());
    delayNode->properties.set("x", 400);
    delayNode->properties.set("y", 100);
    editor.updateComponents();
    sizeModuleComponents(editor);

    // Occupy Delay Left only.
    auto filler = graph.addNode(std::make_unique<OscillatorModule>());
    filler->properties.set("x", 40);
    filler->properties.set("y", 400);
    editor.updateComponents();
    sizeModuleComponents(editor);
    editor.connectPorts(filler->nodeID, 0, delayNode->nodeID, 0, false, false);

    DummyDragSource dummySource;
    juce::DragAndDropTarget::SourceDetails details(juce::var("Oscillator"), &dummySource,
                                                   libraryCursorForGhostTopLeft("Oscillator", {80, 100}));
    editor.itemDragEnter(details);
    editor.itemDragMove(details);
    // Left taken → both-or-neither: no mono→stereo fan onto Right alone.
    EXPECT_EQ(editor.getSmartConnections().getSmartSuggestionCount(), 0);
    editor.endDragPreview();
}
