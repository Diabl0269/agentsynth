// GraphEditorOnboardingTests.cpp
// gtest coverage for UI Phase 5 onboarding deliverables in GraphEditor:
//   1. isCanvasEmpty pure predicate
//   2. computeDropFinalPosition pure helper (snap + anti-overlap + idempotence)
//   3. Paint smoke tests (empty canvas and non-empty canvas — must not crash)
//
// Mirrors the setup of existing Tests/*.cpp (ScopedJuceInitialiser_GUI lives
// in TestMain.cpp and applies globally — no per-file setup needed).
// This file is NOT registered in CMakeLists.txt (per OWNER instructions).

#include "../Source/GravisynthUndoManager.h"
#include "../Source/Modules/OscillatorModule.h"
#include "../Source/UI/GraphEditor.h"
#include "../Source/UI/LayoutUtil.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

// ============================================================================
// 1. isCanvasEmpty — pure predicate
// ============================================================================

TEST(GraphEditorOnboarding, IsCanvasEmptyReturnsTrueForZero) { EXPECT_TRUE(GraphEditor::isCanvasEmpty(0)); }

TEST(GraphEditorOnboarding, IsCanvasEmptyReturnsTrueForNegative) {
    // Defensive: negative count treated as empty (guard against underflow callers).
    EXPECT_TRUE(GraphEditor::isCanvasEmpty(-1));
}

TEST(GraphEditorOnboarding, IsCanvasEmptyReturnsFalseForOne) { EXPECT_FALSE(GraphEditor::isCanvasEmpty(1)); }

TEST(GraphEditorOnboarding, IsCanvasEmptyReturnsFalseForMany) { EXPECT_FALSE(GraphEditor::isCanvasEmpty(5)); }

// ============================================================================
// 2. computeDropFinalPosition — snap + anti-overlap + idempotence
// ============================================================================

TEST(GraphEditorOnboarding, ComputeDropFinalPositionSnapsToGrid) {
    // Drop at a non-grid coordinate with no existing modules.
    const std::vector<gsynth::LayoutUtil::Box> empty;
    gsynth::LayoutUtil::NodeID nullId{};

    auto result = GraphEditor::computeDropFinalPosition({103, 97}, // neither multiple of kGridSize=8
                                                        280, 300, empty, nullId);

    EXPECT_EQ(result.x % gsynth::LayoutUtil::kGridSize, 0)
        << "x=" << result.x << " must be a multiple of kGridSize=" << gsynth::LayoutUtil::kGridSize;
    EXPECT_EQ(result.y % gsynth::LayoutUtil::kGridSize, 0)
        << "y=" << result.y << " must be a multiple of kGridSize=" << gsynth::LayoutUtil::kGridSize;
}

TEST(GraphEditorOnboarding, ComputeDropFinalPositionEmptyBoxesReturnSnapped) {
    // With no existing modules the result should equal snap(dropPoint).
    const std::vector<gsynth::LayoutUtil::Box> empty;
    gsynth::LayoutUtil::NodeID nullId{};

    const juce::Point<int> drop{200, 200};
    auto result = GraphEditor::computeDropFinalPosition(drop, 280, 300, empty, nullId);
    auto expected = gsynth::LayoutUtil::snap(drop);
    EXPECT_EQ(result, expected);
}

TEST(GraphEditorOnboarding, ComputeDropFinalPositionAvoidsExistingBox) {
    // Existing module occupies the snapped drop position.
    gsynth::LayoutUtil::NodeID existingId{1};
    gsynth::LayoutUtil::NodeID newId{}; // new module (no NodeID yet)

    juce::Rectangle<int> existingRect{200, 200, 280, 300};
    std::vector<gsynth::LayoutUtil::Box> boxes = {{existingId, existingRect}};

    auto result = GraphEditor::computeDropFinalPosition({200, 200}, 280, 300, boxes, newId);

    // Must not overlap the existing box (with collision gap).
    const int gap = gsynth::LayoutUtil::kCollisionGap;
    auto resultRect = juce::Rectangle<int>(result.x, result.y, 280, 300);
    EXPECT_FALSE(resultRect.expanded(gap / 2).intersects(existingRect.expanded(gap / 2)))
        << "Result (" << resultRect.toString() << ") overlaps existing box (" << existingRect.toString()
        << ") with gap=" << gap;
}

TEST(GraphEditorOnboarding, ComputeDropFinalPositionResultIsOnGrid) {
    // Even after collision resolution the result stays on-grid.
    gsynth::LayoutUtil::NodeID existingId{1};
    gsynth::LayoutUtil::NodeID newId{};

    juce::Rectangle<int> existingRect{200, 200, 280, 300};
    std::vector<gsynth::LayoutUtil::Box> boxes = {{existingId, existingRect}};

    auto result = GraphEditor::computeDropFinalPosition({203, 197}, 280, 300, boxes, newId);

    EXPECT_EQ(result.x % gsynth::LayoutUtil::kGridSize, 0) << "Post-collision x=" << result.x << " must stay on grid";
    EXPECT_EQ(result.y % gsynth::LayoutUtil::kGridSize, 0) << "Post-collision y=" << result.y << " must stay on grid";
}

TEST(GraphEditorOnboarding, ComputeDropFinalPositionIdempotent) {
    // Calling the function a second time with the result of the first call (adding the
    // first result as a placed box) must return the same position (the slot is clear).
    gsynth::LayoutUtil::NodeID idA{1};
    gsynth::LayoutUtil::NodeID idNew{};

    std::vector<gsynth::LayoutUtil::Box> emptyBoxes;
    auto firstResult = GraphEditor::computeDropFinalPosition({100, 100}, 280, 300, emptyBoxes, idNew);

    // Re-run: this time the module is already placed at firstResult (idNew box).
    // Use idNew as selfId so it is excluded from self-collision.
    std::vector<gsynth::LayoutUtil::Box> withSelf = {
        {idNew, juce::Rectangle<int>(firstResult.x, firstResult.y, 280, 300)}};
    auto secondResult = GraphEditor::computeDropFinalPosition(firstResult, 280, 300, withSelf, idNew);

    EXPECT_EQ(secondResult, firstResult) << "Re-placing an already-placed module should not move it;"
                                         << " firstResult=" << firstResult.toString()
                                         << " secondResult=" << secondResult.toString();
}

TEST(GraphEditorOnboarding, ComputeDropFinalPositionSelfIdExcludedFromCollision) {
    // The module being placed must be excluded from its own collision check.
    // If selfId's box is in existingBoxes at the exact drop position, the result
    // should still land at that position (it does not collide with itself).
    gsynth::LayoutUtil::NodeID selfId{42};
    juce::Point<int> snappedDrop = gsynth::LayoutUtil::snap({200, 200});
    juce::Rectangle<int> selfBox{snappedDrop.x, snappedDrop.y, 280, 300};
    std::vector<gsynth::LayoutUtil::Box> boxes = {{selfId, selfBox}};

    auto result = GraphEditor::computeDropFinalPosition(snappedDrop, 280, 300, boxes, selfId);

    EXPECT_EQ(result, snappedDrop) << "Self-collision must be excluded; module should land at its own position."
                                   << " Expected=" << snappedDrop.toString() << " Got=" << result.toString();
}

// ============================================================================
// 3. Paint smoke tests — exercise paint() without crashing.
//    Requires a running MessageManager (provided by ScopedJuceInitialiser_GUI
//    in TestMain.cpp).
// ============================================================================

TEST(GraphEditorOnboarding, PaintSmokeEmptyCanvas) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    // An empty GraphEditor (no modules) should call paint without crashing.
    // The empty-canvas hint path is exercised here.
    juce::Image img(juce::Image::ARGB, 800, 600, true);
    juce::Graphics g(img);
    EXPECT_NO_THROW(editor.paint(g));
    EXPECT_NO_THROW(editor.resized());
}

TEST(GraphEditorOnboarding, PaintSmokeNonEmptyCanvas) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    // Add a module so the canvas is non-empty (hint must disappear).
    auto oscNode = engine.getGraph().addNode(std::make_unique<OscillatorModule>());
    oscNode->properties.set("x", 100);
    oscNode->properties.set("y", 100);
    editor.updateComponents();

    juce::Image img(juce::Image::ARGB, 800, 600, true);
    juce::Graphics g(img);
    EXPECT_NO_THROW(editor.paint(g));
    EXPECT_NO_THROW(editor.resized());
}

TEST(GraphEditorOnboarding, PaintSmokeContentComponentEmpty) {
    // Directly paint the GraphContentComponent child with zero modules.
    // Exercises the background/wire drawing branch in GraphContentComponent::paint()
    // (the empty-canvas hint has moved to GraphEditor::paintOverChildren).
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    // The first child of GraphEditor is GraphContentComponent.
    auto* contentComp = editor.getChildComponent(0);
    ASSERT_NE(contentComp, nullptr) << "GraphEditor must have a GraphContentComponent child";
    contentComp->setSize(800, 600);

    juce::Image img(juce::Image::ARGB, 800, 600, true);
    juce::Graphics g(img);
    EXPECT_NO_THROW(contentComp->paint(g));
}

TEST(GraphEditorOnboarding, PaintSmokeContentComponentNonEmpty) {
    // Directly paint the GraphContentComponent child with one module.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto oscNode = engine.getGraph().addNode(std::make_unique<OscillatorModule>());
    oscNode->properties.set("x", 50);
    oscNode->properties.set("y", 50);
    editor.updateComponents();

    auto* contentComp = editor.getChildComponent(0);
    ASSERT_NE(contentComp, nullptr);
    contentComp->setSize(800, 600);

    juce::Image img(juce::Image::ARGB, 800, 600, true);
    juce::Graphics g(img);
    EXPECT_NO_THROW(contentComp->paint(g));
}

// ============================================================================
// 3b. paintOverChildren smoke tests — exercise the viewport-centred hint.
//     GraphEditor::paintOverChildren is called by JUCE during a full component
//     paint pass.  We trigger it via a full repaint into an offscreen image so
//     the hint code runs through the JUCE paint dispatch.
// ============================================================================

TEST(GraphEditorOnboarding, PaintOverChildrenSmokeEmptyCanvas) {
    // An empty editor (no modules) must exercise the paintOverChildren hint path
    // without crashing.  The hint is drawn centred in getLocalBounds().
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    // Trigger a full paint pass (parent + children + paintOverChildren).
    juce::Image img(juce::Image::ARGB, 800, 600, true);
    juce::Graphics g(img);
    EXPECT_NO_THROW(editor.paintEntireComponent(g, false));
}

TEST(GraphEditorOnboarding, PaintOverChildrenSmokeNonEmptyCanvas) {
    // A non-empty editor must NOT draw the hint — paintOverChildren returns early.
    // Must not crash either way.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto oscNode = engine.getGraph().addNode(std::make_unique<OscillatorModule>());
    oscNode->properties.set("x", 100);
    oscNode->properties.set("y", 100);
    editor.updateComponents();

    juce::Image img(juce::Image::ARGB, 800, 600, true);
    juce::Graphics g(img);
    EXPECT_NO_THROW(editor.paintEntireComponent(g, false));
}

// ============================================================================
// 4. newPatch() — clear canvas action (headless, no undoManager)
// ============================================================================

TEST(GraphEditorOnboarding, NewPatchClearsGraph) {
    // Construct engine + editor without an undoManager (headless path).
    AudioEngine engine;
    GraphEditor editor(engine); // undoManager = nullptr
    editor.setSize(800, 600);

    // Add one module so the canvas is non-empty.
    auto oscNode = engine.getGraph().addNode(std::make_unique<OscillatorModule>());
    oscNode->properties.set("x", 100);
    oscNode->properties.set("y", 100);
    editor.updateComponents();

    // Confirm non-empty before calling newPatch.
    const int nodesBefore = (int)engine.getGraph().getNodes().size();
    EXPECT_GT(nodesBefore, 0) << "Pre-condition: graph must have at least one node before newPatch";
    EXPECT_FALSE(GraphEditor::isCanvasEmpty(editor.getModuleComponents().size()))
        << "Pre-condition: canvas must be non-empty before newPatch";

    // Act.
    EXPECT_NO_THROW(editor.newPatch());

    // After newPatch the graph must have zero nodes.
    const int nodesAfter = (int)engine.getGraph().getNodes().size();
    EXPECT_EQ(nodesAfter, 0) << "newPatch must clear all graph nodes";

    // And the GraphEditor's module-component list must be empty (detachAllModuleComponents was called).
    EXPECT_EQ(editor.getModuleComponents().size(), 0) << "newPatch must detach all module components";

    // isCanvasEmpty must reflect the cleared state.
    EXPECT_TRUE(GraphEditor::isCanvasEmpty((int)editor.getModuleComponents().size()))
        << "isCanvasEmpty must return true after newPatch";
}

TEST(GraphEditorOnboarding, NewPatchOnEmptyCanvasIsNoop) {
    // Calling newPatch on an already-empty canvas must not crash.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    EXPECT_NO_THROW(editor.newPatch());
    EXPECT_EQ(engine.getGraph().getNodes().size(), 0u);
    EXPECT_TRUE(GraphEditor::isCanvasEmpty((int)editor.getModuleComponents().size()));
}

TEST(GraphEditorOnboarding, NewPatchWithUndoManagerIsUndoable) {
    // With an undoManager present, newPatch must be undoable (Cmd+Z restores the graph).
    AudioEngine engine;
    GravisynthUndoManager undoManager;
    GraphEditor editor(engine, &undoManager);
    undoManager.setGraphEditor(&editor);
    editor.setSize(800, 600);

    // Add a module so there is something to restore on undo.
    auto oscNode = engine.getGraph().addNode(std::make_unique<OscillatorModule>());
    oscNode->properties.set("x", 120);
    oscNode->properties.set("y", 120);
    editor.updateComponents();

    EXPECT_FALSE(GraphEditor::isCanvasEmpty((int)editor.getModuleComponents().size()));

    // Perform newPatch — must be recorded in the undo stack.
    editor.newPatch();
    EXPECT_EQ(engine.getGraph().getNodes().size(), 0u) << "Canvas must be empty after newPatch";
    EXPECT_TRUE(undoManager.canUndo()) << "newPatch must push an undoable action when undoManager is present";

    // Undo must restore the graph to its prior non-empty state.
    undoManager.undo();
    // After undo the graph has nodes again — call updateComponents to reconcile the editor.
    editor.updateComponents();
    EXPECT_FALSE(GraphEditor::isCanvasEmpty((int)editor.getModuleComponents().size()))
        << "Undo of newPatch must restore prior module components";
}
