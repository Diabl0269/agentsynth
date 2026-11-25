// CardGlideMacroTakeOutUndoTests.cpp
//
// Undo and redo of taking a connected module out of a macro glide the module, with its cables, instead of jumping
// (docs/layout/animation.md "Undo and redo glide"). The take-out here is the real Cmd-drag out of the hull: it moves
// the card, re-routes its cables through macro ports and changes the macro's membership in ONE undo step. The restore
// frees and re-creates port nodes, which used to tear down every card, so none had a "before" rect to slide from.

#include "../../../Macros/MacroContainer/MacroDragTestHelpers.h"

#include "AppUndoManager.h"
#include "UI/Graph/CardGlideAnimator/CardGlideAnimator.h"
#include "UI/Layout/UIAnimation.h"
#include <cmath>
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
        editor.getCardGlideForTest().setForceAnimateForTest(true); // not on screen: the glide is what is under test
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

namespace {

// How long after the key the module's drawn place first differs from where it was on the key's own frame, read from the
// glide's frames at 4 ms steps the way the VBlank drives them (the phased timeline's linear time, or the plain glide's
// eased one). A count of frames, never a clock, so it holds on a loaded machine.
double msUntilTheModuleMoves(CardGlideAnimator& glide, uint32_t uid) {
    const bool phased = glide.exitGhostCount() + glide.enterGhostCount() + glide.borderGhostCount() > 0;
    const double total = phased ? glide.timeline().totalMs() : 160.0;
    glide.stepFrameForTest(0.0f);
    const auto start = glide.offsetFor(uid);
    for (double ms = 0.0; ms <= total; ms += 4.0) {
        const auto t = static_cast<float>(ms / total);
        glide.stepFrameForTest(phased ? t : synth::ui::easeOutCubic(t));
        const auto at = glide.offsetFor(uid);
        if (std::abs(at.x - start.x) >= 1.0f || std::abs(at.y - start.y) >= 1.0f)
            return ms;
    }
    return total;
}

} // namespace

// An undo and its redo start the module sliding as soon as each other: a card the step removes or brings back (the
// macro ports a take-out re-routes its cables through) must not hold the module still first. The step that
// freed a port card ran the card's 180 ms shrink before the slide started, the step that created one slid at once, so
// one of Cmd+Z and Cmd+Shift+Z waited a moment before it moved (Tal, twice).
TEST(CardGlideMacroTakeOutUndo, AnUndoAndItsRedoBothStartTheModuleSlidingOnTheFirstFrame) {
    TakeOut t;
    ASSERT_FALSE(t.macroId.isEmpty());
    constexpr double kOneFrameMs = 17.0;
    for (int round = 0; round < 2; ++round) {
        SCOPED_TRACE(round);
        dragBodyBy(*t.compB(), {2400, 0}, kCmdClick);
        t.land();
        t.editor.finishHullGlideForTest();
        for (const bool redo : {false, true}) {
            SCOPED_TRACE(redo ? "redo" : "undo");
            ASSERT_TRUE(redo ? t.undo.redo() : t.undo.undo());
            ASSERT_NE(t.compB(), nullptr);
            EXPECT_TRUE(t.glide().isLive());
            EXPECT_LE(msUntilTheModuleMoves(t.glide(), t.idB().uid), kOneFrameMs)
                << "the module stood still while a card of the step shrank or grew";
            t.land();
            t.editor.finishHullGlideForTest();
        }
    }
}
