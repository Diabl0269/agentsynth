// Concern: moving between canvas cards from the keyboard -- the pure direction rule in
// UI/Graph/CardNavigation.h, and CanvasCardKeyboard driving it (and the one-grid-step move)
// against real cards.
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/FilterModule.h"
#include "Modules/LFOModule.h"
#include "Modules/OscillatorModule.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Graph/CanvasCardKeyboard/CanvasCardKeyboard.h"
#include "UI/Graph/CardNavigation.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include <gtest/gtest.h>

using NodeID = juce::AudioProcessorGraph::NodeID;
using synth::ui::CardDirection;
using synth::ui::nearestCardInDirection;
using synth::ui::StepModule;

namespace {

StepModule card(std::uint32_t uid, float x, float y) { return {NodeID{uid}, {x, y, 100.0f, 60.0f}}; }

const juce::KeyPress kRight(juce::KeyPress::rightKey);
const juce::KeyPress kDown(juce::KeyPress::downKey);
const juce::KeyPress kAltRight(juce::KeyPress::rightKey, juce::ModifierKeys::altModifier, 0);

struct Canvas {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};

    Canvas() {
        undo.setGraphEditor(&editor);
        editor.setSize(1600, 1200);
    }

    NodeID add(std::unique_ptr<juce::AudioProcessor> processor, int x, int y) {
        auto node = engine.getGraph().addNode(std::move(processor));
        node->properties.set("x", x);
        node->properties.set("y", y);
        editor.updateComponents();
        return node->nodeID;
    }

    // By module name: an undo restores the graph from a snapshot, which need not keep node ids.
    int savedX(const juce::String& moduleName) {
        for (auto* node : engine.getGraph().getNodes())
            if (node->getProcessor()->getName() == moduleName)
                return (int)node->properties["x"];
        ADD_FAILURE() << "no " << moduleName << " node";
        return -1;
    }

    ModuleComponent* cardFor(NodeID id) {
        for (auto* c : editor.getModuleComponents())
            if (c != nullptr && c->getNodeId() == id)
                return c;
        return nullptr;
    }
};

} // namespace

// ============================================================================
// The direction rule
// ============================================================================

TEST(CardNavigation, PrefersTheCardOnTheArrowsAxisOverACloserOneOffToTheSide) {
    // From (0,0): B is 300 right and level; C is 200 right but 150 down. B scores 300, C 200+300.
    const std::vector<StepModule> cards{card(1, 0, 0), card(2, 300, 0), card(3, 200, 150)};
    EXPECT_EQ(nearestCardInDirection(cards, NodeID{1}, CardDirection::Right), NodeID{2});
    EXPECT_EQ(nearestCardInDirection(cards, NodeID{1}, CardDirection::Down), NodeID{3});
    // From B going left: A is 300 away and level (300), C is 100 left but 150 down (100 + 300).
    EXPECT_EQ(nearestCardInDirection(cards, NodeID{2}, CardDirection::Left), NodeID{1});
}

TEST(CardNavigation, NoCardInThatDirectionMeansNoMove) {
    const std::vector<StepModule> cards{card(1, 0, 0), card(2, 300, 0)};
    EXPECT_EQ(nearestCardInDirection(cards, NodeID{1}, CardDirection::Left).uid, 0u);
    EXPECT_EQ(nearestCardInDirection(cards, NodeID{1}, CardDirection::Up).uid, 0u);
    EXPECT_EQ(nearestCardInDirection(cards, NodeID{2}, CardDirection::Right).uid, 0u);
}

TEST(CardNavigation, NothingSelectedPicksTheFirstCardInStepOrderWhateverTheDirection) {
    const std::vector<StepModule> cards{card(5, 600, 0), card(6, 0, 400), card(7, 300, 0)};
    for (auto d : {CardDirection::Left, CardDirection::Right, CardDirection::Up, CardDirection::Down})
        EXPECT_EQ(nearestCardInDirection(cards, NodeID{}, d), NodeID{6});
    EXPECT_EQ(nearestCardInDirection({}, NodeID{}, CardDirection::Right).uid, 0u);
}

TEST(CardNavigation, AStaleSelectionIsTreatedAsNothingSelected) {
    const std::vector<StepModule> cards{card(1, 0, 0), card(2, 300, 0)};
    EXPECT_EQ(nearestCardInDirection(cards, NodeID{99}, CardDirection::Left), NodeID{1});
}

// ============================================================================
// On the canvas
// ============================================================================

TEST(CanvasCardKeyboard, AnArrowMovesTheSelectionToTheNearestCard) {
    Canvas c;
    const auto osc = c.add(std::make_unique<OscillatorModule>(), 40, 40);
    const auto filter = c.add(std::make_unique<FilterModule>(), 640, 40);
    const auto lfo = c.add(std::make_unique<LFOModule>(), 600, 600);

    ASSERT_TRUE(c.editor.keyPressed(kRight)); // nothing selected: the first card
    EXPECT_EQ(c.editor.getSelectedNodes(), std::vector<NodeID>{osc});
    ASSERT_TRUE(c.editor.keyPressed(kRight));
    EXPECT_EQ(c.editor.getSelectedNodes(), std::vector<NodeID>{filter});
    ASSERT_TRUE(c.editor.keyPressed(kRight)); // nothing further right: consumed, unchanged
    EXPECT_EQ(c.editor.getSelectedNodes(), std::vector<NodeID>{filter});
    ASSERT_TRUE(c.editor.keyPressed(kDown));
    EXPECT_EQ(c.editor.getSelectedNodes(), std::vector<NodeID>{lfo});
}

TEST(CanvasCardKeyboard, ArrowsOnAnEmptyCanvasFallThrough) {
    Canvas c;
    EXPECT_FALSE(c.editor.keyPressed(kRight));
}

TEST(CanvasCardKeyboard, AReboundKeyIsHonouredAndTheOldOneGoesInert) {
    Canvas c;
    const auto osc = c.add(std::make_unique<OscillatorModule>(), 40, 40);
    ShortcutManager shortcuts;
    shortcuts.setBinding("canvasSelectCardRight", juce::KeyPress('d'));
    c.editor.getCardKeyboard().setShortcutManager(&shortcuts);

    EXPECT_FALSE(c.editor.keyPressed(kRight));
    EXPECT_TRUE(c.editor.keyPressed(juce::KeyPress('d')));
    EXPECT_EQ(c.editor.getSelectedNodes(), std::vector<NodeID>{osc});
    c.editor.getCardKeyboard().setShortcutManager(nullptr);
}

TEST(CanvasCardKeyboard, AltArrowMovesTheCardOneGridStepAsOneUndoStep) {
    Canvas c;
    const auto osc = c.add(std::make_unique<OscillatorModule>(), 40, 40);
    c.editor.selectModule(osc, false);
    auto* card = c.cardFor(osc);
    ASSERT_NE(card, nullptr);
    const auto before = card->getPosition();
    ASSERT_FALSE(c.undo.canUndo());

    ASSERT_TRUE(c.editor.keyPressed(kAltRight));
    card = c.cardFor(osc);
    EXPECT_EQ(card->getPosition(), before + juce::Point<int>(synth::LayoutUtil::kGridSize, 0));
    EXPECT_EQ(c.savedX("Oscillator"), before.x + synth::LayoutUtil::kGridSize);
    ASSERT_TRUE(c.undo.canUndo());

    c.undo.undo();
    EXPECT_FALSE(c.undo.canUndo()) << "the move must be exactly one undo step";
    EXPECT_EQ(c.savedX("Oscillator"), before.x);
    ASSERT_EQ(c.editor.getModuleComponents().size(), 1);
    EXPECT_EQ(c.editor.getModuleComponents()[0]->getPosition(), before);
}

TEST(CanvasCardKeyboard, AltArrowMovesAMultiSelectionTogetherAsOneUndoStep) {
    Canvas c;
    const auto a = c.add(std::make_unique<OscillatorModule>(), 40, 40);
    const auto b = c.add(std::make_unique<FilterModule>(), 440, 40);
    c.editor.setSelectedNodes({a, b});
    const auto beforeA = c.cardFor(a)->getPosition();
    const auto beforeB = c.cardFor(b)->getPosition();

    ASSERT_TRUE(c.editor.keyPressed(kAltRight));
    const juce::Point<int> step(synth::LayoutUtil::kGridSize, 0);
    EXPECT_EQ(c.cardFor(a)->getPosition(), beforeA + step);
    EXPECT_EQ(c.cardFor(b)->getPosition(), beforeB + step);

    c.undo.undo();
    EXPECT_FALSE(c.undo.canUndo());
    EXPECT_EQ(c.savedX("Oscillator"), beforeA.x);
    EXPECT_EQ(c.savedX("Filter"), beforeB.x);
}

TEST(CanvasCardKeyboard, AltArrowWithNothingSelectedFallsThrough) {
    Canvas c;
    c.add(std::make_unique<OscillatorModule>(), 40, 40);
    EXPECT_FALSE(c.editor.keyPressed(kAltRight));
    EXPECT_FALSE(c.undo.canUndo());
}

TEST(CanvasCardKeyboard, ReturnEntersTheSelectedCardAtItsFirstControl) {
    Canvas c;
    const auto filter = c.add(std::make_unique<FilterModule>(), 40, 40);
    auto* card = c.cardFor(filter);
    ASSERT_NE(card, nullptr);
    card->setRecordFocusForTest(true);

    EXPECT_FALSE(c.editor.keyPressed(juce::KeyPress(juce::KeyPress::returnKey))) << "nothing selected";
    c.editor.selectModule(filter, false);
    ASSERT_TRUE(c.editor.keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    const auto stops = card->getKeyboardControls();
    ASSERT_FALSE(stops.empty());
    EXPECT_EQ(card->getRecordedFocusForTest(), stops.front());
    EXPECT_TRUE(stops.front()->getTitle().isNotEmpty());
    EXPECT_GE(card->getLocalArea(stops.front()->getParentComponent(), stops.front()->getBounds()).getY(),
              ModuleComponent::kHeaderHeight)
        << "the first stop is a body control (Filter Type, then the knobs), not a header button";
}

TEST(CanvasCardKeyboard, AltArrowLeavesACollapsedMacrosHiddenMembersAlone) {
    Canvas c;
    const auto a = c.add(std::make_unique<OscillatorModule>(), 40, 40);
    const auto b = c.add(std::make_unique<FilterModule>(), 440, 40);
    c.cardFor(a)->setVisible(false); // what a collapsed macro does to its members
    c.cardFor(b)->setVisible(false);
    c.editor.setSelectedNodes({a, b});

    EXPECT_FALSE(c.editor.keyPressed(kAltRight));
    EXPECT_FALSE(c.undo.canUndo());
    EXPECT_EQ(c.savedX("Oscillator"), 40);
}
