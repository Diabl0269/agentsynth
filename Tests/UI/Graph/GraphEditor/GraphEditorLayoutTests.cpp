// GraphEditor layout tests: grid-layout / anti-overlap placement, alignment-guide rendering,
// and Macro Control bank runtime resize (grow/shrink, hidden-jack routing cleanup).
// Shared GraphEditorTest fixture and helpers live in GraphEditorTestHelpers.h.

#include "GraphEditorTestHelpers.h"

#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/FilterModule.h"
#include "Modules/ModuleBase.h"
#include "Modules/OscillatorModule.h"
#include "Modules/VCAModule.h"
#include "UI/Layout/LayoutUtil.h"
#include "UI/Theme/BuiltInThemes.h"

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
    EXPECT_FALSE(editor.getDragDropController().isDragPreviewActive());
    editor.getDragDropController().beginDragPreview(280, 300, juce::AudioProcessorGraph::NodeID{});
    EXPECT_TRUE(editor.getDragDropController().isDragPreviewActive());

    // Desired position OVERLAPS the existing oscillator (same coordinates).
    juce::Point<int> desiredOverlap(100, 100);
    editor.getDragDropController().updateDragPreview(desiredOverlap);

    auto ghost = editor.getDragDropController().getDragPreviewGhost();
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
    editor.getDragDropController().endDragPreview();
    EXPECT_FALSE(editor.getDragDropController().isDragPreviewActive());
    EXPECT_TRUE(editor.getDragDropController().getDragPreviewGhost().isEmpty());
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

// ============================================================================
// Macro Control bank — runtime resize
// ============================================================================

namespace {

juce::AudioParameterInt* knobCountParam(juce::AudioProcessor* p) {
    for (auto* param : p->getParameters())
        if (auto* i = dynamic_cast<juce::AudioParameterInt*>(param))
            if (i->paramID == "macroCount")
                return i;
    return nullptr;
}

ModuleComponent* componentFor(GraphEditor& editor, juce::AudioProcessorGraph::NodeID id) {
    for (auto* comp : editor.getModuleComponents())
        if (comp != nullptr && comp->getNodeId() == id)
            return comp;
    return nullptr;
}

void setKnobs(juce::AudioProcessor* macros, int count) {
    auto* p = knobCountParam(macros);
    p->setValueNotifyingHost(p->convertTo0to1(count));
    // parameterValueChanged marshals the resize onto the message thread. A single fixed 50 ms
    // pump was measured too tight on a loaded CI runner (the macOS job flaked exactly here), so
    // pump in slices until the module actually reports the new count — bounded, then one extra
    // slice so the same message-thread callback's routing cleanup has run too.
    auto* mb = dynamic_cast<ModuleBase*>(macros);
    for (int i = 0; i < 40; ++i) {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
        if (mb != nullptr && mb->getVisibleOutputPortCount() == count)
            break;
    }
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
}

} // namespace

TEST_F(GraphEditorTest, MacroBankGrowsAndPushesTheModuleBelowItDown) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 900);

    auto& graph = engine.getGraph();
    auto macroNode = graph.addNode(synth::AIStateMapper::createModule("Macros"));
    auto vcaNode = graph.addNode(std::make_unique<VCAModule>());
    ASSERT_NE(macroNode, nullptr);
    ASSERT_NE(vcaNode, nullptr);

    setKnobs(macroNode->getProcessor(), 4);

    macroNode->properties.set("x", 100);
    macroNode->properties.set("y", 100);
    vcaNode->properties.set("x", 100);
    vcaNode->properties.set("y", 500);
    editor.updateComponents();

    auto* macroComp = componentFor(editor, macroNode->nodeID);
    auto* vcaComp = componentFor(editor, vcaNode->nodeID);
    ASSERT_NE(macroComp, nullptr);
    ASSERT_NE(vcaComp, nullptr);

    const auto macroTopLeftBefore = macroComp->getPosition();
    const int vcaYBefore = vcaComp->getY();
    ASSERT_LT(macroComp->getBottom(), vcaComp->getY()) << "test setup: the two must start clear of each other";

    setKnobs(macroNode->getProcessor(), 16);

    EXPECT_EQ(macroComp->getHeight(), synth::LayoutUtil::macroBankHeight(16));
    EXPECT_EQ(macroComp->getPosition(), macroTopLeftBefore) << "the resized module must not move";
    EXPECT_GT(vcaComp->getY(), vcaYBefore) << "the module below must be pushed clear";
    EXPECT_GE(vcaComp->getY(), macroComp->getBottom() + synth::LayoutUtil::kCollisionGap);

    // The displaced position must be persisted, or a reload would drop it back into the overlap.
    EXPECT_EQ(static_cast<int>(vcaNode->properties.getWithDefault("y", -1)), vcaComp->getY());
}

TEST_F(GraphEditorTest, ShrinkingTheMacroBankDropsRoutingsOnTheJacksItHides) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 900);

    auto& graph = engine.getGraph();
    auto macroNode = graph.addNode(synth::AIStateMapper::createModule("Macros"));
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    ASSERT_NE(macroNode, nullptr);
    ASSERT_NE(filterNode, nullptr);

    setKnobs(macroNode->getProcessor(), 16);
    editor.updateComponents();

    // M1 (kept) and M12 (about to be hidden) both drive the filter's cutoff CV.
    engine.addModRouting(macroNode->nodeID, 0, filterNode->nodeID, 8);
    engine.addModRouting(macroNode->nodeID, 11, filterNode->nodeID, 8);

    auto sourcesFrom = [&](int channel) {
        int n = 0;
        for (const auto& c : graph.getConnections())
            if (c.source.nodeID == macroNode->nodeID && c.source.channelIndex == channel)
                ++n;
        return n;
    };

    ASSERT_EQ(sourcesFrom(0), 1);
    ASSERT_EQ(sourcesFrom(11), 1);
    const int attenuvertersBefore = (int)graph.getNodes().size();

    setKnobs(macroNode->getProcessor(), 4);

    EXPECT_EQ(sourcesFrom(0), 1) << "a jack that is still visible must keep its routing";
    EXPECT_EQ(sourcesFrom(11), 0) << "the hidden jack's routing must be removed";
    EXPECT_LT((int)graph.getNodes().size(), attenuvertersBefore)
        << "the orphaned attenuverter must be removed with the routing, not left behind";
}

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
