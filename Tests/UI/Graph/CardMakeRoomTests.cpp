// CardMakeRoomTests.cpp
// A card that changes height in place on the canvas pushes the neighbours it now covers aside and, when it shrinks
// again, brings them back (docs/layout/layout.md, "Making room when something grows"). Everything goes through the
// real card path: the Macro bank's Knobs count (ModuleComponent::refreshPortLayout, the same steps the parameter
// change takes) and a real click on a Show Scope toggle, with the app's undo manager around the gesture.

#include "../../Macros/MacroContainer/MacroContainerTestHelpers.h"

#include "AppUndoManager.h"
#include "Modules/MacroControlModule.h"
#include "Modules/OscillatorModule.h"
#include "UI/Layout/LayoutUtil.h"
#include <gtest/gtest.h>

namespace {

struct Canvas {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};

    Canvas() {
        undo.setGraphEditor(&editor);
        editor.setSize(3200, 2400);
    }

    NodeID bank(int x, int y, int count) {
        auto module = std::make_unique<MacroControlModule>();
        setCount(*module, count);
        return addModuleAt(editor, engine, std::move(module), x, y);
    }
    NodeID osc(int x, int y) { return addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), x, y); }

    juce::Rectangle<int> rect(NodeID id) { return findComponent(editor, id)->getBounds(); }

    // Moves a card the way a finished drag leaves it: live component and node properties agree.
    void moveByHand(NodeID id, juce::Point<int> pos) {
        findComponent(editor, id)->setTopLeftPosition(pos);
        engine.getGraph().getNodeForId(id)->properties.set("x", pos.x);
        engine.getGraph().getNodeForId(id)->properties.set("y", pos.y);
    }

    static void setCount(MacroControlModule& module, int count) {
        for (auto* p : module.getParameters())
            if (auto* i = dynamic_cast<juce::AudioParameterInt*>(p))
                if (i->paramID == "macroCount")
                    i->setValueNotifyingHost(i->convertTo0to1(count));
    }

    // The bank's Knobs count changes in place, exactly the steps ModuleComponent::applyMacroCountChange takes.
    void resizeBank(NodeID id, int count) {
        auto* node = engine.getGraph().getNodeForId(id);
        setCount(*dynamic_cast<MacroControlModule*>(node->getProcessor()), count);
        findComponent(editor, id)->refreshPortLayout();
    }

    // One undo step around the gesture, like the parameter gesture's own snapshot.
    void resizeBankUndoably(NodeID id, int count) {
        undo.recordStructuralChange(engine.getGraph(), [&] { resizeBank(id, count); });
    }
};

constexpr int kSmall = 4;
constexpr int kLarge = 16;

// A bank at (400, 300) and a neighbour to its lower right: the grown bank covers it, and the shortest way out is
// to the right (about 292 px) rather than down (about 470 px).
struct Scene {
    Canvas c;
    NodeID bank, neighbour;
    juce::Rectangle<int> neighbourHome;

    Scene() {
        bank = c.bank(400, 300, kSmall);
        neighbour = c.osc(504, 650);
        neighbourHome = c.rect(neighbour);
    }
};

} // namespace

TEST(CardMakeRoom, ABankThatGrowsPushesTheNeighbourItCoversTheShortestWay) {
    Scene s;
    ASSERT_FALSE(s.c.rect(s.bank).intersects(s.neighbourHome)) << "premise: clear at the small size";

    s.c.resizeBank(s.bank, kLarge);

    const auto bank = s.c.rect(s.bank);
    EXPECT_EQ(bank.getPosition(), juce::Point<int>(400, 300)) << "the grower never moves";
    ASSERT_GT(bank.getHeight(), s.neighbourHome.getBottom() - bank.getY()) << "premise: the bank now covers it";
    const auto moved = s.c.rect(s.neighbour);
    EXPECT_FALSE(moved.expanded(synth::LayoutUtil::kCollisionGap).intersects(bank));
    EXPECT_EQ(moved.getY(), s.neighbourHome.getY()) << "it went sideways, not down";
    EXPECT_GT(moved.getX(), s.neighbourHome.getX());
    EXPECT_EQ(moved.getX() % synth::LayoutUtil::kGridSize, 0);
}

TEST(CardMakeRoom, ShrinkingTheBankBringsTheNeighbourHome) {
    Scene s;
    s.c.resizeBank(s.bank, kLarge);
    ASSERT_NE(s.c.rect(s.neighbour), s.neighbourHome);

    s.c.resizeBank(s.bank, kSmall);

    EXPECT_EQ(s.c.rect(s.neighbour), s.neighbourHome);
    EXPECT_EQ(s.c.engine.getGraph().getNodeForId(s.neighbour)->properties["x"], juce::var(s.neighbourHome.getX()))
        << "the node, not just the card, is back";
}

TEST(CardMakeRoom, GrowShrinkGrowSettlesTheSameEveryTime) {
    Scene s;
    s.c.resizeBank(s.bank, kLarge);
    const auto pushed = s.c.rect(s.neighbour);
    s.c.resizeBank(s.bank, kSmall);
    ASSERT_EQ(s.c.rect(s.neighbour), s.neighbourHome);

    s.c.resizeBank(s.bank, kLarge);
    EXPECT_EQ(s.c.rect(s.neighbour), pushed);
    s.c.resizeBank(s.bank, kSmall);
    EXPECT_EQ(s.c.rect(s.neighbour), s.neighbourHome);
}

TEST(CardMakeRoom, ABankThatShrinksOnlyPartWayLeavesWhatItStillCoversPushed) {
    Scene s;
    s.c.resizeBank(s.bank, kLarge);
    const auto pushed = s.c.rect(s.neighbour);

    // Still tall enough to reach the neighbour's home (bank bottom > 650 - gap), but narrower than the full 16.
    s.c.resizeBank(s.bank, 10);
    ASSERT_GT(s.c.rect(s.bank).getBottom() + synth::LayoutUtil::kCollisionGap, s.neighbourHome.getY())
        << "premise: the home is still covered";
    EXPECT_EQ(s.c.rect(s.neighbour), pushed) << "a blocked home keeps the neighbour where it is";

    s.c.resizeBank(s.bank, kSmall);
    EXPECT_EQ(s.c.rect(s.neighbour), s.neighbourHome) << "and its record survived to the next shrink";
}

TEST(CardMakeRoom, ANeighbourMovedByHandStaysWhereTheUserPutIt) {
    Scene s;
    s.c.resizeBank(s.bank, kLarge);
    const juce::Point<int> byHand{2000, 1200};
    s.c.moveByHand(s.neighbour, byHand);

    s.c.resizeBank(s.bank, kSmall);

    EXPECT_EQ(s.c.rect(s.neighbour).getPosition(), byHand);
}

TEST(CardMakeRoom, ANeighbourWhoseHomeIsNowTakenStaysPut) {
    Scene s;
    s.c.resizeBank(s.bank, kLarge);
    const auto pushed = s.c.rect(s.neighbour);
    const auto squatter = s.c.osc(2000, 1200);
    s.c.moveByHand(squatter, s.neighbourHome.getPosition());

    s.c.resizeBank(s.bank, kSmall);

    EXPECT_EQ(s.c.rect(s.neighbour), pushed);
    EXPECT_EQ(s.c.rect(squatter).getPosition(), s.neighbourHome.getPosition());
}

TEST(CardMakeRoom, OneUndoOfTheGrowPutsTheBankAndTheNeighbourBack) {
    Scene s;
    s.c.undo.clearUndoHistory();
    const auto bankStart = s.c.rect(s.bank).getPosition();

    s.c.resizeBankUndoably(s.bank, kLarge);
    ASSERT_NE(s.c.rect(s.neighbour), s.neighbourHome);

    ASSERT_TRUE(s.c.undo.undo());

    EXPECT_EQ(s.c.rect(s.neighbour).getPosition(), s.neighbourHome.getPosition());
    EXPECT_EQ(s.c.rect(s.bank).getPosition(), bankStart);
    EXPECT_FALSE(s.c.undo.canUndo()) << "the push was part of the single step";
}

// Undo and redo restore snapshots, so the card's record is dropped with them: a redone push is not "owed" back.
TEST(CardMakeRoom, ARestoreForgetsWhatTheCardPushed) {
    Scene s;
    s.c.undo.clearUndoHistory();
    s.c.resizeBankUndoably(s.bank, kLarge);
    const auto pushed = s.c.rect(s.neighbour);
    ASSERT_TRUE(s.c.undo.undo());
    ASSERT_TRUE(s.c.undo.redo());
    ASSERT_EQ(s.c.rect(s.neighbour).getPosition(), pushed.getPosition());

    s.c.resizeBank(s.bank, kSmall);

    EXPECT_EQ(s.c.rect(s.neighbour).getPosition(), pushed.getPosition());
}

// Deleting the grower drops its record; a new card that reuses nothing of it never pulls anyone home.
TEST(CardMakeRoom, DeletingTheGrowerForgetsItsPushes) {
    Scene s;
    s.c.resizeBank(s.bank, kLarge);
    const auto pushed = s.c.rect(s.neighbour);

    s.c.editor.requestDeleteModule(s.bank);

    EXPECT_EQ(s.c.rect(s.neighbour), pushed) << "deleting the card does not move anyone";
}

// A project load builds the cards at their saved geometry: a bank saved overlapping a neighbour leaves both exactly
// where they were saved, and what was pushed before the load is not owed back afterwards.
TEST(CardMakeRoom, LoadingNeverPushesAndForgetsEarlierPushes) {
    {
        Canvas c;
        auto module = std::make_unique<MacroControlModule>();
        Canvas::setCount(*module, kLarge);
        auto node = c.engine.getGraph().addNode(std::move(module));
        node->properties.set("x", 400);
        node->properties.set("y", 300);
        auto osc = c.engine.getGraph().addNode(std::make_unique<OscillatorModule>());
        osc->properties.set("x", 504);
        osc->properties.set("y", 650);
        c.editor.updateComponents(); // the load step: graph populated first, then the cards
        EXPECT_EQ(c.rect(osc->nodeID).getPosition(), juce::Point<int>(504, 650));
        EXPECT_EQ(c.rect(node->nodeID).getPosition(), juce::Point<int>(400, 300));
    }
    {
        Scene s;
        s.c.resizeBank(s.bank, kLarge);
        const auto pushed = s.c.rect(s.neighbour);
        s.c.editor.detachAllModuleComponents(); // every graph-replacing load goes through here first
        s.c.editor.updateComponents();

        s.c.resizeBank(s.bank, kSmall);

        EXPECT_EQ(s.c.rect(s.neighbour).getPosition(), pushed.getPosition());
    }
}

// The same rule for any in-place height change, not only the Macro bank: opening the Scope under an oscillator pushes
// the neighbour below it aside, closing it brings the neighbour home.
TEST(CardMakeRoom, TheScopeToggleMakesRoomAndGivesItBack) {
    Canvas c;
    const auto osc = c.osc(400, 300);
    const auto neighbour = c.osc(400, c.rect(osc).getBottom() + synth::LayoutUtil::kCollisionGap);
    const auto home = c.rect(neighbour);
    auto* card = findComponent(c.editor, osc);
    juce::ToggleButton* toggle = nullptr;
    for (auto* child : card->getChildren())
        if (auto* t = dynamic_cast<juce::ToggleButton*>(child); t != nullptr && t->getButtonText() == "Show Scope")
            toggle = t;
    ASSERT_NE(toggle, nullptr);

    toggle->triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    ASSERT_TRUE(toggle->getToggleState());
    EXPECT_FALSE(c.rect(neighbour).intersects(card->getBounds()));
    EXPECT_NE(c.rect(neighbour), home);

    toggle->triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    EXPECT_EQ(c.rect(neighbour), home);
}
