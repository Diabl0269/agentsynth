#include "../Source/AppUndoManager.h"
#include "../Source/Modules/ADSRModule.h"
#include "../Source/Modules/AttenuverterModule.h"
#include "../Source/Modules/FilterModule.h"
#include "../Source/Modules/LFOModule.h"
#include "../Source/Modules/ModuleBase.h"
#include "../Source/Modules/OscillatorModule.h"
#include "../Source/Modules/PolyMidiModule.h"
#include "../Source/Modules/SequencerModule.h"
#include "../Source/Modules/VCAModule.h"
#include "../Source/PresetManager.h"
#include "../Source/UI/GraphEditor.h"
#include "../Source/UI/LayoutUtil.h"
#include "../Source/UI/ModuleComponent.h"
#include "../Source/UI/Theme/BuiltInThemes.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

// Define a dummy component to act as the drag source
class DummyDragSource : public juce::Component {};

class GraphEditorTest : public ::testing::Test {};

// Helper to find and set a bool parameter by ID (mirrors LogicalPortTests.cpp's setPolyParam).
static void setPolyParam(juce::AudioProcessor& proc, bool value) {
    for (auto* param : proc.getParameters()) {
        if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param)) {
            if (p->paramID == "poly") {
                p->setValueNotifyingHost(value ? 1.0f : 0.0f);
                return;
            }
        }
    }
}

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
            // Drop position is now snapped to the layout grid (anti-overlap may also offset it).
            EXPECT_EQ(static_cast<int>(node->properties.getWithDefault("x", -1)) % synth::LayoutUtil::kGridSize, 0)
                << "Dropped module x should snap to grid";
            EXPECT_EQ(static_cast<int>(node->properties.getWithDefault("y", -1)) % synth::LayoutUtil::kGridSize, 0)
                << "Dropped module y should snap to grid";
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

// Inc-4: Verify that for the Poly Pad preset's PolyBus (ADSR->VCA) routing,
// sourceVisibleJack and destVisibleJack are within their module's visible port counts.
// This guarantees getPortCenter() lands on a real rendered jack, not a phantom y.
// Placed in AudioEngine-level fixture (no GUI needed) to keep the test headless.
TEST_F(GraphEditorTest, PolyBusWireResolvesToVisibleJacks) {
    AudioEngine engine;
    engine.initialise();
    engine.getGraph().clear();

    bool loaded = synth::PresetManager::loadPreset(6, engine.getGraph());
    ASSERT_TRUE(loaded) << "Poly Pad preset (index 6) must load successfully";

    auto routings = engine.getModulationRoutings();

    // Find the PolyBus routing
    const AudioEngine::ModulationRouting* polyRouting = nullptr;
    for (const auto& r : routings) {
        if (r.kind == AudioEngine::RoutingKind::PolyBus) {
            polyRouting = &r;
            break;
        }
    }
    ASSERT_NE(polyRouting, nullptr) << "Expected a PolyBus routing in the Poly Pad preset";

    // Locate source and dest processors to query their visible port counts.
    auto& graph = engine.getGraph();
    juce::AudioProcessor* srcProcessor = nullptr;
    juce::AudioProcessor* dstProcessor = nullptr;
    for (auto* node : graph.getNodes()) {
        if (node->nodeID == polyRouting->sourceNodeID)
            srcProcessor = node->getProcessor();
        if (node->nodeID == polyRouting->destNodeID)
            dstProcessor = node->getProcessor();
    }
    ASSERT_NE(srcProcessor, nullptr) << "Source node must exist in graph";
    ASSERT_NE(dstProcessor, nullptr) << "Dest node must exist in graph";

    // Get visible port counts — use ModuleBase if available, else fall back.
    int srcVisibleOuts = srcProcessor->getTotalNumOutputChannels();
    int dstVisibleIns = dstProcessor->getTotalNumInputChannels();
    if (auto* mb = dynamic_cast<ModuleBase*>(srcProcessor))
        srcVisibleOuts = mb->getVisibleOutputPortCount();
    if (auto* mb = dynamic_cast<ModuleBase*>(dstProcessor))
        dstVisibleIns = mb->getVisibleInputPortCount();

    // Key assertions: both visible jacks must be within the visible range,
    // so paint() / getPortCenter() will use a real jack (not a phantom y).
    EXPECT_LT(polyRouting->sourceVisibleJack, srcVisibleOuts)
        << "PolyBus sourceVisibleJack (" << polyRouting->sourceVisibleJack
        << ") must be < source visible output count (" << srcVisibleOuts << ")";
    EXPECT_GE(polyRouting->sourceVisibleJack, 0) << "PolyBus sourceVisibleJack must be non-negative";

    EXPECT_LT(polyRouting->destVisibleJack, dstVisibleIns)
        << "PolyBus destVisibleJack (" << polyRouting->destVisibleJack << ") must be < dest visible input count ("
        << dstVisibleIns << ")";
    EXPECT_GE(polyRouting->destVisibleJack, 0) << "PolyBus destVisibleJack must be non-negative";

    engine.shutdown();
}

TEST_F(GraphEditorTest, ReplaceModuleIsUndoable) {
    AudioEngine engine;
    AppUndoManager undoMgr;
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

// ============================================================================
// Grid-layout / anti-overlap tests
// ============================================================================

// DropSnapsPositionToGrid: after itemDropped at a non-grid coordinate, the node's
// persisted x,y must both be multiples of kGridSize=8.
TEST_F(GraphEditorTest, DropSnapsPositionToGrid) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    // Non-grid drop point: (103, 97) — neither is a multiple of 8
    DummyDragSource dummySource;
    juce::var description("Oscillator");
    juce::DragAndDropTarget::SourceDetails details(description, &dummySource, juce::Point<int>(103, 97));

    auto nodeBefore = engine.getGraph().getNodes().size();
    editor.itemDropped(details);
    ASSERT_EQ(engine.getGraph().getNodes().size(), nodeBefore + 1);

    // Find the newly added oscillator node
    juce::AudioProcessorGraph::Node* newNode = nullptr;
    for (auto* node : engine.getGraph().getNodes()) {
        if (dynamic_cast<OscillatorModule*>(node->getProcessor())) {
            newNode = node;
        }
    }
    ASSERT_NE(newNode, nullptr) << "Should find the dropped Oscillator node";

    int x = static_cast<int>(newNode->properties.getWithDefault("x", -1));
    int y = static_cast<int>(newNode->properties.getWithDefault("y", -1));

    EXPECT_GE(x, 0) << "Node x property must be set";
    EXPECT_GE(y, 0) << "Node y property must be set";
    EXPECT_EQ(x % synth::LayoutUtil::kGridSize, 0)
        << "Node x=" << x << " must be a multiple of kGridSize=" << synth::LayoutUtil::kGridSize;
    EXPECT_EQ(y % synth::LayoutUtil::kGridSize, 0)
        << "Node y=" << y << " must be a multiple of kGridSize=" << synth::LayoutUtil::kGridSize;
}

// DropOnOccupiedCellOffsetsToClearSlot: dropping two modules at the same position
// must result in non-overlapping bounding boxes (gap >= kCollisionGap).
TEST_F(GraphEditorTest, DropOnOccupiedCellOffsetsToClearSlot) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    // Drop first module
    DummyDragSource dummySource;
    {
        juce::var description("Oscillator");
        juce::DragAndDropTarget::SourceDetails details(description, &dummySource, juce::Point<int>(200, 200));
        editor.itemDropped(details);
    }

    // Drop second module at the same position
    {
        juce::var description("Filter");
        juce::DragAndDropTarget::SourceDetails details(description, &dummySource, juce::Point<int>(200, 200));
        editor.itemDropped(details);
    }

    // Gather positions and sizes from module components
    struct ModInfo {
        juce::Rectangle<int> bounds;
    };
    std::vector<ModInfo> mods;

    auto* content = editor.getChildComponent(0);
    ASSERT_NE(content, nullptr);
    for (auto* child : content->getChildren()) {
        if (auto* mc = dynamic_cast<ModuleComponent*>(child)) {
            mods.push_back({mc->getBoundsInParent()});
        }
    }

    ASSERT_GE(static_cast<int>(mods.size()), 2) << "Expected at least 2 module components after two drops";

    // Check every pair: bounding boxes must not intersect when inflated by kCollisionGap/2
    const int gap = synth::LayoutUtil::kCollisionGap;
    for (size_t i = 0; i < mods.size(); ++i) {
        for (size_t j = i + 1; j < mods.size(); ++j) {
            auto ri = mods[i].bounds.expanded(gap / 2);
            auto rj = mods[j].bounds.expanded(gap / 2);
            EXPECT_FALSE(ri.intersects(rj))
                << "Module " << i << " (" << mods[i].bounds.toString() << ") and module " << j << " ("
                << mods[j].bounds.toString() << ") overlap after anti-overlap resolution";
        }
    }
}

// DragPreviewGhostTracksResolvedPlacement: beginDragPreview / updateDragPreview set a ghost
// equal to resolvePlacement and the ghost does NOT intersect an existing module.
// endDragPreview() resets the active flag.
TEST_F(GraphEditorTest, DragPreviewGhostTracksResolvedPlacement) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    // Place a module at a known canvas position so the ghost has something to avoid.
    auto& graph = engine.getGraph();
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    oscNode->properties.set("x", 100);
    oscNode->properties.set("y", 100);
    editor.updateComponents();

    // Find and size the module component so its bounds are valid for collision checks.
    auto* content = editor.getChildComponent(0);
    ASSERT_NE(content, nullptr);
    for (auto* child : content->getChildren()) {
        if (auto* mc = dynamic_cast<ModuleComponent*>(child)) {
            if (mc->getModule() == oscNode->getProcessor())
                mc->setSize(280, 300);
        }
    }

    // Start a drag preview for a new (library) module: selfId = {} (no existing node)
    EXPECT_FALSE(editor.isDragPreviewActive());
    editor.beginDragPreview(280, 300, juce::AudioProcessorGraph::NodeID{});
    EXPECT_TRUE(editor.isDragPreviewActive());

    // Desired position OVERLAPS the existing oscillator (same coordinates).
    juce::Point<int> desiredOverlap(100, 100);
    editor.updateDragPreview(desiredOverlap);

    auto ghost = editor.getDragPreviewGhost();
    EXPECT_FALSE(ghost.isEmpty()) << "Ghost rect should be non-empty after updateDragPreview";

    // The ghost must equal what resolvePlacement returns for the same inputs.
    auto expected = editor.resolvePlacement(desiredOverlap, 280, 300, juce::AudioProcessorGraph::NodeID{});
    EXPECT_EQ(ghost.getTopLeft(), expected) << "Ghost top-left must equal resolvePlacement result; got "
                                            << ghost.getTopLeft().toString() << " but expected " << expected.toString();

    // The ghost must NOT intersect the existing module's bounds (collision was resolved).
    juce::Rectangle<int> oscBounds(100, 100, 280, 300);
    const int gap = synth::LayoutUtil::kCollisionGap;
    EXPECT_FALSE(ghost.expanded(gap / 2).intersects(oscBounds.expanded(gap / 2)))
        << "Ghost rect (" << ghost.toString() << ") must not overlap existing module (" << oscBounds.toString()
        << ") after anti-overlap resolution";

    // endDragPreview clears the active flag.
    editor.endDragPreview();
    EXPECT_FALSE(editor.isDragPreviewActive());
    EXPECT_TRUE(editor.getDragPreviewGhost().isEmpty());
}

// DropUsesRealModuleSizeForAntiOverlap: drop two tall Oscillator modules at the same canvas
// point. With the old 300px estimate both would land on the same slot because the estimate
// was too short to detect overlap; with real-size finalize they must not overlap.
TEST_F(GraphEditorTest, DropUsesRealModuleSizeForAntiOverlap) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 900);

    DummyDragSource dummySource;

    // Drop two Oscillators at the same position. The second must be displaced because the
    // first occupies that slot (real Oscillator height ~530px far exceeds old 300px estimate).
    {
        juce::var description("Oscillator");
        juce::DragAndDropTarget::SourceDetails details(description, &dummySource, juce::Point<int>(200, 200));
        editor.itemDropped(details);
    }
    {
        juce::var description("Oscillator");
        juce::DragAndDropTarget::SourceDetails details(description, &dummySource, juce::Point<int>(200, 200));
        editor.itemDropped(details);
    }

    // Collect all ModuleComponent bounds from the content component.
    auto* content = editor.getChildComponent(0);
    ASSERT_NE(content, nullptr);

    std::vector<juce::Rectangle<int>> bounds;
    for (auto* child : content->getChildren()) {
        if (auto* mc = dynamic_cast<ModuleComponent*>(child)) {
            auto b = mc->getBoundsInParent();
            if (b.getWidth() > 0 && b.getHeight() > 0)
                bounds.push_back(b);
        }
    }

    ASSERT_GE(static_cast<int>(bounds.size()), 2) << "Expected at least 2 module components after two drops";

    // All pairs must be non-overlapping (with collision gap).
    const int gap = synth::LayoutUtil::kCollisionGap;
    for (size_t i = 0; i < bounds.size(); ++i) {
        for (size_t j = i + 1; j < bounds.size(); ++j) {
            auto ri = bounds[i].expanded(gap / 2);
            auto rj = bounds[j].expanded(gap / 2);
            EXPECT_FALSE(ri.intersects(rj))
                << "Oscillator " << i << " (" << bounds[i].toString() << ") and Oscillator " << j << " ("
                << bounds[j].toString() << ") overlap — real-size finalize should have displaced the second";
        }
    }
}

// ============================================================================
// Item 4: Alignment guide rendering tests
// ============================================================================

TEST_F(GraphEditorTest, AlignmentGuideDrawingThemeAware) {
    // Verify paintOverChildren() uses theme colors correctly.
    synth::theme::AppLookAndFeel lf;
    lf.applyTheme(synth::theme::makeObsidian());

    const auto& m = lf.getTheme().metrics;
    const auto guideColor = lf.getTheme().colors.textMuted.withAlpha(m.guideAlpha);

    // Verify opacity matches Item 4 spec (70%)
    EXPECT_FLOAT_EQ(m.guideAlpha, 0.7f);
    EXPECT_NEAR(guideColor.getFloatAlpha(), 0.7f, 0.01f);

    // Verify line width matches spec
    EXPECT_FLOAT_EQ(m.guideLineWidth, 1.5f);
}

// ============================================================================
// Issue #163: poly connections auto-fan-out
// ============================================================================

// ---- Pure resolvePolyLink tests (no graph needed) ----

TEST_F(GraphEditorTest, ResolvePolyLinkFansEnvelopeToPolyVCA) {
    ADSRModule adsr;
    VCAModule vca;
    setPolyParam(adsr, true);
    setPolyParam(vca, true);

    auto link = GraphEditor::resolvePolyLink(&adsr, 0, &vca, 1);
    EXPECT_EQ(link.sourceRawChannel, 0);
    EXPECT_EQ(link.destRawChannel, 8);
    EXPECT_EQ(link.voiceCount, 8);
}

TEST_F(GraphEditorTest, ResolvePolyLinkPicksFanMatchingRole) {
    PolyMidiModule polyMidi;
    OscillatorModule osc;
    ADSRModule adsr;
    setPolyParam(osc, true);
    setPolyParam(adsr, true);

    // PolyMidi's "Poly Out" jack fronts both a Pitch fan (raw 0) and a Gate fan (raw 8).
    // Into the Oscillator's Pitch input, the Pitch fan must win.
    auto linkToOsc = GraphEditor::resolvePolyLink(&polyMidi, 0, &osc, 0);
    EXPECT_EQ(linkToOsc.sourceRawChannel, 0);
    EXPECT_EQ(linkToOsc.destRawChannel, 0);
    EXPECT_EQ(linkToOsc.voiceCount, 8);

    // Into the ADSR's Gate input (same source visible jack), the Gate fan must win instead.
    auto linkToAdsr = GraphEditor::resolvePolyLink(&polyMidi, 0, &adsr, 0);
    EXPECT_EQ(linkToAdsr.sourceRawChannel, 8);
    EXPECT_EQ(linkToAdsr.destRawChannel, 0);
    EXPECT_EQ(linkToAdsr.voiceCount, 8);
}

TEST_F(GraphEditorTest, ResolvePolyLinkStaysMonoWhenDestIsMono) {
    ADSRModule polyAdsr;
    setPolyParam(polyAdsr, true);
    VCAModule monoVca; // poly defaults to false

    // A poly source into a mono jack must not sum eight envelopes onto one CV channel.
    auto link = GraphEditor::resolvePolyLink(&polyAdsr, 0, &monoVca, 1);
    EXPECT_EQ(link.sourceRawChannel, 0);
    EXPECT_EQ(link.destRawChannel, 1);
    EXPECT_EQ(link.voiceCount, 1);
    EXPECT_EQ(link.sourceStride, 1);
}

TEST_F(GraphEditorTest, ResolvePolyLinkBroadcastsMonoSourceAcrossModCvFan) {
    // One mono modulator on a per-voice mod-CV fan drives every voice: all eight wires leave the
    // same source channel (sourceStride == 0) and land on VCA raw channels 8-15.
    LFOModule lfo;
    VCAModule polyVca;
    setPolyParam(polyVca, true);

    auto link = GraphEditor::resolvePolyLink(&lfo, 0, &polyVca, 1);
    EXPECT_EQ(link.sourceRawChannel, 0);
    EXPECT_EQ(link.destRawChannel, 8);
    EXPECT_EQ(link.voiceCount, 8);
    EXPECT_EQ(link.sourceStride, 0);
}

TEST_F(GraphEditorTest, ResolvePolyLinkDoesNotBroadcastAudioOrPitchFans) {
    // Broadcasting is limited to ModCV. Audio would build a paraphonic voice stack, and Pitch/Gate
    // would make all eight voices sound the same note — both stay single head-to-head wires.
    OscillatorModule monoOsc; // poly defaults to false
    FilterModule polyFilter;
    setPolyParam(polyFilter, true);

    auto audioLink = GraphEditor::resolvePolyLink(&monoOsc, 0, &polyFilter, 0);
    EXPECT_EQ(audioLink.voiceCount, 1);
    EXPECT_EQ(audioLink.sourceStride, 1);

    LFOModule lfo;
    OscillatorModule polyOsc;
    setPolyParam(polyOsc, true);

    auto pitchLink = GraphEditor::resolvePolyLink(&lfo, 0, &polyOsc, 0);
    EXPECT_EQ(pitchLink.voiceCount, 1);
    EXPECT_EQ(pitchLink.sourceStride, 1);

    ADSRModule polyAdsr;
    setPolyParam(polyAdsr, true);

    auto gateLink = GraphEditor::resolvePolyLink(&lfo, 0, &polyAdsr, 0);
    EXPECT_EQ(gateLink.voiceCount, 1);
    EXPECT_EQ(gateLink.sourceStride, 1);
}

// ---- Integration tests (full graph + drag/toggle interactions) ----

TEST_F(GraphEditorTest, DragBetweenPolyModulesFansOutAllVoices) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto adsrNode = engine.getGraph().addNode(std::make_unique<ADSRModule>());
    auto vcaNode = engine.getGraph().addNode(std::make_unique<VCAModule>());
    setPolyParam(*adsrNode->getProcessor(), true);
    setPolyParam(*vcaNode->getProcessor(), true);

    editor.updateComponents();

    ModuleComponent* adsrComp = nullptr;
    ModuleComponent* vcaComp = nullptr;

    auto* content = editor.getChildComponent(0);
    if (content) {
        for (auto* contentChild : content->getChildren()) {
            if (auto* mod = dynamic_cast<ModuleComponent*>(contentChild)) {
                if (mod->getModule() == adsrNode->getProcessor())
                    adsrComp = mod;
                if (mod->getModule() == vcaNode->getProcessor())
                    vcaComp = mod;
            }
        }
    }

    ASSERT_NE(adsrComp, nullptr);
    ASSERT_NE(vcaComp, nullptr);

    adsrComp->setBounds(0, 0, 100, 100);
    vcaComp->setBounds(200, 0, 100, 100);

    editor.beginConnectionDrag(adsrComp, 0, false, false, juce::Point<int>(0, 0));
    editor.dragConnection(juce::Point<int>(50, 0));

    auto vcaTargetPoint = vcaComp->getBounds().getPosition() + vcaComp->getPortCenter(1, true);
    editor.endConnectionDrag(vcaTargetPoint);

    auto& graph = engine.getGraph();

    for (int i = 0; i < 8; ++i) {
        bool found = false;
        for (auto& conn : graph.getConnections()) {
            if (conn.source.nodeID == adsrNode->nodeID && conn.source.channelIndex == i &&
                conn.destination.nodeID == vcaNode->nodeID && conn.destination.channelIndex == 8 + i) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "Missing fan connection for voice " << i;
    }

    int connectionCount = 0;
    for (auto& conn : graph.getConnections())
        if (conn.source.nodeID == adsrNode->nodeID && conn.destination.nodeID == vcaNode->nodeID)
            ++connectionCount;
    EXPECT_EQ(connectionCount, 8);

    bool foundAttenuverter = false;
    for (auto* node : graph.getNodes())
        if (dynamic_cast<AttenuverterModule*>(node->getProcessor()) != nullptr)
            foundAttenuverter = true;
    EXPECT_FALSE(foundAttenuverter);
}

TEST_F(GraphEditorTest, DragBetweenMonoModulesIsUnchanged) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto adsrNode = engine.getGraph().addNode(std::make_unique<ADSRModule>());
    auto vcaNode = engine.getGraph().addNode(std::make_unique<VCAModule>());

    editor.updateComponents();

    ModuleComponent* adsrComp = nullptr;
    ModuleComponent* vcaComp = nullptr;

    auto* content = editor.getChildComponent(0);
    if (content) {
        for (auto* contentChild : content->getChildren()) {
            if (auto* mod = dynamic_cast<ModuleComponent*>(contentChild)) {
                if (mod->getModule() == adsrNode->getProcessor())
                    adsrComp = mod;
                if (mod->getModule() == vcaNode->getProcessor())
                    vcaComp = mod;
            }
        }
    }

    ASSERT_NE(adsrComp, nullptr);
    ASSERT_NE(vcaComp, nullptr);

    adsrComp->setBounds(0, 0, 100, 100);
    vcaComp->setBounds(200, 0, 100, 100);

    editor.beginConnectionDrag(adsrComp, 0, false, false, juce::Point<int>(0, 0));
    editor.dragConnection(juce::Point<int>(50, 0));

    auto vcaTargetPoint = vcaComp->getBounds().getPosition() + vcaComp->getPortCenter(1, true);
    editor.endConnectionDrag(vcaTargetPoint);

    auto& graph = engine.getGraph();

    // Legacy behaviour: a mono mod-CV wire is mediated by an attenuverter, not a direct connection.
    bool foundAttenuverter = false;
    for (auto* node : graph.getNodes())
        if (dynamic_cast<AttenuverterModule*>(node->getProcessor()) != nullptr)
            foundAttenuverter = true;
    EXPECT_TRUE(foundAttenuverter);

    bool directConnectionFound = false;
    for (auto& conn : graph.getConnections())
        if (conn.source.nodeID == adsrNode->nodeID && conn.destination.nodeID == vcaNode->nodeID)
            directConnectionFound = true;
    EXPECT_FALSE(directConnectionFound);
}

TEST_F(GraphEditorTest, DisconnectPolyPortRemovesEntireFan) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto adsrNode = engine.getGraph().addNode(std::make_unique<ADSRModule>());
    auto vcaNode = engine.getGraph().addNode(std::make_unique<VCAModule>());
    setPolyParam(*adsrNode->getProcessor(), true);
    setPolyParam(*vcaNode->getProcessor(), true);

    editor.updateComponents();

    ModuleComponent* adsrComp = nullptr;
    ModuleComponent* vcaComp = nullptr;

    auto* content = editor.getChildComponent(0);
    if (content) {
        for (auto* contentChild : content->getChildren()) {
            if (auto* mod = dynamic_cast<ModuleComponent*>(contentChild)) {
                if (mod->getModule() == adsrNode->getProcessor())
                    adsrComp = mod;
                if (mod->getModule() == vcaNode->getProcessor())
                    vcaComp = mod;
            }
        }
    }

    ASSERT_NE(adsrComp, nullptr);
    ASSERT_NE(vcaComp, nullptr);

    adsrComp->setBounds(0, 0, 100, 100);
    vcaComp->setBounds(200, 0, 100, 100);

    editor.beginConnectionDrag(adsrComp, 0, false, false, juce::Point<int>(0, 0));
    editor.dragConnection(juce::Point<int>(50, 0));

    auto vcaTargetPoint = vcaComp->getBounds().getPosition() + vcaComp->getPortCenter(1, true);
    editor.endConnectionDrag(vcaTargetPoint);

    auto& graph = engine.getGraph();

    int preCount = 0;
    for (auto& conn : graph.getConnections())
        if (conn.source.nodeID == adsrNode->nodeID && conn.destination.nodeID == vcaNode->nodeID)
            ++preCount;
    ASSERT_EQ(preCount, 8) << "Setup must produce the 8-voice fan before disconnecting";

    editor.disconnectPort(vcaComp, 1, true, false);

    int postCount = 0;
    for (auto& conn : graph.getConnections())
        if (conn.source.nodeID == adsrNode->nodeID && conn.destination.nodeID == vcaNode->nodeID)
            ++postCount;
    EXPECT_EQ(postCount, 0);
}

TEST_F(GraphEditorTest, DragMonoLfoOntoPolyVcaBroadcastsToEveryVoice) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto lfoNode = engine.getGraph().addNode(std::make_unique<LFOModule>());
    auto vcaNode = engine.getGraph().addNode(std::make_unique<VCAModule>());
    setPolyParam(*vcaNode->getProcessor(), true);

    editor.updateComponents();

    ModuleComponent* lfoComp = nullptr;
    ModuleComponent* vcaComp = nullptr;

    auto* content = editor.getChildComponent(0);
    if (content) {
        for (auto* contentChild : content->getChildren()) {
            if (auto* mod = dynamic_cast<ModuleComponent*>(contentChild)) {
                if (mod->getModule() == lfoNode->getProcessor())
                    lfoComp = mod;
                if (mod->getModule() == vcaNode->getProcessor())
                    vcaComp = mod;
            }
        }
    }

    ASSERT_NE(lfoComp, nullptr);
    ASSERT_NE(vcaComp, nullptr);

    lfoComp->setBounds(0, 0, 100, 100);
    vcaComp->setBounds(200, 0, 100, 100);

    editor.beginConnectionDrag(lfoComp, 0, false, false, juce::Point<int>(0, 0));
    editor.dragConnection(juce::Point<int>(50, 0));

    auto vcaTargetPoint = vcaComp->getBounds().getPosition() + vcaComp->getPortCenter(1, true);
    editor.endConnectionDrag(vcaTargetPoint);

    auto& graph = engine.getGraph();

    // Every voice's gain CV is fed from the SAME LFO output channel — that is the broadcast.
    for (int i = 0; i < 8; ++i) {
        bool found = false;
        for (auto& conn : graph.getConnections()) {
            if (conn.source.nodeID == lfoNode->nodeID && conn.source.channelIndex == 0 &&
                conn.destination.nodeID == vcaNode->nodeID && conn.destination.channelIndex == 8 + i) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "Missing broadcast connection for voice " << i;
    }

    int connectionCount = 0;
    for (auto& conn : graph.getConnections())
        if (conn.source.nodeID == lfoNode->nodeID && conn.destination.nodeID == vcaNode->nodeID)
            ++connectionCount;
    EXPECT_EQ(connectionCount, 8);

    // The broadcast must read back as ONE PolyBus wire with an x8 badge, not eight stacked wires.
    int polyBusCount = 0;
    for (const auto& r : engine.getModulationRoutings()) {
        if (r.kind == AudioEngine::RoutingKind::PolyBus && r.sourceNodeID == lfoNode->nodeID &&
            r.destNodeID == vcaNode->nodeID) {
            ++polyBusCount;
            EXPECT_EQ(r.voiceCount, 8);
            EXPECT_EQ(r.destChannelIndex, 8);
            EXPECT_EQ(r.role, PortRole::ModCV);
        }
    }
    EXPECT_EQ(polyBusCount, 1);

    int directCvCount = 0;
    for (const auto& r : engine.getModulationRoutings())
        if (r.kind == AudioEngine::RoutingKind::DirectCV && r.sourceNodeID == lfoNode->nodeID)
            ++directCvCount;
    EXPECT_EQ(directCvCount, 0) << "Broadcast edges should be consumed by the PolyBus collapse";
}

TEST_F(GraphEditorTest, TogglingPolyOnBroadcastsExistingMonoModWire) {
    // A mono LFO -> mono VCA CV wire (an attenuverter chain) must spread across all eight voices
    // when the VCA is switched to poly, rather than staying on voice 0.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto lfoNode = engine.getGraph().addNode(std::make_unique<LFOModule>());
    auto vcaNode = engine.getGraph().addNode(std::make_unique<VCAModule>());

    editor.updateComponents();

    ModuleComponent* lfoComp = nullptr;
    ModuleComponent* vcaComp = nullptr;

    auto* content = editor.getChildComponent(0);
    if (content) {
        for (auto* contentChild : content->getChildren()) {
            if (auto* mod = dynamic_cast<ModuleComponent*>(contentChild)) {
                if (mod->getModule() == lfoNode->getProcessor())
                    lfoComp = mod;
                if (mod->getModule() == vcaNode->getProcessor())
                    vcaComp = mod;
            }
        }
    }

    ASSERT_NE(lfoComp, nullptr);
    ASSERT_NE(vcaComp, nullptr);

    lfoComp->setBounds(0, 0, 100, 100);
    vcaComp->setBounds(200, 0, 100, 100);

    editor.beginConnectionDrag(lfoComp, 0, false, false, juce::Point<int>(0, 0));
    editor.dragConnection(juce::Point<int>(50, 0));
    editor.endConnectionDrag(vcaComp->getBounds().getPosition() + vcaComp->getPortCenter(1, true));

    auto& graph = engine.getGraph();

    setPolyParam(*vcaNode->getProcessor(), true);

    for (int i = 0; i < 8; ++i) {
        bool found = false;
        for (auto& conn : graph.getConnections()) {
            if (conn.source.nodeID == lfoNode->nodeID && conn.source.channelIndex == 0 &&
                conn.destination.nodeID == vcaNode->nodeID && conn.destination.channelIndex == 8 + i) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "Missing broadcast connection for voice " << i << " after poly toggle";
    }

    bool foundAttenuverter = false;
    for (auto* node : graph.getNodes())
        if (dynamic_cast<AttenuverterModule*>(node->getProcessor()) != nullptr)
            foundAttenuverter = true;
    EXPECT_FALSE(foundAttenuverter) << "A poly fan is wired direct, so the attenuverter must be gone";
}

TEST_F(GraphEditorTest, TogglingPolyOnFansOutExistingConnection) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto oscNode = engine.getGraph().addNode(std::make_unique<OscillatorModule>());
    auto filterNode = engine.getGraph().addNode(std::make_unique<FilterModule>());

    editor.updateComponents();

    ModuleComponent* oscComp = nullptr;
    ModuleComponent* filterComp = nullptr;

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

    auto& graph = engine.getGraph();

    int monoCount = 0;
    for (auto& conn : graph.getConnections())
        if (conn.source.nodeID == oscNode->nodeID && conn.source.channelIndex == 0 &&
            conn.destination.nodeID == filterNode->nodeID && conn.destination.channelIndex == 0)
            ++monoCount;
    ASSERT_EQ(monoCount, 1) << "Setup must produce a single mono connection before toggling poly";

    // Toggling poly on both ends must re-anchor the mono wire onto the 8-voice fan.
    setPolyParam(*oscNode->getProcessor(), true);
    setPolyParam(*filterNode->getProcessor(), true);

    for (int i = 0; i < 8; ++i) {
        bool found = false;
        for (auto& conn : graph.getConnections()) {
            if (conn.source.nodeID == oscNode->nodeID && conn.source.channelIndex == i &&
                conn.destination.nodeID == filterNode->nodeID && conn.destination.channelIndex == i) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "Missing fan connection for voice " << i;
    }

    int fanCount = 0;
    for (auto& conn : graph.getConnections())
        if (conn.source.nodeID == oscNode->nodeID && conn.destination.nodeID == filterNode->nodeID)
            ++fanCount;
    EXPECT_EQ(fanCount, 8);
}

TEST_F(GraphEditorTest, TogglingPolyOffCollapsesFanToSingleConnection) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto oscNode = engine.getGraph().addNode(std::make_unique<OscillatorModule>());
    auto filterNode = engine.getGraph().addNode(std::make_unique<FilterModule>());

    editor.updateComponents();

    ModuleComponent* oscComp = nullptr;
    ModuleComponent* filterComp = nullptr;

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

    auto& graph = engine.getGraph();

    // Toggle poly ON for both to build the 8-voice fan.
    setPolyParam(*oscNode->getProcessor(), true);
    setPolyParam(*filterNode->getProcessor(), true);

    int fanCount = 0;
    for (auto& conn : graph.getConnections())
        if (conn.source.nodeID == oscNode->nodeID && conn.destination.nodeID == filterNode->nodeID)
            ++fanCount;
    ASSERT_EQ(fanCount, 8) << "Setup must produce the 8-voice fan before toggling poly off";

    // Toggle poly OFF for both — the fan must collapse back to a single mono wire.
    setPolyParam(*oscNode->getProcessor(), false);
    setPolyParam(*filterNode->getProcessor(), false);

    int monoCount = 0;
    for (auto& conn : graph.getConnections())
        if (conn.source.nodeID == oscNode->nodeID && conn.destination.nodeID == filterNode->nodeID)
            ++monoCount;
    EXPECT_EQ(monoCount, 1);

    bool foundMonoConn = false;
    for (auto& conn : graph.getConnections())
        if (conn.source.nodeID == oscNode->nodeID && conn.source.channelIndex == 0 &&
            conn.destination.nodeID == filterNode->nodeID && conn.destination.channelIndex == 0)
            foundMonoConn = true;
    EXPECT_TRUE(foundMonoConn);
}

TEST_F(GraphEditorTest, TogglingPolyMovesModCvWireOntoPolyChannels) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto adsrNode = engine.getGraph().addNode(std::make_unique<ADSRModule>());
    auto vcaNode = engine.getGraph().addNode(std::make_unique<VCAModule>());

    editor.updateComponents();

    ModuleComponent* adsrComp = nullptr;
    ModuleComponent* vcaComp = nullptr;

    auto* content = editor.getChildComponent(0);
    if (content) {
        for (auto* contentChild : content->getChildren()) {
            if (auto* mod = dynamic_cast<ModuleComponent*>(contentChild)) {
                if (mod->getModule() == adsrNode->getProcessor())
                    adsrComp = mod;
                if (mod->getModule() == vcaNode->getProcessor())
                    vcaComp = mod;
            }
        }
    }

    ASSERT_NE(adsrComp, nullptr);
    ASSERT_NE(vcaComp, nullptr);

    adsrComp->setBounds(0, 0, 100, 100);
    vcaComp->setBounds(200, 0, 100, 100);

    // Mono ADSR out jack0 -> mono VCA in jack1 (CV) creates an attenuverter chain.
    editor.beginConnectionDrag(adsrComp, 0, false, false, juce::Point<int>(0, 0));
    editor.dragConnection(juce::Point<int>(50, 0));

    auto vcaTargetPoint = vcaComp->getBounds().getPosition() + vcaComp->getPortCenter(1, true);
    editor.endConnectionDrag(vcaTargetPoint);

    auto& graph = engine.getGraph();

    bool foundAttenuverterBefore = false;
    for (auto* node : graph.getNodes())
        if (dynamic_cast<AttenuverterModule*>(node->getProcessor()) != nullptr)
            foundAttenuverterBefore = true;
    ASSERT_TRUE(foundAttenuverterBefore) << "Setup must create an attenuverter chain before toggling poly";

    // Toggling poly on both ends must collapse the attenuverter chain into a direct 8-voice fan
    // landing on raw channels 8-15 (VCA's poly CV bus), not the stale raw channel 1.
    setPolyParam(*adsrNode->getProcessor(), true);
    setPolyParam(*vcaNode->getProcessor(), true);

    bool foundAttenuverterAfter = false;
    for (auto* node : graph.getNodes())
        if (dynamic_cast<AttenuverterModule*>(node->getProcessor()) != nullptr)
            foundAttenuverterAfter = true;
    EXPECT_FALSE(foundAttenuverterAfter);

    for (int i = 0; i < 8; ++i) {
        bool found = false;
        for (auto& conn : graph.getConnections()) {
            if (conn.source.nodeID == adsrNode->nodeID && conn.source.channelIndex == i &&
                conn.destination.nodeID == vcaNode->nodeID && conn.destination.channelIndex == 8 + i) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "Missing direct fan connection for voice " << i;
    }

    int directCount = 0;
    for (auto& conn : graph.getConnections())
        if (conn.source.nodeID == adsrNode->nodeID && conn.destination.nodeID == vcaNode->nodeID)
            ++directCount;
    EXPECT_EQ(directCount, 8);
}
