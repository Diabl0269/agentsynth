// MultiSelectTests.cpp
// GraphEditor-level tests for multi-select, group drag and snippet drop — issue #156.
//
//   • selection API      — single/additive select, select-all, clear, prune after node removal
//   • marquee            — replaces vs adds, canvas-coordinate hit testing, degenerate band
//   • group drag         — every selected module moves by the same delta, relative layout survives
//   • delete selection   — removes the whole group as ONE undoable change
//   • snippet drop       — a library snippet payload inserts a group and leaves it selected

#include "../Source/AppUndoManager.h"
#include "../Source/Modules/FilterModule.h"
#include "../Source/Modules/LFOModule.h"
#include "../Source/Modules/OscillatorModule.h"
#include "../Source/Modules/VCAModule.h"
#include "../Source/SnippetManager.h"
#include "../Source/UI/GraphEditor.h"
#include "../Source/UI/ModuleComponent.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

using NodeID = juce::AudioProcessorGraph::NodeID;

namespace {

class DummyDragSource : public juce::Component {};

/** Adds a module and lays out its ModuleComponent at a known position. */
NodeID addModuleAt(GraphEditor& editor, AudioEngine& engine, std::unique_ptr<juce::AudioProcessor> processor, int x,
                   int y) {
    auto node = engine.getGraph().addNode(std::move(processor));
    node->properties.set("x", x);
    node->properties.set("y", y);
    editor.updateComponents();
    return node->nodeID;
}

ModuleComponent* findComponent(GraphEditor& editor, NodeID id) {
    for (auto* comp : editor.getModuleComponents())
        if (comp != nullptr && comp->getNodeId() == id)
            return comp;
    return nullptr;
}

juce::Rectangle<int> boundsOf(GraphEditor& editor, NodeID id) {
    auto* comp = findComponent(editor, id);
    return comp != nullptr ? comp->getBounds() : juce::Rectangle<int>();
}

bool selectionContains(GraphEditor& editor, NodeID id) { return editor.isNodeSelected(id); }

} // namespace

// ============================================================================
// Selection API
// ============================================================================

TEST(MultiSelectSelection, StartsWithNothingSelected) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 900);

    EXPECT_EQ(editor.getSelectionCount(), 0);
}

TEST(MultiSelectSelection, SelectModuleReplacesByDefaultAndTogglesWhenAdditive) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 900);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 0, 0);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 600, 0);

    editor.selectModule(a, false);
    EXPECT_EQ(editor.getSelectionCount(), 1);
    EXPECT_TRUE(selectionContains(editor, a));

    // Non-additive select collapses onto the new module.
    editor.selectModule(b, false);
    EXPECT_EQ(editor.getSelectionCount(), 1);
    EXPECT_TRUE(selectionContains(editor, b));
    EXPECT_FALSE(selectionContains(editor, a));

    // Additive select adds, then toggles back off.
    editor.selectModule(a, true);
    EXPECT_EQ(editor.getSelectionCount(), 2);
    editor.selectModule(a, true);
    EXPECT_EQ(editor.getSelectionCount(), 1);
    EXPECT_TRUE(selectionContains(editor, b));
}

TEST(MultiSelectSelection, SelectAllPicksUpEveryRenderedModule) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 900);

    addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 0, 0);
    addModuleAt(editor, engine, std::make_unique<FilterModule>(), 600, 0);
    addModuleAt(editor, engine, std::make_unique<VCAModule>(), 0, 600);

    editor.selectAllModules();
    EXPECT_EQ(editor.getSelectionCount(), (int)editor.getModuleComponents().size());
    EXPECT_GE(editor.getSelectionCount(), 3);
}

TEST(MultiSelectSelection, ClearSelectionEmptiesIt) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 900);

    addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 0, 0);
    editor.selectAllModules();
    ASSERT_GT(editor.getSelectionCount(), 0);

    editor.clearSelection();
    EXPECT_EQ(editor.getSelectionCount(), 0);
}

TEST(MultiSelectSelection, PruneDropsIdsWhoseNodesWereRemoved) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 900);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 0, 0);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 600, 0);
    editor.setSelectedNodes({a, b});
    ASSERT_EQ(editor.getSelectionCount(), 2);

    // Remove one node behind the editor's back (mirrors an undo/preset load).
    engine.getGraph().removeNode(a);
    editor.updateComponents(); // reconciles, and must prune the stale id

    EXPECT_EQ(editor.getSelectionCount(), 1);
    EXPECT_FALSE(selectionContains(editor, a));
    EXPECT_TRUE(selectionContains(editor, b));
}

// ============================================================================
// Marquee
// ============================================================================

TEST(MultiSelectMarquee, BandSelectsEveryModuleItTouches) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<VCAModule>(), 500, 100);
    auto far = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 3000, 3000);

    auto boundsA = boundsOf(editor, a);
    auto boundsB = boundsOf(editor, b);
    auto band = boundsA.getUnion(boundsB);

    editor.beginMarquee(band.getTopLeft(), /*additive=*/false);
    EXPECT_TRUE(editor.isMarqueeActive());
    editor.updateMarquee(band.getBottomRight());

    EXPECT_TRUE(selectionContains(editor, a));
    EXPECT_TRUE(selectionContains(editor, b));
    EXPECT_FALSE(selectionContains(editor, far));

    editor.endMarquee();
    EXPECT_FALSE(editor.isMarqueeActive());
    EXPECT_TRUE(editor.getMarqueeRect().isEmpty());
    EXPECT_EQ(editor.getSelectionCount(), 2) << "ending the marquee keeps what it selected";
}

TEST(MultiSelectMarquee, NonAdditiveMarqueeReplacesThePreviousSelection) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<VCAModule>(), 900, 100);

    editor.selectModule(a, false);
    ASSERT_TRUE(selectionContains(editor, a));

    auto boundsB = boundsOf(editor, b);
    editor.beginMarquee(boundsB.getTopLeft(), false);
    editor.updateMarquee(boundsB.getBottomRight());
    editor.endMarquee();

    EXPECT_TRUE(selectionContains(editor, b));
    EXPECT_FALSE(selectionContains(editor, a)) << "a plain marquee replaces rather than adds";
}

TEST(MultiSelectMarquee, AdditiveMarqueeKeepsThePreviousSelection) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<VCAModule>(), 900, 100);

    editor.selectModule(a, false);

    auto boundsB = boundsOf(editor, b);
    editor.beginMarquee(boundsB.getTopLeft(), /*additive=*/true);
    editor.updateMarquee(boundsB.getBottomRight());
    editor.endMarquee();

    EXPECT_TRUE(selectionContains(editor, a));
    EXPECT_TRUE(selectionContains(editor, b));
    EXPECT_EQ(editor.getSelectionCount(), 2);
}

TEST(MultiSelectMarquee, MarqueeOverEmptyCanvasDeselectsEverything) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    editor.selectModule(a, false);
    ASSERT_EQ(editor.getSelectionCount(), 1);

    // Band far away from any module.
    editor.beginMarquee({4000, 4000}, false);
    editor.updateMarquee({4200, 4200});
    editor.endMarquee();

    EXPECT_EQ(editor.getSelectionCount(), 0);
}

TEST(MultiSelectMarquee, ShrinkingTheBandDeselectsWhatItNoLongerCovers) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<VCAModule>(), 700, 100);

    auto boundsA = boundsOf(editor, a);
    auto boundsB = boundsOf(editor, b);

    editor.beginMarquee(boundsA.getTopLeft(), false);
    editor.updateMarquee(boundsB.getBottomRight());
    ASSERT_EQ(editor.getSelectionCount(), 2);

    // Drag back so only the first module is covered.
    editor.updateMarquee(boundsA.getBottomRight());
    EXPECT_TRUE(selectionContains(editor, a));
    EXPECT_FALSE(selectionContains(editor, b));
    editor.endMarquee();
}

TEST(MultiSelectMarquee, UpdateWithoutBeginIsANoOp) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 900);
    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 0, 0);

    EXPECT_NO_THROW(editor.updateMarquee({500, 500}));
    EXPECT_NO_THROW(editor.endMarquee());
    EXPECT_FALSE(selectionContains(editor, a));
}

// A collapsed macro's hidden members are not on the canvas as far as marquee hit-testing is
// concerned — collectModuleBoxes() skips !isVisible() components (P8-12). Clicking the visible
// card is the only way to select a collapsed macro; a marquee over the original footprint must
// pick up nothing.
TEST(MultiSelectMarquee, CollapsedMacroMembersAreNotMarqueeSelectable) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<VCAModule>(), 500, 100);
    // groupSelectionIntoMacro() groups by persistent uuid, which this file's addModuleAt() (unlike
    // MacroContainerTests.cpp's own helper) does not assign — set one here so grouping succeeds.
    engine.getGraph().getNodeForId(a)->properties.set("uuid", juce::Uuid().toDashedString());
    engine.getGraph().getNodeForId(b)->properties.set("uuid", juce::Uuid().toDashedString());

    auto boundsA = boundsOf(editor, a);
    auto boundsB = boundsOf(editor, b);
    auto band = boundsA.getUnion(boundsB);

    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());
    editor.setMacroCollapsed(macroId, true);

    editor.beginMarquee(band.getTopLeft(), /*additive=*/false);
    editor.updateMarquee(band.getBottomRight());
    editor.endMarquee();

    EXPECT_EQ(editor.getSelectionCount(), 0);
}

// ============================================================================
// Group drag
// ============================================================================

TEST(MultiSelectGroupDrag, MovesEverySelectedModuleByTheSameDelta) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<VCAModule>(), 700, 100);
    editor.setSelectedNodes({a, b});

    auto* compA = findComponent(editor, a);
    auto* compB = findComponent(editor, b);
    ASSERT_NE(compA, nullptr);
    ASSERT_NE(compB, nullptr);
    const auto startA = compA->getPosition();
    const auto startB = compB->getPosition();

    editor.beginSelectionDrag();
    EXPECT_TRUE(editor.isSelectionDragActive());

    // compA is the initiator (its own ComponentDragger already moved it), so simulate that.
    const juce::Point<int> delta(120, 64);
    compA->setTopLeftPosition(startA + delta);
    editor.dragSelectionBy(delta, compA);

    EXPECT_EQ(compA->getPosition(), startA + delta);
    EXPECT_EQ(compB->getPosition(), startB + delta) << "followers move by the initiator's delta";
}

TEST(MultiSelectGroupDrag, FinalizePreservesRelativeLayout) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<VCAModule>(), 700, 140);
    editor.setSelectedNodes({a, b});

    auto* compA = findComponent(editor, a);
    auto* compB = findComponent(editor, b);
    ASSERT_NE(compA, nullptr);
    ASSERT_NE(compB, nullptr);
    const auto relativeBefore = compB->getPosition() - compA->getPosition();

    editor.beginSelectionDrag();
    const juce::Point<int> delta(133, 77);
    compA->setTopLeftPosition(compA->getPosition() + delta);
    editor.dragSelectionBy(delta, compA);
    editor.finalizeSelectionDrag();

    EXPECT_FALSE(editor.isSelectionDragActive());
    EXPECT_EQ(compB->getPosition() - compA->getPosition(), relativeBefore)
        << "the group must resolve as one rigid body, not per-module";
}

TEST(MultiSelectGroupDrag, FinalizeSnapsTheGroupToTheGrid) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<VCAModule>(), 700, 100);
    editor.setSelectedNodes({a, b});

    auto* compA = findComponent(editor, a);
    ASSERT_NE(compA, nullptr);

    editor.beginSelectionDrag();
    const juce::Point<int> delta(37, 43); // deliberately off-grid
    compA->setTopLeftPosition(compA->getPosition() + delta);
    editor.dragSelectionBy(delta, compA);
    editor.finalizeSelectionDrag();

    // The group's bounding-box origin lands on the layout grid.
    juce::Rectangle<int> group;
    for (auto id : std::vector<NodeID>{a, b}) {
        auto bounds = boundsOf(editor, id);
        group = group.isEmpty() ? bounds : group.getUnion(bounds);
    }
    EXPECT_EQ(group.getX() % synth::LayoutUtil::kGridSize, 0);
    EXPECT_EQ(group.getY() % synth::LayoutUtil::kGridSize, 0);
}

TEST(MultiSelectGroupDrag, SingleSelectionDoesNotEngageGroupDrag) {
    // With one module selected the normal single-module path must still own the drag.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 900);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    editor.selectModule(a, false);

    editor.beginSelectionDrag();
    EXPECT_FALSE(editor.isSelectionDragActive());
}

TEST(MultiSelectGroupDrag, CancelLeavesPositionsUntouched) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 103, 107);
    auto b = addModuleAt(editor, engine, std::make_unique<VCAModule>(), 703, 107);
    editor.setSelectedNodes({a, b});

    const auto beforeA = boundsOf(editor, a).getTopLeft();
    const auto beforeB = boundsOf(editor, b).getTopLeft();

    editor.beginSelectionDrag();
    editor.cancelSelectionDrag();

    EXPECT_FALSE(editor.isSelectionDragActive());
    EXPECT_EQ(boundsOf(editor, a).getTopLeft(), beforeA) << "a click that never moved must not nudge the group";
    EXPECT_EQ(boundsOf(editor, b).getTopLeft(), beforeB);
}

TEST(MultiSelectGroupDrag, PersistsFollowerPositionsToGraphProperties) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<VCAModule>(), 700, 100);
    editor.setSelectedNodes({a, b});

    auto* compA = findComponent(editor, a);
    ASSERT_NE(compA, nullptr);

    editor.beginSelectionDrag();
    const juce::Point<int> delta(160, 80);
    compA->setTopLeftPosition(compA->getPosition() + delta);
    editor.dragSelectionBy(delta, compA);
    editor.finalizeSelectionDrag();

    // Positions must survive a save/reload, so they have to reach the node properties.
    auto* nodeB = engine.getGraph().getNodeForId(b);
    ASSERT_NE(nodeB, nullptr);
    EXPECT_EQ((int)nodeB->properties["x"], boundsOf(editor, b).getX());
    EXPECT_EQ((int)nodeB->properties["y"], boundsOf(editor, b).getY());
}

// ============================================================================
// Delete selection
// ============================================================================

TEST(MultiSelectDelete, RemovesEverySelectedModule) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 900);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 0, 0);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 600, 0);
    auto keep = addModuleAt(editor, engine, std::make_unique<VCAModule>(), 0, 600);

    editor.setSelectedNodes({a, b});
    editor.deleteSelection();

    EXPECT_EQ(engine.getGraph().getNodeForId(a), nullptr);
    EXPECT_EQ(engine.getGraph().getNodeForId(b), nullptr);
    EXPECT_NE(engine.getGraph().getNodeForId(keep), nullptr);
    EXPECT_EQ(editor.getSelectionCount(), 0);
}

TEST(MultiSelectDelete, IsOneUndoableChangeForTheWholeGroup) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1200, 900);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 0, 0);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 600, 0);
    const int nodesBefore = engine.getGraph().getNumNodes();

    editor.setSelectedNodes({a, b});
    editor.deleteSelection();
    ASSERT_EQ(engine.getGraph().getNumNodes(), nodesBefore - 2);

    ASSERT_TRUE(undo.canUndo());
    undo.undo();

    EXPECT_EQ(engine.getGraph().getNumNodes(), nodesBefore) << "one Cmd+Z must restore the entire group";
}

TEST(MultiSelectDelete, WithNothingSelectedIsANoOp) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 900);

    addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 0, 0);
    const int before = engine.getGraph().getNumNodes();

    EXPECT_NO_THROW(editor.deleteSelection());
    EXPECT_EQ(engine.getGraph().getNumNodes(), before);
}

// ============================================================================
// Snippets through the editor
// ============================================================================

TEST(MultiSelectSnippet, ExtractsTheCurrentSelection) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 900);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    addModuleAt(editor, engine, std::make_unique<VCAModule>(), 900, 100);

    editor.setSelectedNodes({a, b});
    auto snippet = editor.extractSelectionSnippet("Pair");

    EXPECT_EQ(synth::SnippetManager::getSnippetName(snippet), "Pair");
    EXPECT_EQ(synth::SnippetManager::getModuleCount(snippet), 2);
}

TEST(MultiSelectSnippet, InsertAtLeavesTheNewGroupSelected) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    editor.setSelectedNodes({a, b});
    auto snippet = editor.extractSelectionSnippet("Pair");

    const int before = engine.getGraph().getNumNodes();
    ASSERT_TRUE(editor.insertSnippetAt(snippet, {1200, 700}));

    EXPECT_EQ(engine.getGraph().getNumNodes(), before + 2);
    EXPECT_EQ(editor.getSelectionCount(), 2);
    // The freshly inserted copies are selected, not the originals.
    EXPECT_FALSE(selectionContains(editor, a));
    EXPECT_FALSE(selectionContains(editor, b));
}

TEST(MultiSelectSnippet, InsertIsUndoableAsOneChange) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    editor.setSelectedNodes({a, b});
    auto snippet = editor.extractSelectionSnippet("Pair");

    const int before = engine.getGraph().getNumNodes();
    ASSERT_TRUE(editor.insertSnippetAt(snippet, {1200, 700}));
    ASSERT_EQ(engine.getGraph().getNumNodes(), before + 2);

    ASSERT_TRUE(undo.canUndo());
    undo.undo();
    EXPECT_EQ(engine.getGraph().getNumNodes(), before);
}

TEST(MultiSelectSnippet, InsertSnapsTheDropPointToTheGrid) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    editor.setSelectedNodes({a});
    auto snippet = editor.extractSelectionSnippet("One");

    ASSERT_TRUE(editor.insertSnippetAt(snippet, {1003, 707})); // off-grid drop
    auto added = editor.getSelectedNodes();
    ASSERT_EQ(added.size(), 1u);

    auto* node = engine.getGraph().getNodeForId(added[0]);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ((int)node->properties["x"] % synth::LayoutUtil::kGridSize, 0);
    EXPECT_EQ((int)node->properties["y"] % synth::LayoutUtil::kGridSize, 0);
}

TEST(MultiSelectSnippet, EmptySnippetInsertReportsFailure) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 900);

    auto snippet = editor.extractSelectionSnippet("Nothing"); // nothing selected
    EXPECT_FALSE(editor.insertSnippetAt(snippet, {100, 100}));
}

TEST(MultiSelectSnippet, DroppingASnippetPayloadFromTheLibraryInsertsTheGroup) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    editor.setSelectedNodes({a, b});
    auto snippet = editor.extractSelectionSnippet("Lead");

    // MainComponent normally resolves the payload from disk; here we serve it in-memory.
    editor.snippetProvider = [snippet](const juce::String& name) -> juce::var {
        return name == "Lead" ? snippet : juce::var();
    };

    DummyDragSource dragSource;
    juce::var payload(synth::SnippetManager::payloadForName("Lead"));
    juce::DragAndDropTarget::SourceDetails details(payload, &dragSource, juce::Point<int>(900, 700));

    const int before = engine.getGraph().getNumNodes();
    editor.itemDragEnter(details); // must size the ghost from the group, not one module
    EXPECT_TRUE(editor.isDragPreviewActive());
    editor.itemDropped(details);

    EXPECT_EQ(engine.getGraph().getNumNodes(), before + 2);
    EXPECT_EQ(editor.getSelectionCount(), 2);
}

TEST(MultiSelectSnippet, UnknownSnippetPayloadDropsNothing) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 900);

    editor.snippetProvider = [](const juce::String&) -> juce::var { return {}; };

    DummyDragSource dragSource;
    juce::var payload(synth::SnippetManager::payloadForName("Missing"));
    juce::DragAndDropTarget::SourceDetails details(payload, &dragSource, juce::Point<int>(200, 200));

    const int before = engine.getGraph().getNumNodes();
    EXPECT_NO_THROW(editor.itemDropped(details));
    EXPECT_EQ(engine.getGraph().getNumNodes(), before);
}

TEST(MultiSelectSnippet, PlainModuleDropStillWorksAlongsideSnippetPayloads) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 900);

    DummyDragSource dragSource;
    juce::var payload("Oscillator");
    juce::DragAndDropTarget::SourceDetails details(payload, &dragSource, juce::Point<int>(200, 200));

    const int before = engine.getGraph().getNumNodes();
    editor.itemDropped(details);
    EXPECT_EQ(engine.getGraph().getNumNodes(), before + 1);
}

// ============================================================================
// Canvas keys
// ============================================================================

TEST(MultiSelectKeys, EscapeClearsTheSelectionAndIsOnlyConsumedWhenItDoesSomething) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 900);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 0, 0);

    EXPECT_FALSE(editor.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)))
        << "with nothing selected the key must fall through to other handlers";

    editor.selectModule(a, false);
    EXPECT_TRUE(editor.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_EQ(editor.getSelectionCount(), 0);
}

TEST(MultiSelectKeys, DeleteAndBackspaceRemoveTheSelection) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 900);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 0, 0);
    editor.selectModule(a, false);
    EXPECT_TRUE(editor.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)));
    EXPECT_EQ(engine.getGraph().getNodeForId(a), nullptr);

    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 0, 0);
    editor.selectModule(b, false);
    EXPECT_TRUE(editor.keyPressed(juce::KeyPress(juce::KeyPress::backspaceKey)));
    EXPECT_EQ(engine.getGraph().getNodeForId(b), nullptr);
}

TEST(MultiSelectKeys, DeleteWithNothingSelectedIsNotConsumed) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 900);

    EXPECT_FALSE(editor.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)));
}
