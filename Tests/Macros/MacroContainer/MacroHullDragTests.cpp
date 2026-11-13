// MacroHullDragTests.cpp
// The "moveMacroOnHullDrag" preference (GraphEditor::setMoveMacroOnHullDragEnabled): with it OFF
// (default) a drag on empty space inside an expanded macro's hull pans the canvas; with it ON the same
// drag moves the macro through the name chip's beginSelectionDrag/dragSelectionBy/finalizeSelectionDrag
// path (one undo step). Shift-drag is a marquee either way. Events go through the real
// GraphEditor::mouseDown/mouseDrag/mouseUp.

#include "AudioEngine/AudioEngine.h"
#include "MacroContainerTestHelpers.h"

#include "AppUndoManager.h"
#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include "UI/Layout/LayoutUtil.h"
#include "UI/Macros/MacroCardComponent/MacroCardComponent.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace {

juce::MouseEvent makeHullMouseEvent(juce::Component& comp, juce::Point<int> position, bool shift = false,
                                    int numClicks = 1) {
    const auto pos = position.toFloat();
    int flags = juce::ModifierKeys::leftButtonModifier;
    if (shift)
        flags |= juce::ModifierKeys::shiftModifier;
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos, juce::ModifierKeys(flags), 0.0f,
                            0.0f, 0.0f, 0.0f, 0.0f, &comp, &comp, juce::Time::getCurrentTime(), pos,
                            juce::Time::getCurrentTime(), numClicks, false);
}

struct HullFixture {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    NodeID a, b;
    juce::String macroId;
    juce::Point<int> emptyHullPoint;

    HullFixture() {
        undo.setGraphEditor(&editor);
        editor.setSize(1600, 1200);
        a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 300, 300);
        b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 700, 300);
        editor.setSelectedNodes({a, b});
        macroId = editor.getMacroController().groupSelectionIntoMacro();
        editor.getMacroController().setMacroCollapsed(macroId, false);
        // Fresh (identity) transform, so canvas == editor coordinates.
        emptyHullPoint = editor.getMacroController().macroHullBounds(macroId).getCentre();
    }

    bool pointIsEmptyHull() {
        if (editor.getMacroController().macroHullAt(emptyHullPoint) != macroId)
            return false;
        for (auto* comp : editor.getModuleComponents())
            if (comp != nullptr && comp->getBounds().contains(emptyHullPoint))
                return false;
        return true;
    }

    void drag(juce::Point<int> delta, bool shift = false) {
        editor.mouseDown(makeHullMouseEvent(editor, emptyHullPoint, shift));
        editor.mouseDrag(makeHullMouseEvent(editor, emptyHullPoint + delta, shift));
        editor.mouseUp(makeHullMouseEvent(editor, emptyHullPoint + delta, shift));
    }
};

} // namespace

TEST(MacroHullDrag, DefaultsOff) {
    AudioEngine engine;
    GraphEditor editor(engine);
    EXPECT_FALSE(editor.getMoveMacroOnHullDragEnabled());
}

TEST(MacroHullDrag, OffPansTheCanvasAndLeavesMembersAlone) {
    HullFixture f;
    ASSERT_TRUE(f.pointIsEmptyHull());
    const auto posA = findComponent(f.editor, f.a)->getPosition();
    const auto posB = findComponent(f.editor, f.b)->getPosition();
    const auto viewBefore = f.editor.getVisibleCanvasRect();

    f.drag({120, 80});

    EXPECT_NE(f.editor.getVisibleCanvasRect().getPosition(), viewBefore.getPosition()) << "the canvas panned";
    EXPECT_EQ(findComponent(f.editor, f.a)->getPosition(), posA);
    EXPECT_EQ(findComponent(f.editor, f.b)->getPosition(), posB);
}

TEST(MacroHullDrag, OnMovesTheMacroAsOneRigidBodyAndOneUndoRestoresIt) {
    HullFixture f;
    f.editor.setMoveMacroOnHullDragEnabled(true);
    ASSERT_TRUE(f.pointIsEmptyHull());
    auto* compA = findComponent(f.editor, f.a);
    auto* compB = findComponent(f.editor, f.b);
    const auto posA = compA->getPosition();
    const auto posB = compB->getPosition();
    const auto viewBefore = f.editor.getVisibleCanvasRect();
    f.undo.clearUndoHistory();

    f.drag({120, 80});

    EXPECT_EQ(f.editor.getVisibleCanvasRect().getPosition(), viewBefore.getPosition()) << "no pan";
    EXPECT_NE(compA->getPosition(), posA);
    EXPECT_NE(compB->getPosition(), posB);
    EXPECT_EQ(compB->getPosition() - compA->getPosition(), posB - posA) << "rigid body";

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    compA = findComponent(f.editor, f.a);
    compB = findComponent(f.editor, f.b);
    EXPECT_EQ(compA->getPosition(), posA);
    EXPECT_EQ(compB->getPosition(), posB);
    EXPECT_FALSE(f.undo.canUndo()) << "the whole drag was a single undo step";
}

TEST(MacroHullDrag, OnClickWithoutMovingSelectsTheMacroAndPushesNoUndo) {
    HullFixture f;
    f.editor.setMoveMacroOnHullDragEnabled(true);
    ASSERT_TRUE(f.pointIsEmptyHull());
    f.editor.clearSelection();
    f.undo.clearUndoHistory();

    f.editor.mouseDown(makeHullMouseEvent(f.editor, f.emptyHullPoint));
    f.editor.mouseUp(makeHullMouseEvent(f.editor, f.emptyHullPoint));

    EXPECT_EQ(f.editor.getSelectionCount(), 2);
    EXPECT_FALSE(f.undo.canUndo());
}

namespace {
void clickAt(HullFixture& f, juce::Point<int> p, int numClicks = 1) {
    f.editor.mouseDown(makeHullMouseEvent(f.editor, p, false, numClicks));
    f.editor.mouseUp(makeHullMouseEvent(f.editor, p, false, numClicks));
}
} // namespace

// The hull reads as empty canvas, so a click on an already-selected macro's hull clears.
TEST(MacroHullDrag, ClickingTheHullOfASelectedMacroClearsInOneClick) {
    HullFixture f;
    ASSERT_TRUE(f.pointIsEmptyHull());
    f.editor.clearSelection();

    clickAt(f, f.emptyHullPoint);
    EXPECT_EQ(f.editor.getSelectionCount(), 2) << "first click selects the macro";
    clickAt(f, f.emptyHullPoint);
    EXPECT_EQ(f.editor.getSelectionCount(), 0) << "second click on the same empty hull clears";
}

TEST(MacroHullDrag, DoubleClickOnTheHullKeepsTheMacroSelected) {
    HullFixture f;
    ASSERT_TRUE(f.pointIsEmptyHull());
    f.editor.clearSelection();

    clickAt(f, f.emptyHullPoint, 1);
    clickAt(f, f.emptyHullPoint, 2);
    EXPECT_EQ(f.editor.getSelectionCount(), 2) << "the second click of a double-click must not deselect";
}

TEST(MacroHullDrag, ClickingEmptyCanvasOutsideTheHullClearsASelectedMacroInOneClick) {
    HullFixture f;
    ASSERT_TRUE(f.pointIsEmptyHull());
    f.editor.clearSelection();
    clickAt(f, f.emptyHullPoint);
    ASSERT_EQ(f.editor.getSelectionCount(), 2);

    const juce::Point<int> outside(1550, 1150);
    ASSERT_TRUE(f.editor.getMacroController().macroHullAt(outside).isEmpty());
    clickAt(f, outside);
    EXPECT_EQ(f.editor.getSelectionCount(), 0);
}

TEST(MacroHullDrag, ShiftDragInsideTheHullStillMarqueesInBothModes) {
    for (const bool pref : {false, true}) {
        HullFixture f;
        f.editor.setMoveMacroOnHullDragEnabled(pref);
        ASSERT_TRUE(f.pointIsEmptyHull());
        const auto posA = findComponent(f.editor, f.a)->getPosition();
        const auto viewBefore = f.editor.getVisibleCanvasRect();

        f.editor.mouseDown(makeHullMouseEvent(f.editor, f.emptyHullPoint, true));
        EXPECT_TRUE(f.editor.isMarqueeActive()) << "pref=" << pref;
        f.editor.mouseDrag(makeHullMouseEvent(f.editor, f.emptyHullPoint + juce::Point<int>(40, 40), true));
        f.editor.mouseUp(makeHullMouseEvent(f.editor, f.emptyHullPoint + juce::Point<int>(40, 40), true));

        EXPECT_FALSE(f.editor.isMarqueeActive());
        EXPECT_EQ(findComponent(f.editor, f.a)->getPosition(), posA) << "pref=" << pref;
        EXPECT_EQ(f.editor.getVisibleCanvasRect().getPosition(), viewBefore.getPosition()) << "pref=" << pref;
    }
}

TEST(MacroHullDrag, OnStillPansOutsideAnyHull) {
    HullFixture f;
    f.editor.setMoveMacroOnHullDragEnabled(true);
    const juce::Point<int> outside(1400, 1000);
    ASSERT_TRUE(f.editor.getMacroController().macroHullAt(outside).isEmpty());
    const auto viewBefore = f.editor.getVisibleCanvasRect();
    f.editor.mouseDown(makeHullMouseEvent(f.editor, outside));
    f.editor.mouseDrag(makeHullMouseEvent(f.editor, outside + juce::Point<int>(-50, -30)));
    f.editor.mouseUp(makeHullMouseEvent(f.editor, outside + juce::Point<int>(-50, -30)));
    EXPECT_NE(f.editor.getVisibleCanvasRect().getPosition(), viewBefore.getPosition());
}

// ---- Dropping a collapsed macro's card onto another module -------------------------------------
// Real MacroCardComponent mouseDown/mouseDrag/mouseUp: the card lands in the nearest free slot like any other drop,
// and the hidden members ride the same delta.

namespace {

juce::MouseEvent makeCardMouseEvent(juce::Component& card, juce::Point<int> position, juce::Point<int> downPosition,
                                    bool dragged) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), position.toFloat(),
                            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &card, &card, juce::Time::getCurrentTime(), downPosition.toFloat(),
                            juce::Time::getCurrentTime(), 1, dragged);
}

} // namespace

TEST(MacroCardDrop, CollapsedCardDroppedOnAModuleLandsClearAndCarriesItsMembers) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);

    const auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    const auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    const auto loose = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 900, 600);
    editor.setSelectedNodes({a, b});
    const auto macroId = editor.getMacroController().groupSelectionIntoMacro(); // collapses by default
    ASSERT_FALSE(macroId.isEmpty());
    ASSERT_TRUE(editor.getMacros().find(macroId)->collapsed);

    auto* card = editor.getMacroController().getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);
    const auto looseRect = findComponent(editor, loose)->getBounds();
    const auto cardStart = card->getPosition();
    const auto posA = findComponent(editor, a)->getPosition();
    const auto posB = findComponent(editor, b)->getPosition();
    ASSERT_FALSE(card->getBounds().expanded(synth::LayoutUtil::kCollisionGap).intersects(looseRect));
    undo.clearUndoHistory();

    // Drag so the card's top-left lands 8 px inside the loose module.
    const juce::Point<int> press(140, 60); // body, below the title row
    const auto delta = looseRect.getPosition() + juce::Point<int>(8, 8) - cardStart;
    card->mouseDown(makeCardMouseEvent(*card, press, press, false));
    card->mouseDrag(makeCardMouseEvent(*card, press + delta, press, true));
    card->mouseUp(makeCardMouseEvent(*card, press + delta, press, true));

    card = editor.getMacroController().getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);
    const auto cardRect = editor.getMacros().find(macroId)->bounds;
    EXPECT_EQ(card->getPosition(), cardRect.getPosition());
    EXPECT_FALSE(cardRect.expanded(synth::LayoutUtil::kCollisionGap).intersects(looseRect))
        << "the dropped card must not overlap the module it was dropped on";

    const auto moved = cardRect.getPosition() - cardStart;
    EXPECT_NE(moved, juce::Point<int>()) << "the card moved";
    EXPECT_EQ(findComponent(editor, a)->getPosition() - posA, moved) << "hidden members ride the card's delta";
    EXPECT_EQ(findComponent(editor, b)->getPosition() - posB, moved);
    EXPECT_EQ(findComponent(editor, loose)->getBounds(), looseRect) << "the other module stays put";

    ASSERT_TRUE(undo.canUndo());
    undo.undo();
    EXPECT_EQ(editor.getMacros().find(macroId)->bounds.getPosition(), cardStart);
    EXPECT_EQ(findComponent(editor, a)->getPosition(), posA);
    EXPECT_EQ(findComponent(editor, b)->getPosition(), posB);
    EXPECT_FALSE(undo.canUndo()) << "the whole drop was one undo step";
}
