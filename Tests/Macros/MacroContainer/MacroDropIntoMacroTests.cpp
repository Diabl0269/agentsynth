// MacroDropIntoMacroTests.cpp
//
// Dropping a whole macro into an open macro nests it
// (docs/macros/menu-and-membership.md#dropping-a-macro-into-another-or-out-of-its-parent):
// a folded card dragged by its body, or an open macro dragged by its name chip, through the real MacroCardComponent and
// GraphEditor mouseDown/mouseDrag/mouseUp. The target is outlined while the drag hovers it, the drop is one undo step,
// a macro never nests into itself or a macro inside it, and the nested macro folds with its new parent.

#include "MacroNestedTestFixture.h"
#include "MacroUnitDragHelpers.h"

#include "UI/Graph/CardGlideAnimator/CardGlideAnimator.h"
#include "UI/Graph/MacroFoldAnimator/MacroFoldAnimator.h"
#include "UI/Layout/LayoutUtil.h"
#include "UI/Layout/ReducedMotion.h"

namespace {

// An open target macro {t1, t2} with a wide empty gap between its members, and a second macro {d1, d2} below it,
// folded unless `droppedOpen`.
struct DropFixture {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    NodeID t1, t2, d1, d2;
    juce::String targetId, droppedId;

    explicit DropFixture(bool droppedOpen = false) {
        undo.setGraphEditor(&editor);
        editor.setSize(2600, 1800);
        t1 = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 300, 300);
        t2 = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 1800, 300);
        d1 = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 300, 1100);
        d2 = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 700, 1100);
        editor.setSelectedNodes({t1, t2});
        targetId = ctl().groupSelectionIntoMacro(/*autoCreatePorts=*/false);
        ctl().setMacroCollapsed(targetId, false);
        editor.setSelectedNodes({d1, d2});
        droppedId = ctl().groupSelectionIntoMacro(/*autoCreatePorts=*/false);
        if (droppedOpen)
            ctl().setMacroCollapsed(droppedId, false);
        editor.clearSelection();
        undo.clearUndoHistory();
    }

    MacroGroupController& ctl() { return editor.getMacroController(); }
    juce::String parentOf(const juce::String& id) { return editor.getMacros().parentOf(id); }
    juce::Point<int> pos(NodeID id) { return findComponent(editor, id)->getPosition(); }
    // Empty space inside the target's border, between its two members.
    juce::Point<int> gapInTarget() { return {1040, pos(t1).y + 150}; }
};

struct SystemMotionPin {
    SystemMotionPin() { synth::ui::setReducedMotionForTest(false); }
    ~SystemMotionPin() { synth::ui::setReducedMotionForTest(std::nullopt); }
};

} // namespace

// Regression test for FRO800: dragging a folded macro onto an open one only moved the card; there was no way to nest
// a macro by dragging it.
TEST(MacroDropIntoMacro, AFoldedCardDroppedOnAnOpenMacroNestsThereAsOneUndoStep) {
    DropFixture f;
    ASSERT_TRUE(f.parentOf(f.droppedId).isEmpty());
    const auto cardStart = f.editor.getMacros().find(f.droppedId)->bounds;
    const auto d1Start = f.pos(f.d1);

    juce::String outlinedMidDrag;
    dragCardCentreTo(f.editor, f.droppedId, f.gapInTarget(), [&] { outlinedMidDrag = f.editor.getMacroDragJoinId(); });

    EXPECT_EQ(outlinedMidDrag, f.targetId) << "the open macro is outlined like a module drop's target";
    EXPECT_TRUE(f.editor.getMacroDragJoinId().isEmpty()) << "the outline clears on drop";
    EXPECT_EQ(f.parentOf(f.droppedId), f.targetId);
    const auto card = f.editor.getMacros().find(f.droppedId)->bounds;
    const auto dropped = cardStart.withCentre(f.gapInTarget());
    EXPECT_EQ(card.getPosition(), synth::LayoutUtil::snap(dropped.getPosition()))
        << "it lands where it was dropped (on the grid), inside the border";
    EXPECT_TRUE(f.ctl().macroHullBounds(f.targetId).contains(card));
    EXPECT_EQ(f.pos(f.d1) - d1Start, card.getPosition() - cardStart.getPosition()) << "the hidden members ride along";
    EXPECT_TRUE(f.ctl().getMacroCardForTest(f.droppedId)->isVisible());

    ASSERT_TRUE(f.undo.undo());
    EXPECT_TRUE(f.parentOf(f.droppedId).isEmpty());
    EXPECT_EQ(f.editor.getMacros().find(f.droppedId)->bounds.getPosition(), cardStart.getPosition());
    EXPECT_EQ(f.pos(f.d1), d1Start);
    EXPECT_FALSE(f.undo.canUndo()) << "the move and the nest were one step";

    ASSERT_TRUE(f.undo.redo());
    EXPECT_EQ(f.parentOf(f.droppedId), f.targetId);
    EXPECT_EQ(f.editor.getMacros().find(f.droppedId)->bounds, card);
}

TEST(MacroDropIntoMacro, AnOpenMacroDraggedByItsChipNestsIntoTheMacroUnderThePointer) {
    DropFixture f(/*droppedOpen=*/true);
    const auto d1Start = f.pos(f.d1);
    // Members land at (700, 360): in the target's gap, clear of its members.
    const auto delta = juce::Point<int>(700, 360) - d1Start;

    juce::String outlinedMidDrag;
    dragChipBy(f.editor, f.droppedId, delta, [&] { outlinedMidDrag = f.editor.getMacroDragJoinId(); });

    EXPECT_EQ(outlinedMidDrag, f.targetId);
    EXPECT_EQ(f.parentOf(f.droppedId), f.targetId);
    EXPECT_FALSE(f.editor.getMacros().find(f.droppedId)->collapsed) << "it stays open inside its new parent";
    EXPECT_TRUE(f.ctl().macroHullBounds(f.targetId).contains(f.ctl().macroHullBounds(f.droppedId)));
    EXPECT_TRUE(f.editor.getMacroDragJoinId().isEmpty());
    EXPECT_TRUE(f.ctl().macroNestCandidate().isEmpty());

    ASSERT_TRUE(f.undo.undo());
    EXPECT_TRUE(f.parentOf(f.droppedId).isEmpty());
    EXPECT_EQ(f.pos(f.d1), d1Start);
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(MacroDropIntoMacro, AMacroNeverNestsIntoItselfOrAMacroInsideIt) {
    NestedMacroFixture n;
    ASSERT_TRUE(n.linked);
    const auto inChild = n.ctl().macroHullBounds(n.childId).getCentre();
    const auto inParent = juce::Point<int>(n.pos(n.p1).x + 400, n.pos(n.p1).y + 100);
    ASSERT_TRUE(n.isEmptyHullSpace(inParent, n.parentId));

    EXPECT_TRUE(n.ctl().macroNestTargetAt(n.parentId, inChild).isEmpty()) << "not into its own child";
    EXPECT_TRUE(n.ctl().macroNestTargetAt(n.parentId, inParent).isEmpty()) << "not into itself";
    EXPECT_TRUE(n.ctl().macroNestTargetAt(n.childId, inParent).isEmpty()) << "its own parent changes nothing";
}

TEST(MacroDropIntoMacro, DraggingANestedMacroAroundItsParentOutlinesNothingAndKeepsItThere) {
    NestedMacroFixture n;
    ASSERT_TRUE(n.linked);
    juce::String outlinedMidDrag = "unset";
    dragChipBy(n.editor, n.childId, {0, 20}, [&] { outlinedMidDrag = n.editor.getMacroDragJoinId(); });
    EXPECT_TRUE(outlinedMidDrag.isEmpty());
    EXPECT_EQ(n.editor.getMacros().parentOf(n.childId), n.parentId);
}

TEST(MacroDropIntoMacro, ADropIntoANestedOpenMacroWalksDownOneLevelAtATime) {
    NestedMacroFixture n;
    ASSERT_TRUE(n.linked);
    const auto x1 = addModuleAt(n.editor, n.engine, std::make_unique<OscillatorModule>(), 300, 1700);
    const auto x2 = addModuleAt(n.editor, n.engine, std::make_unique<FilterModule>(), 700, 1700);
    n.editor.setSelectedNodes({x1, x2});
    const auto droppedId = n.ctl().groupSelectionIntoMacro(/*autoCreatePorts=*/false);
    n.editor.clearSelection();
    n.undo.clearUndoHistory();

    dragCardCentreTo(n.editor, droppedId, {n.pos(n.c1).x + 340, n.pos(n.c1).y + 150});

    EXPECT_EQ(n.editor.getMacros().parentOf(droppedId), n.childId);
    EXPECT_EQ(n.editor.getMacros().parentOf(n.childId), n.parentId);
    ASSERT_TRUE(n.undo.undo());
    EXPECT_TRUE(n.editor.getMacros().parentOf(droppedId).isEmpty());
    EXPECT_FALSE(n.undo.canUndo());
}

TEST(MacroDropIntoMacro, ANestedCardFoldsIntoItsNewParentAndComesBackFolded) {
    SystemMotionPin pin;
    DropFixture f;
    dragCardCentreTo(f.editor, f.droppedId, f.gapInTarget());
    ASSERT_EQ(f.parentOf(f.droppedId), f.targetId);
    auto& glide = f.editor.getCardGlideForTest();
    f.editor.finishCardGlideForTest();
    glide.setForceAnimateForTest(true);

    f.ctl().setMacroCollapsed(f.targetId, true);
    ASSERT_TRUE(glide.fold().isLive());
    EXPECT_FALSE(glide.fold().nestedRectFor(f.droppedId).isEmpty()) << "its card flies into the parent as one box";
    f.editor.finishCardGlideForTest();
    EXPECT_FALSE(f.ctl().getMacroCardForTest(f.droppedId)->isVisible());

    f.ctl().setMacroCollapsed(f.targetId, false);
    ASSERT_TRUE(glide.fold().isLive());
    EXPECT_TRUE(f.editor.getMacros().find(f.droppedId)->collapsed);
    EXPECT_FALSE(f.ctl().getMacroCardForTest(f.droppedId)->isVisible()) << "held until its box lands";
    glide.fold().applyAtMs(glide.fold().totalMs());
    EXPECT_TRUE(f.ctl().getMacroCardForTest(f.droppedId)->isVisible());
    f.editor.finishCardGlideForTest();
}
