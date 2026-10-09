// MacroNestedGeometryTests.cpp
// Nested macros on the canvas (docs/layout/macro-cards.md#nested-macros): the parent hull wraps its
// child, a collapsed ancestor hides the child completely while the child keeps its own collapsed
// flag, parents paint first, hit-tests pick the innermost macro (including the hull-drag
// preference's real mouse path), selection/names/bypass are transitive (the preview shows a child as one box), and
// moving a parent carries its collapsed child's bounds. Cable cases live in MacroNestedCableTests.cpp.

#include "MacroDragTestHelpers.h"
#include "MacroNestedTestFixture.h"

#include "Modules/ModuleBase.h"
#include "UI/Graph/MacroGroupController/MacroNesting.h"
#include "UI/Layout/LayoutUtil.h"

// ============================================================================
// Hull
// ============================================================================

TEST(MacroNestedHull, ParentHullContainsTheExpandedChildHullAndItsOwnMembers) {
    NestedMacroFixture f;
    ASSERT_TRUE(f.linked);
    const auto childHull = f.ctl().macroHullBounds(f.childId);
    const auto parentHull = f.ctl().macroHullBounds(f.parentId);
    ASSERT_FALSE(childHull.isEmpty());
    ASSERT_FALSE(parentHull.isEmpty());

    EXPECT_TRUE(parentHull.contains(childHull)) << parentHull.toString() << " vs child " << childHull.toString();
    EXPECT_TRUE(parentHull.contains(findComponent(f.editor, f.p1)->getBounds()));
    EXPECT_TRUE(parentHull.contains(findComponent(f.editor, f.p2)->getBounds()));
    EXPECT_GT(childHull.getY(), parentHull.getY()) << "the child hull clears the parent's chip row";
}

TEST(MacroNestedHull, ParentHullContainsTheCollapsedChildsCard) {
    NestedMacroFixture f;
    f.ctl().setMacroCollapsed(f.childId, true);

    EXPECT_TRUE(f.ctl().macroHullBounds(f.childId).isEmpty()) << "a collapsed child has no hull";
    ASSERT_TRUE(f.cardVisible(f.childId)) << "a collapsed child inside an expanded parent shows its card";
    const auto parentHull = f.ctl().macroHullBounds(f.parentId);
    EXPECT_TRUE(parentHull.contains(f.card(f.childId)->getBounds()));
}

// ============================================================================
// Effective collapse
// ============================================================================

TEST(MacroNestedCollapse, CollapsedParentHidesTheChildsHullCardAndMembers) {
    for (const bool childCollapsed : {false, true}) {
        NestedMacroFixture f;
        if (childCollapsed)
            f.ctl().setMacroCollapsed(f.childId, true);
        const auto childHullBefore = f.ctl().macroHullBounds(f.childId);
        f.ctl().setMacroCollapsed(f.parentId, true);

        EXPECT_TRUE(f.ctl().macroHullBounds(f.childId).isEmpty()) << "childCollapsed=" << childCollapsed;
        EXPECT_TRUE(f.ctl().macroChipBounds(f.childId).isEmpty());
        EXPECT_TRUE(f.ctl().macroCollapseButtonBounds(f.childId).isEmpty());
        if (!childHullBefore.isEmpty())
            EXPECT_TRUE(f.ctl().macroHullAt(childHullBefore.getCentre()).isEmpty());
        EXPECT_FALSE(f.cardVisible(f.childId)) << "only the outermost collapsed card is shown";
        EXPECT_TRUE(f.cardVisible(f.parentId));
        for (auto id : {f.c1, f.c2, f.p1, f.p2})
            EXPECT_FALSE(f.moduleVisible(id)) << "childCollapsed=" << childCollapsed;
    }
}

TEST(MacroNestedCollapse, ChildKeepsItsCollapsedFlagAndReturnsAsACardWhenTheParentExpands) {
    NestedMacroFixture f;
    f.ctl().setMacroCollapsed(f.childId, true);
    f.ctl().setMacroCollapsed(f.parentId, true);
    ASSERT_FALSE(f.cardVisible(f.childId));

    f.ctl().setMacroCollapsed(f.parentId, false);

    EXPECT_TRUE(f.editor.getMacros().find(f.childId)->collapsed);
    EXPECT_TRUE(f.cardVisible(f.childId)) << "the child comes back as a card";
    EXPECT_FALSE(f.cardVisible(f.parentId));
    EXPECT_FALSE(f.moduleVisible(f.c1)) << "the child's members stay hidden behind its card";
    EXPECT_FALSE(f.moduleVisible(f.c2));
    EXPECT_TRUE(f.moduleVisible(f.p1)) << "the parent's own members show again";
    EXPECT_TRUE(f.moduleVisible(f.p2));
}

// ============================================================================
// Paint order
// ============================================================================

TEST(MacroNestedPaintOrder, ParentsPaintBeforeTheirChildrenAndAFlatSetKeepsStoredOrder) {
    synth::MacroSet set;
    synth::Macro inner, outer, other;
    inner.id = "inner";
    inner.members = {"a"};
    outer.id = "outer";
    outer.members = {"b"};
    other.id = "other";
    other.members = {"c"};
    set.add(inner); // stored before its parent on purpose
    set.add(outer);
    set.add(other);

    auto ids = [&set] {
        std::vector<juce::String> out;
        for (const auto* m : macro_nesting::macrosParentsFirst(set))
            out.push_back(m->id);
        return out;
    };
    EXPECT_EQ(ids(), (std::vector<juce::String>{"inner", "outer", "other"})) << "flat: stored order";

    ASSERT_TRUE(set.setParent("inner", "outer"));
    EXPECT_EQ(ids(), (std::vector<juce::String>{"outer", "other", "inner"})) << "parents first";
}

// ============================================================================
// Hit-testing picks the innermost macro
// ============================================================================

TEST(MacroNestedHitTest, HullChipAndCollapseButtonPickTheInnermostVisibleMacro) {
    NestedMacroFixture f;
    const auto childHull = f.ctl().macroHullBounds(f.childId);
    const auto childChip = f.ctl().macroChipBounds(f.childId);
    const auto childButton = f.ctl().macroCollapseButtonBounds(f.childId);
    ASSERT_TRUE(f.ctl().macroHullBounds(f.parentId).contains(childHull));

    EXPECT_EQ(f.ctl().macroHullAt(childHull.getCentre()), f.childId) << "deepest hull wins over the larger parent";
    EXPECT_EQ(f.ctl().macroChipAt(childChip.getCentre()), f.childId);
    EXPECT_EQ(f.ctl().macroCollapseButtonAt(childButton.getCentre()), f.childId);
    EXPECT_EQ(f.ctl().macroChipAt(f.ctl().macroChipBounds(f.parentId).getCentre()), f.parentId);

    // Collapsing the child leaves the parent as the hit under the child's old hull.
    f.ctl().setMacroCollapsed(f.childId, true);
    EXPECT_EQ(f.ctl().macroHullAt(childHull.getCentre()), f.parentId);
    EXPECT_TRUE(f.ctl().macroChipAt(childChip.getCentre()) != f.childId);
}

namespace {
// Between the two members of a row, vertically level with them: empty space inside that row's hull.
juce::Point<int> gapBetween(GraphEditor& editor, NodeID left, NodeID right) {
    const auto l = findComponent(editor, left)->getBounds();
    const auto r = findComponent(editor, right)->getBounds();
    return {(l.getRight() + r.getX()) / 2, l.getY() + 20};
}
} // namespace

TEST(MacroNestedHitTest, HullDragPreferenceMovesOnlyTheInnerMacroFromItsHull) {
    NestedMacroFixture f;
    f.editor.setMoveMacroOnHullDragEnabled(true);
    const auto press = gapBetween(f.editor, f.c1, f.c2);
    ASSERT_TRUE(f.isEmptyHullSpace(press, f.childId)) << "the press must land on the child hull's own empty space";
    const auto c1 = f.pos(f.c1), c2 = f.pos(f.c2), p1 = f.pos(f.p1), p2 = f.pos(f.p2);
    const juce::Point<int> delta(120, 80);

    f.editor.mouseDown(makeCanvasMouseEvent(f.editor, press));
    f.editor.mouseDrag(makeCanvasMouseEvent(f.editor, press + delta));
    f.editor.mouseUp(makeCanvasMouseEvent(f.editor, press + delta));

    EXPECT_NE(f.pos(f.c1), c1) << "the inner macro moved";
    EXPECT_EQ(f.pos(f.c2) - f.pos(f.c1), c2 - c1) << "as one rigid body";
    EXPECT_EQ(f.pos(f.p1), p1) << "the parent's own members stayed put";
    EXPECT_EQ(f.pos(f.p2), p2);
}

TEST(MacroNestedHitTest, HullDragPreferenceFromParentOnlySpaceMovesEverythingInside) {
    NestedMacroFixture f;
    f.editor.setMoveMacroOnHullDragEnabled(true);
    const auto press = gapBetween(f.editor, f.p1, f.p2);
    ASSERT_TRUE(f.isEmptyHullSpace(press, f.parentId));
    const auto c1 = f.pos(f.c1), p1 = f.pos(f.p1), p2 = f.pos(f.p2);
    const juce::Point<int> delta(120, 80);

    f.editor.mouseDown(makeCanvasMouseEvent(f.editor, press));
    f.editor.mouseDrag(makeCanvasMouseEvent(f.editor, press + delta));
    f.editor.mouseUp(makeCanvasMouseEvent(f.editor, press + delta));

    const auto moved = f.pos(f.p1) - p1;
    EXPECT_NE(moved, juce::Point<int>());
    EXPECT_EQ(f.pos(f.p2) - p2, moved);
    EXPECT_EQ(f.pos(f.c1) - c1, moved) << "the nested child's members ride along";
}

// ============================================================================
// Selection, names and bypass are transitive; the card preview shows a child as one box
// ============================================================================

TEST(MacroNestedSelection, SelectingTheParentSelectsEveryNestedMemberAndReadsSelected) {
    NestedMacroFixture f;
    f.ctl().selectMacro(f.parentId, false);
    EXPECT_EQ(f.editor.getSelectionCount(), 4);
    EXPECT_TRUE(f.ctl().isMacroSelected(f.parentId));
    EXPECT_FALSE(f.ctl().isMacroSelected(f.childId)) << "the child alone is a smaller set";

    f.ctl().selectMacro(f.childId, false);
    EXPECT_EQ(f.editor.getSelectionCount(), 2);
    EXPECT_TRUE(f.ctl().isMacroSelected(f.childId));
    EXPECT_FALSE(f.ctl().isMacroSelected(f.parentId));
}

TEST(MacroNestedSelection, ParentCardPreviewsTheChildAsOneBoxAndNamesIncludeNestedModules) {
    NestedMacroFixture f;
    EXPECT_EQ(f.ctl().macroMemberPreviews(f.parentId).size(), 3u) << "two modules and one box for the child";
    EXPECT_EQ(f.ctl().macroMemberNames(f.parentId).size(), 4);
    EXPECT_EQ(f.ctl().macroMemberPreviews(f.childId).size(), 2u);
}

TEST(MacroNestedSelection, BypassingTheParentBypassesNestedMembers) {
    NestedMacroFixture f;
    f.ctl().setMacroBypassed(f.parentId, true);
    for (auto id : {f.c1, f.c2, f.p1, f.p2}) {
        auto* mb = dynamic_cast<ModuleBase*>(f.engine.getGraph().getNodeForId(id)->getProcessor());
        ASSERT_NE(mb, nullptr);
        EXPECT_TRUE(mb->isBypassed());
    }
    EXPECT_EQ(f.ctl().macroBypassState(f.parentId), MacroGroupController::MacroToggleState::AllOn);
}

// ============================================================================
// Moving a parent carries its collapsed child
// ============================================================================

TEST(MacroNestedDrag, DraggingTheParentCardShiftsTheCollapsedChildsBoundsAndUndoRestoresThem) {
    NestedMacroFixture f;
    f.ctl().setMacroCollapsed(f.childId, true);
    f.ctl().setMacroCollapsed(f.parentId, true);
    f.undo.clearUndoHistory();
    const auto childBoundsBefore = f.editor.getMacros().find(f.childId)->bounds;
    const auto c1Before = f.pos(f.c1);
    auto* card = f.card(f.parentId);
    ASSERT_NE(card, nullptr);
    ASSERT_TRUE(card->isVisible());

    // A real press/drag/release on the parent's card body (its centre: off the title row, jacks and buttons).
    const auto press = card->getLocalBounds().getCentre();
    const juce::Point<int> delta(200, 120);
    card->mouseDown(makeCanvasMouseEvent(*card, press));
    card->mouseDrag(makeCanvasMouseEvent(*card, press + delta));
    card->mouseUp(makeCanvasMouseEvent(*card, press + delta));

    const auto memberDelta = f.pos(f.c1) - c1Before;
    ASSERT_NE(memberDelta, juce::Point<int>()) << "the card drag moved the hidden members";
    const auto childBounds = f.editor.getMacros().find(f.childId)->bounds;
    EXPECT_EQ(childBounds.getPosition(), childBoundsBefore.getPosition() + memberDelta)
        << "the collapsed child's bounds moved with its members";

    f.ctl().setMacroCollapsed(f.parentId, false);
    EXPECT_EQ(f.card(f.childId)->getPosition(), childBounds.getPosition()) << "it reappears where it was carried";

    f.undo.undo(); // the expand
    f.undo.undo(); // the card drag
    EXPECT_EQ(f.editor.getMacros().find(f.childId)->bounds, childBoundsBefore);
}

TEST(MacroNestedDrag, DraggingTheParentChipCarriesTheCollapsedChildCardLiveAndOnDrop) {
    NestedMacroFixture f;
    f.ctl().setMacroCollapsed(f.childId, true);
    f.undo.clearUndoHistory();
    const auto childBoundsBefore = f.editor.getMacros().find(f.childId)->bounds;
    const auto cardBefore = f.card(f.childId)->getPosition();
    const auto p1Before = f.pos(f.p1);
    const auto chip = f.ctl().macroChipBounds(f.parentId).getCentre();
    const juce::Point<int> delta(120, 80);

    f.editor.mouseDown(makeCanvasMouseEvent(f.editor, chip));
    f.editor.mouseDrag(makeCanvasMouseEvent(f.editor, chip + delta));
    EXPECT_EQ(f.card(f.childId)->getPosition(), cardBefore + delta) << "the child card follows mid-drag";
    f.editor.mouseUp(makeCanvasMouseEvent(f.editor, chip + delta));

    const auto memberDelta = f.pos(f.p1) - p1Before;
    ASSERT_NE(memberDelta, juce::Point<int>());
    EXPECT_EQ(f.editor.getMacros().find(f.childId)->bounds.getPosition(),
              childBoundsBefore.getPosition() + memberDelta);
    EXPECT_EQ(f.card(f.childId)->getPosition(), childBoundsBefore.getPosition() + memberDelta);
    EXPECT_TRUE(f.ctl().macroHullBounds(f.parentId).contains(f.card(f.childId)->getBounds()));

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_FALSE(f.undo.canUndo()) << "the chip drag is one undo step";
    EXPECT_EQ(f.editor.getMacros().find(f.childId)->bounds, childBoundsBefore);
    EXPECT_EQ(f.pos(f.p1), p1Before);
}

TEST(MacroNestedDrag, DraggingAMemberOfTheSelectedParentCarriesTheCollapsedChildAndOneUndoRestoresIt) {
    NestedMacroFixture f;
    f.ctl().setMacroCollapsed(f.childId, true);
    f.ctl().selectMacro(f.parentId, false);
    f.undo.clearUndoHistory();
    const auto childBoundsBefore = f.editor.getMacros().find(f.childId)->bounds;
    const auto p1Before = f.pos(f.p1);

    dragBodyBy(*findComponent(f.editor, f.p1), {160, 60}, kPlainClick);

    const auto memberDelta = f.pos(f.p1) - p1Before;
    ASSERT_NE(memberDelta, juce::Point<int>());
    EXPECT_EQ(f.editor.getMacros().find(f.childId)->bounds.getPosition(),
              childBoundsBefore.getPosition() + memberDelta);

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_FALSE(f.undo.canUndo()) << "one undo step";
    EXPECT_EQ(f.editor.getMacros().find(f.childId)->bounds, childBoundsBefore);
    EXPECT_EQ(f.pos(f.p1), p1Before);
}

TEST(MacroNestedDrag, DraggingTheNestedChildCardOnItsOwnLeavesTheParentAlone) {
    NestedMacroFixture f;
    f.ctl().setMacroCollapsed(f.childId, true);
    const auto p1Before = f.pos(f.p1);
    auto* card = f.card(f.childId);
    ASSERT_TRUE(card->isVisible());
    const auto cardStart = card->getPosition();
    const auto c1Before = f.pos(f.c1);
    const auto press = card->getLocalBounds().getCentre();
    const juce::Point<int> delta(0, 200);

    card->mouseDown(makeCanvasMouseEvent(*card, press));
    card->mouseDrag(makeCanvasMouseEvent(*card, press + delta));
    card->mouseUp(makeCanvasMouseEvent(*card, press + delta));

    EXPECT_NE(f.pos(f.c1), c1Before) << "the child's own members moved";
    EXPECT_EQ(f.pos(f.p1), p1Before) << "the parent's members did not";
    EXPECT_EQ(f.editor.getMacros().find(f.childId)->bounds.getPosition(), synth::LayoutUtil::snap(cardStart + delta))
        << "the card's own drag wrote its bounds, exactly as for a top-level card";
}
