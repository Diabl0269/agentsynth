// MacroNestedMembershipTests.cpp
// Editing nested macros (docs/macros/menu-and-membership.md#nested-macros): ungroup a parent promotes its
// children, delete a parent deletes the whole subtree, the collapse toggle resolves a hidden module to its
// outermost collapsed macro, remove-from-macro moves up one level, and a reparent drag transfers between
// levels or leaves every level in one gesture, all through real handlers with undo and redo. Nothing in the UI
// creates a nested macro yet, so MacroNestedTestFixture.h links two flat macros with MacroSet::setParent.

#include "MacroDragTestHelpers.h"
#include "MacroNestedTestFixture.h"

namespace {

juce::String ownerOf(NestedMacroFixture& f, NodeID id) {
    const auto* macro = f.editor.getMacros().findByMember(uuidOf(f.engine, id));
    return macro != nullptr ? macro->id : juce::String();
}

// Delta that moves `comp`'s centre to `target`.
juce::Point<int> deltaTo(ModuleComponent& comp, juce::Point<int> target) {
    return target - comp.getBounds().getCentre();
}

// A point inside the parent's hull that is in neither the child's hull nor any module: the gap between the rows.
constexpr juce::Point<int> kParentOnlySpace{350, 700};
// A point far outside every hull.
constexpr juce::Point<int> kOutsideSpace{1800, 100};

} // namespace

// ---------------------------------------------------------------------------------------------
// Ungroup
// ---------------------------------------------------------------------------------------------

TEST(MacroNestedMembership, UngroupParentPromotesItsMembersAndChildToTopLevel) {
    NestedMacroFixture f;
    ASSERT_TRUE(f.linked);
    f.editor.setSelectedNodes({f.p1});
    f.ctl().ungroupSelection();

    EXPECT_EQ(f.editor.getMacros().find(f.parentId), nullptr) << "the parent is dissolved";
    const auto* child = f.editor.getMacros().find(f.childId);
    ASSERT_NE(child, nullptr) << "its child macro survives";
    EXPECT_TRUE(child->parentId.isEmpty()) << "and is promoted to top level";
    EXPECT_TRUE(child->hasMember(uuidOf(f.engine, f.c1)));
    EXPECT_TRUE(ownerOf(f, f.p1).isEmpty());
    EXPECT_TRUE(ownerOf(f, f.p2).isEmpty());
    EXPECT_EQ(f.editor.getSelection().size(), 4) << "everything the parent held is re-selected";
}

TEST(MacroNestedMembership, SelectingTheWholeParentUngroupsOnlyTheParent) {
    NestedMacroFixture f;
    f.ctl().selectMacro(f.parentId, false);
    f.ctl().ungroupSelection();

    EXPECT_EQ(f.editor.getMacros().find(f.parentId), nullptr);
    ASSERT_NE(f.editor.getMacros().find(f.childId), nullptr) << "a child is not ungrouped with its parent";
}

TEST(MacroNestedMembership, UngroupChildMovesItsMembersIntoTheParent) {
    NestedMacroFixture f;
    f.editor.setSelectedNodes({f.c1});
    f.ctl().ungroupSelection();

    EXPECT_EQ(f.editor.getMacros().find(f.childId), nullptr);
    EXPECT_EQ(ownerOf(f, f.c1), f.parentId);
    EXPECT_EQ(ownerOf(f, f.c2), f.parentId);
    EXPECT_EQ(ownerOf(f, f.p1), f.parentId);
}

TEST(MacroNestedMembership, UngroupParentUndoAndRedoRestoreTheHierarchy) {
    NestedMacroFixture f;
    f.editor.setSelectedNodes({f.p1});
    f.ctl().ungroupSelection();
    ASSERT_EQ(f.editor.getMacros().find(f.parentId), nullptr);

    ASSERT_TRUE(f.undo.undo());
    ASSERT_NE(f.editor.getMacros().find(f.parentId), nullptr);
    ASSERT_NE(f.editor.getMacros().find(f.childId), nullptr);
    EXPECT_EQ(f.editor.getMacros().parentOf(f.childId), f.parentId);
    EXPECT_EQ(ownerOf(f, f.p1), f.parentId);
    EXPECT_EQ(ownerOf(f, f.c1), f.childId);

    ASSERT_TRUE(f.undo.redo());
    EXPECT_EQ(f.editor.getMacros().find(f.parentId), nullptr);
    ASSERT_NE(f.editor.getMacros().find(f.childId), nullptr);
    EXPECT_TRUE(f.editor.getMacros().parentOf(f.childId).isEmpty());
    EXPECT_TRUE(ownerOf(f, f.p1).isEmpty());
    EXPECT_EQ(ownerOf(f, f.c1), f.childId);
}

// ---------------------------------------------------------------------------------------------
// Delete
// ---------------------------------------------------------------------------------------------

TEST(MacroNestedMembership, DeleteParentDeletesTheChildAndAllNestedModules) {
    NestedMacroFixture f;
    const auto totalBefore = f.engine.getGraph().getNumNodes();
    f.ctl().deleteMacroAndMembers(f.parentId);

    EXPECT_EQ(f.engine.getGraph().getNumNodes(), totalBefore - 4);
    EXPECT_TRUE(f.editor.getMacros().empty()) << "both macros are gone with their modules";
}

TEST(MacroNestedMembership, DeleteParentUndoAndRedoRestoreAndRemoveTheSubtree) {
    NestedMacroFixture f;
    const auto totalBefore = f.engine.getGraph().getNumNodes();
    f.ctl().deleteMacroAndMembers(f.parentId);
    ASSERT_TRUE(f.editor.getMacros().empty());

    ASSERT_TRUE(f.undo.undo());
    EXPECT_EQ(f.engine.getGraph().getNumNodes(), totalBefore);
    ASSERT_NE(f.editor.getMacros().find(f.parentId), nullptr);
    ASSERT_NE(f.editor.getMacros().find(f.childId), nullptr);
    EXPECT_EQ(f.editor.getMacros().parentOf(f.childId), f.parentId);

    ASSERT_TRUE(f.undo.redo());
    EXPECT_EQ(f.engine.getGraph().getNumNodes(), totalBefore - 4);
    EXPECT_TRUE(f.editor.getMacros().empty());
}

// ---------------------------------------------------------------------------------------------
// Collapse toggle
// ---------------------------------------------------------------------------------------------

TEST(MacroNestedMembership, ToggleResolvesAHiddenModuleToItsOutermostCollapsedMacro) {
    NestedMacroFixture f;
    f.ctl().setMacroCollapsed(f.parentId, true);
    ASSERT_TRUE(f.editor.getMacros().isEffectivelyCollapsed(f.childId));

    f.editor.setSelectedNodes({f.c1});
    f.ctl().toggleSelectionMacrosCollapsed();

    EXPECT_FALSE(f.editor.getMacros().find(f.parentId)->collapsed) << "the card the user sees is what toggles";
    EXPECT_FALSE(f.editor.getMacros().find(f.childId)->collapsed) << "the child's own flag is untouched";
    EXPECT_TRUE(f.moduleVisible(f.c1));
}

// ---------------------------------------------------------------------------------------------
// Remove from macro
// ---------------------------------------------------------------------------------------------

TEST(MacroNestedMembership, RemoveFromChildMovesTheModuleUpOneLevelOnly) {
    NestedMacroFixture f;
    f.ctl().removeNodeFromMacro(f.c1);
    EXPECT_EQ(ownerOf(f, f.c1), f.parentId) << "a nested macro's member moves into its parent";
    EXPECT_EQ(ownerOf(f, f.c2), f.childId);

    f.ctl().removeNodeFromMacro(f.c1);
    EXPECT_TRUE(ownerOf(f, f.c1).isEmpty()) << "from a top-level macro it leaves entirely, as before";
}

TEST(MacroNestedMembership, RemovingTheLastMemberOfAChildDissolvesItIntoTheParent) {
    NestedMacroFixture f;
    f.ctl().removeNodeFromMacro(f.c1);
    f.ctl().removeNodeFromMacro(f.c2);

    EXPECT_EQ(f.editor.getMacros().find(f.childId), nullptr) << "no direct members and no children: dissolved";
    EXPECT_EQ(ownerOf(f, f.c1), f.parentId);
    EXPECT_EQ(ownerOf(f, f.c2), f.parentId);
}

// ---------------------------------------------------------------------------------------------
// Drag in and out with real mouse events
// ---------------------------------------------------------------------------------------------

TEST(MacroNestedDrag, DraggingAChildsMemberIntoTheParentsSpaceTransfersItUpOneLevel) {
    NestedMacroFixture f;
    auto* comp = findComponent(f.editor, f.c1);
    ASSERT_NE(comp, nullptr);

    dragBodyBy(*comp, deltaTo(*comp, kParentOnlySpace), kCmdClick, [&] {
        EXPECT_EQ(f.editor.getMacroDragLeaveId(), f.childId);
        EXPECT_EQ(f.editor.getMacroDragJoinId(), f.parentId);
    });

    EXPECT_EQ(ownerOf(f, f.c1), f.parentId);
    EXPECT_EQ(ownerOf(f, f.c2), f.childId);
    EXPECT_EQ(f.editor.getMacros().parentOf(f.childId), f.parentId);
    EXPECT_FALSE(f.editor.hasMacroDragCandidate());

    ASSERT_TRUE(f.undo.undo());
    EXPECT_EQ(ownerOf(f, f.c1), f.childId) << "one undo restores the membership";
    ASSERT_TRUE(f.undo.redo());
    EXPECT_EQ(ownerOf(f, f.c1), f.parentId);
}

TEST(MacroNestedDrag, DraggingAChildsMemberOutOfEveryHullLeavesAllLevelsInOneGesture) {
    NestedMacroFixture f;
    auto* comp = findComponent(f.editor, f.c1);
    ASSERT_NE(comp, nullptr);

    dragBodyBy(*comp, deltaTo(*comp, kOutsideSpace), kCmdClick, [&] {
        EXPECT_EQ(f.editor.getMacroDragLeaveId(), f.childId);
        EXPECT_TRUE(f.editor.getMacroDragJoinId().isEmpty()) << "the parent's hull no longer chases the module";
    });

    EXPECT_TRUE(ownerOf(f, f.c1).isEmpty()) << "out of the child AND the parent";
    EXPECT_EQ(ownerOf(f, f.c2), f.childId);
    EXPECT_EQ(f.editor.getMacros().parentOf(f.childId), f.parentId);

    ASSERT_TRUE(f.undo.undo());
    EXPECT_EQ(ownerOf(f, f.c1), f.childId) << "one undo restores the whole gesture";
}

TEST(MacroNestedDrag, DraggingAParentsOwnMemberIntoTheChildHullTransfersItDown) {
    NestedMacroFixture f;
    auto* comp = findComponent(f.editor, f.p1);
    ASSERT_NE(comp, nullptr);
    const auto childCentre = f.ctl().macroHullBounds(f.childId).getCentre();

    dragBodyBy(*comp, deltaTo(*comp, childCentre), kCmdClick, [&] {
        EXPECT_EQ(f.editor.getMacroDragLeaveId(), f.parentId);
        EXPECT_EQ(f.editor.getMacroDragJoinId(), f.childId);
    });

    EXPECT_EQ(ownerOf(f, f.p1), f.childId);
    EXPECT_EQ(ownerOf(f, f.p2), f.parentId);
    EXPECT_EQ(f.editor.getMacros().parentOf(f.childId), f.parentId) << "the parent survives; p1 is still inside it";
}
