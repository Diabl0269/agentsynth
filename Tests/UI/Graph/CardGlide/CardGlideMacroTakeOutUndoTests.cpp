// CardGlideMacroTakeOutUndoTests.cpp
//
// Undo and redo of taking a connected module out of a macro glide the module, with its cables, instead of jumping
// (docs/layout/animation.md "Undo and redo glide"). The take-out here is the real Cmd-drag out of the hull: it moves
// the card, re-routes its cables through macro ports and changes the macro's membership in ONE undo step. The restore
// frees and re-creates port nodes, which used to tear down every card, so none had a "before" rect to slide from.

#include "../../../Macros/MacroContainer/MacroDragTestHelpers.h"

#include "AppUndoManager.h"
#include "UI/Graph/CardGlideAnimator/CardGlideAnimator.h"
#include <gtest/gtest.h>

namespace {

struct TakeOut {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    NodeID a, b;
    juce::String uuidB;
    juce::String macroId;

    // B sits in the macro with A; a cable from outside into B makes the take-out re-route through macro ports.
    TakeOut() {
        undo.setGraphEditor(&editor);
        editor.setSize(3200, 1200);
        a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
        b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 100, 300);
        auto outside = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 900, 100);
        uuidB = uuidOf(engine, b);
        editor.connectPorts(outside, 0, b, 0, /*isMidi=*/false);
        editor.connectPorts(a, 0, b, 1, /*isMidi=*/false);
        editor.setSelectedNodes({a, b});
        macroId = editor.getMacroController().groupSelectionIntoMacro(/*autoCreatePorts=*/true);
        editor.getMacroController().setMacroCollapsed(macroId, false);
    }

    CardGlideAnimator& glide() { return editor.getCardGlideForTest(); }
    NodeID idB() { return nodeIdForUuid(engine, uuidB); }
    ModuleComponent* compB() { return findComponent(editor, idB()); }
    void land() { editor.finishCardGlideForTest(); }
};

} // namespace

TEST(CardGlideMacroTakeOutUndo, UndoAndRedoOfTakingAConnectedModuleOutGlideTheModule) {
    TakeOut t;
    ASSERT_FALSE(t.macroId.isEmpty());
    ASSERT_NE(t.compB(), nullptr);
    const auto inside = t.compB()->getBounds();

    dragBodyBy(*t.compB(), {2400, 0}, kCmdClick);
    ASSERT_EQ(t.editor.getMacroController().macroForNode(t.idB()), nullptr) << "sanity: B left the macro";
    t.land();
    t.editor.finishHullGlideForTest();
    const auto outside = t.compB()->getBounds();
    ASSERT_NE(inside, outside);

    ASSERT_TRUE(t.undo.undo());
    ASSERT_NE(t.compB(), nullptr);
    EXPECT_EQ(t.compB()->getBounds(), inside) << "geometry is final at once";
    EXPECT_TRUE(t.glide().isLive());
    EXPECT_EQ(t.compB()->getAlpha(), 0.0f) << "the real card is hidden while its snapshot glides";
    EXPECT_EQ(t.glide().currentRectFor(t.compB()), outside) << "drawn where it was, not at the end";
    EXPECT_NE(t.glide().offsetFor(t.idB().uid), juce::Point<float>()) << "its cables follow the gliding card";
    EXPECT_TRUE(t.editor.isHullGlideLiveForTest()) << "the macro border glides back out to hold B again";
    t.land();
    t.editor.finishHullGlideForTest();
    EXPECT_EQ(t.compB()->getAlpha(), 1.0f);

    ASSERT_TRUE(t.undo.redo());
    ASSERT_NE(t.compB(), nullptr);
    EXPECT_EQ(t.compB()->getBounds(), outside);
    EXPECT_TRUE(t.glide().isLive());
    EXPECT_EQ(t.glide().currentRectFor(t.compB()), inside);
    EXPECT_TRUE(t.editor.isHullGlideLiveForTest()) << "and the border shrinks again on redo";
    t.land();
    t.editor.finishHullGlideForTest();
    EXPECT_EQ(t.compB()->getAlpha(), 1.0f);
}
