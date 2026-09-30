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
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace {

juce::MouseEvent makeHullMouseEvent(juce::Component& comp, juce::Point<int> position, bool shift = false) {
    const auto pos = position.toFloat();
    int flags = juce::ModifierKeys::leftButtonModifier;
    if (shift)
        flags |= juce::ModifierKeys::shiftModifier;
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos, juce::ModifierKeys(flags), 0.0f,
                            0.0f, 0.0f, 0.0f, 0.0f, &comp, &comp, juce::Time::getCurrentTime(), pos,
                            juce::Time::getCurrentTime(), 1, false);
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
