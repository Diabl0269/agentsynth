// MacroFoldDeepNestTests.cpp
//
// Three macros nested in one another (docs/layout/animation.md "Macro fold", nested macros): the fold's total time is
// that of a flat macro, not one macro's time per level, and a fold-and-open round trip through the real
// Cmd+G toggle brings every inner macro back in the state it had before (an open one stays open, a folded one folded).

#include "../../../Macros/MacroContainer/MacroContainerTestHelpers.h"
#include "UI/Graph/MacroFoldAnimator/MacroFoldAnimator.h"

#include "AppUndoManager.h"
#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include "UI/Graph/CardGlideAnimator/CardGlideAnimator.h"
#include "UI/Layout/ReducedMotion.h"
#include <gtest/gtest.h>

namespace {

namespace mf = synth::ui::macro_fold;

struct SystemMotionPin {
    SystemMotionPin() { synth::ui::setReducedMotionForTest(false); }
    ~SystemMotionPin() { synth::ui::setReducedMotionForTest(std::nullopt); }
};

// innermost {a1, a2} in middle {.., b1, b2} in outer {.., c1, c2}, all open.
struct DeepNest {
    SystemMotionPin pin;
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    NodeID a1, a2, b1, b2, c1, c2;
    juce::String inner, middle, outer;

    DeepNest() {
        undo.setGraphEditor(&editor);
        editor.setSize(2400, 2000);
        a1 = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 400, 200);
        a2 = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 800, 200);
        b1 = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 300, 700);
        b2 = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 900, 700);
        c1 = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 300, 1300);
        c2 = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 900, 1300);
        editor.setSelectedNodes({a1, a2});
        inner = ctl().groupSelectionIntoMacro(false);
        editor.setSelectedNodes({a1, a2, b1, b2});
        middle = ctl().groupSelectionIntoMacro();
        editor.setSelectedNodes({a1, a2, b1, b2, c1, c2});
        outer = ctl().groupSelectionIntoMacro();
        for (const auto& id : {inner, middle, outer})
            ctl().setMacroCollapsed(id, false);
        editor.clearSelection();
        editor.updateComponents();
        undo.clearUndoHistory();
    }

    bool nested() {
        const auto& m = editor.getMacros();
        return inner.isNotEmpty() && middle.isNotEmpty() && outer.isNotEmpty() && m.parentOf(inner) == middle &&
               m.parentOf(middle) == outer;
    }
    MacroGroupController& ctl() { return editor.getMacroController(); }
    MacroFoldAnimator& fold() { return editor.getCardGlideForTest().fold(); }
    void animate() { editor.getCardGlideForTest().setForceAnimateForTest(true); }
    void land() { editor.finishCardGlideForTest(); }
    bool collapsed(const juce::String& id) { return editor.getMacros().find(id)->collapsed; }
    // The real gesture: select the macro's modules, press Cmd+G.
    void toggleByShortcut(const std::vector<NodeID>& nodes) {
        editor.setSelectedNodes(nodes);
        ctl().groupOrToggleSelectionMacros();
    }
};

} // namespace

TEST(MacroFoldDeepNest, ATreeOfThreeFoldsInTheTimeOfOneMacroNotThree) {
    DeepNest n;
    ASSERT_TRUE(n.nested());
    n.animate();
    n.ctl().setMacroCollapsed(n.outer, true);
    ASSERT_TRUE(n.fold().isLive());
    EXPECT_LE(n.fold().totalMs(), mf::kTotalMs + 1e-9) << "collapsing three levels";
    for (const auto& id : {n.inner, n.middle, n.outer}) {
        const auto span = n.fold().spanFor(id);
        ASSERT_TRUE(span.has_value());
        EXPECT_GT(span->second, span->first);
    }
    n.fold().applyAtMs(n.fold().totalMs());
    n.land();

    n.ctl().setMacroCollapsed(n.outer, false);
    ASSERT_TRUE(n.fold().isLive());
    EXPECT_LE(n.fold().totalMs(), mf::kTotalMs + 1e-9) << "expanding three levels";
    n.land();
}

TEST(MacroFoldDeepNest, AFlatMacroKeepsItsFullTimingAndANestOfTwoShareOneTotal) {
    const mf::Timeline flat{4, true};
    const mf::Timeline third{4, true, false, 1.0 / 3.0};
    EXPECT_DOUBLE_EQ(flat.modulesEndMs(), mf::kStaggerMs * 3 + mf::kFlightMs);
    EXPECT_NEAR(third.modulesEndMs(), flat.modulesEndMs() / 3.0, 1e-9);
    EXPECT_LE(3 * third.modulesEndMs(), mf::kTotalMs + 1e-9) << "three levels in a row stay within one total";
    EXPECT_NEAR(third.handoverMs(), mf::kHandoverMs / 3.0, 1e-9);
}

TEST(MacroFoldDeepNest, FoldingTheWholeNestWithCmdGOpensWithEveryInnerMacroAsItWas) {
    DeepNest n;
    ASSERT_TRUE(n.nested());
    n.animate();
    // Every module of the nest is selected, so the shortcut touches all three macros.
    n.toggleByShortcut({n.a1, n.a2, n.b1, n.b2, n.c1, n.c2});
    ASSERT_TRUE(n.collapsed(n.outer));
    EXPECT_FALSE(n.collapsed(n.middle)) << "an inner macro keeps its own flag when the outer one folds";
    EXPECT_FALSE(n.collapsed(n.inner));
    ASSERT_TRUE(n.fold().isLive());
    EXPECT_LE(n.fold().totalMs(), mf::kTotalMs + 1e-9);
    n.land();

    // What is on the canvas now is the outer card; the same shortcut opens it.
    n.toggleByShortcut({n.a1, n.a2, n.b1, n.b2, n.c1, n.c2});
    EXPECT_FALSE(n.collapsed(n.outer));
    EXPECT_FALSE(n.collapsed(n.middle)) << "the middle macro is open again";
    EXPECT_FALSE(n.collapsed(n.inner)) << "and so is the innermost";
    n.land();
    for (auto id : {n.a1, n.a2, n.b1, n.b2, n.c1, n.c2})
        EXPECT_TRUE(findComponent(n.editor, id)->isVisible());
}

TEST(MacroFoldDeepNest, AnInnerMacroThatWasFoldedStaysFoldedWhenTheOuterOpens) {
    DeepNest n;
    ASSERT_TRUE(n.nested());
    n.ctl().setMacroCollapsed(n.inner, true);
    n.toggleByShortcut({n.b1, n.b2, n.c1, n.c2});
    ASSERT_TRUE(n.collapsed(n.outer));
    n.toggleByShortcut({n.b1, n.b2, n.c1, n.c2});
    EXPECT_FALSE(n.collapsed(n.outer));
    EXPECT_FALSE(n.collapsed(n.middle));
    EXPECT_TRUE(n.collapsed(n.inner)) << "it was folded before, so it is folded after";
}

TEST(MacroFoldDeepNest, FoldAndPackOverTheNestAlsoLeavesInnerMacrosAlone) {
    DeepNest n;
    ASSERT_TRUE(n.nested());
    n.editor.setSelectedNodes({n.a1, n.a2, n.b1, n.b2, n.c1, n.c2});
    n.ctl().foldAndPackSelectionMacros();
    EXPECT_TRUE(n.collapsed(n.outer));
    EXPECT_FALSE(n.collapsed(n.middle));
    EXPECT_FALSE(n.collapsed(n.inner));
    n.editor.setSelectedNodes({n.a1, n.a2, n.b1, n.b2, n.c1, n.c2});
    n.ctl().foldAndPackSelectionMacros();
    EXPECT_FALSE(n.collapsed(n.outer));
    EXPECT_FALSE(n.collapsed(n.middle));
    EXPECT_FALSE(n.collapsed(n.inner));
}
