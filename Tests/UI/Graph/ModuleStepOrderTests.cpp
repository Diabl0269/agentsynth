// Concern: FRO278's next/previous module selection -- the pure ordering rule in
// UI/Graph/ModuleStepOrder.h, and GraphEditor::selectAdjacentModule() driving it against real cards.
#include "AppUndoManager.h"
#include "Modules/FilterModule.h"
#include "Modules/LFOModule.h"
#include "Modules/OscillatorModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Graph/ModuleStepOrder.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

using NodeID = juce::AudioProcessorGraph::NodeID;
using synth::ui::StepModule;

namespace {

StepModule at(std::uint32_t uid, float x, float y) { return {NodeID{uid}, {x, y, 100.0f, 60.0f}}; }

NodeID addModuleAt(GraphEditor& editor, AudioEngine& engine, std::unique_ptr<juce::AudioProcessor> processor, int x,
                   int y) {
    auto node = engine.getGraph().addNode(std::move(processor));
    node->properties.set("x", x);
    node->properties.set("y", y);
    editor.updateComponents();
    return node->nodeID;
}

} // namespace

// ============================================================================
// Ordering rule
// ============================================================================

TEST(ModuleStepOrder, NoModulesYieldsNoTarget) {
    EXPECT_EQ(synth::ui::adjacentModule({}, {}, 1).uid, 0u);
    EXPECT_EQ(synth::ui::adjacentModule({}, {}, -1).uid, 0u);
}

TEST(ModuleStepOrder, NothingSelectedStartsAtTheFirstOrLastByPosition) {
    // Deliberately not in id order: 30 is leftmost, 10 rightmost.
    const std::vector<StepModule> mods{at(10, 900, 0), at(20, 400, 0), at(30, 0, 0)};
    EXPECT_EQ(synth::ui::adjacentModule(mods, {}, 1).uid, 30u);
    EXPECT_EQ(synth::ui::adjacentModule(mods, {}, -1).uid, 10u);
}

TEST(ModuleStepOrder, StepsLeftToRightAndClampsAtTheEnds) {
    const std::vector<StepModule> mods{at(10, 900, 0), at(20, 400, 0), at(30, 0, 0)};
    EXPECT_EQ(synth::ui::adjacentModule(mods, {NodeID{30}}, 1).uid, 20u);
    EXPECT_EQ(synth::ui::adjacentModule(mods, {NodeID{20}}, 1).uid, 10u);
    EXPECT_EQ(synth::ui::adjacentModule(mods, {NodeID{10}}, 1).uid, 10u) << "next parks on the last module";
    EXPECT_EQ(synth::ui::adjacentModule(mods, {NodeID{10}}, -1).uid, 20u);
    EXPECT_EQ(synth::ui::adjacentModule(mods, {NodeID{30}}, -1).uid, 30u) << "previous parks on the first";
}

TEST(ModuleStepOrder, SameColumnBreaksTiesTopToBottomThenById) {
    const std::vector<StepModule> mods{at(5, 0, 300), at(6, 0, 0), at(7, 0, 300)};
    EXPECT_EQ(synth::ui::adjacentModule(mods, {}, 1).uid, 6u);
    EXPECT_EQ(synth::ui::adjacentModule(mods, {NodeID{6}}, 1).uid, 5u);
    EXPECT_EQ(synth::ui::adjacentModule(mods, {NodeID{5}}, 1).uid, 7u);
}

TEST(ModuleStepOrder, AMultiSelectionStepsFromItsLastMemberForwardAndFirstBackward) {
    const std::vector<StepModule> mods{at(1, 0, 0), at(2, 300, 0), at(3, 600, 0), at(4, 900, 0)};
    const std::vector<NodeID> two{NodeID{2}, NodeID{3}};
    EXPECT_EQ(synth::ui::adjacentModule(mods, two, 1).uid, 4u);
    EXPECT_EQ(synth::ui::adjacentModule(mods, two, -1).uid, 1u);
}

TEST(ModuleStepOrder, AStaleSelectedIdIsTreatedAsNothingSelected) {
    const std::vector<StepModule> mods{at(1, 0, 0), at(2, 300, 0)};
    EXPECT_EQ(synth::ui::adjacentModule(mods, {NodeID{99}}, 1).uid, 1u);
}

// ============================================================================
// GraphEditor::selectAdjacentModule
// ============================================================================

TEST(GraphEditorSelectAdjacentModule, WalksTheCanvasLeftToRightSelectingOneModuleAtATime) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 900);
    const auto right = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 700, 0);
    const auto left = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 0, 0);
    const auto mid = addModuleAt(editor, engine, std::make_unique<LFOModule>(), 350, 0);

    ASSERT_TRUE(editor.selectAdjacentModule(1));
    EXPECT_EQ(editor.getSelectedNodes(), std::vector<NodeID>{left});
    ASSERT_TRUE(editor.selectAdjacentModule(1));
    EXPECT_EQ(editor.getSelectedNodes(), std::vector<NodeID>{mid});
    ASSERT_TRUE(editor.selectAdjacentModule(1));
    EXPECT_EQ(editor.getSelectedNodes(), std::vector<NodeID>{right});
    ASSERT_TRUE(editor.selectAdjacentModule(-1));
    EXPECT_EQ(editor.getSelectedNodes(), std::vector<NodeID>{mid});
}

TEST(GraphEditorSelectAdjacentModule, ReturnsFalseAndSelectsNothingOnAnEmptyCanvas) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 900);
    EXPECT_FALSE(editor.selectAdjacentModule(1));
    EXPECT_EQ(editor.getSelectionCount(), 0);
}

TEST(GraphEditorSelectAdjacentModule, HiddenCardsAreSkipped) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 900);
    const auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 0, 0);
    const auto hidden = addModuleAt(editor, engine, std::make_unique<LFOModule>(), 350, 0);
    const auto c = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 700, 0);
    for (auto* comp : editor.getModuleComponents())
        if (comp != nullptr && comp->getNodeId() == hidden)
            comp->setVisible(false); // what a collapsed macro does to its members

    editor.selectModule(a, false);
    ASSERT_TRUE(editor.selectAdjacentModule(1));
    EXPECT_EQ(editor.getSelectedNodes(), std::vector<NodeID>{c});
}

TEST(GraphEditorSelectAdjacentModule, PansAnOffScreenTargetIntoView) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(600, 400);
    const auto near = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 0, 0);
    const auto far = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 5000, 0);

    juce::Rectangle<float> farBounds;
    for (auto* comp : editor.getModuleComponents())
        if (comp != nullptr && comp->getNodeId() == far)
            farBounds = comp->getBounds().toFloat();

    editor.selectModule(near, false);
    ASSERT_FALSE(editor.getVisibleCanvasRect().contains(farBounds));
    ASSERT_TRUE(editor.selectAdjacentModule(1));
    ASSERT_EQ(editor.getSelectedNodes(), std::vector<NodeID>{far});
    EXPECT_TRUE(editor.getVisibleCanvasRect().contains(farBounds.getCentre()))
        << "the newly selected module must be visible; bounds " << farBounds.toString() << " view "
        << editor.getVisibleCanvasRect().toString();
}

TEST(GraphEditorSelectAdjacentModule, AnOnScreenTargetDoesNotMoveTheView) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 900);
    const auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 0, 0);
    addModuleAt(editor, engine, std::make_unique<FilterModule>(), 350, 0);

    editor.selectModule(a, false);
    const auto before = editor.getVisibleCanvasRect();
    ASSERT_TRUE(editor.selectAdjacentModule(1));
    EXPECT_EQ(editor.getVisibleCanvasRect(), before);
}
