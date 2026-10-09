// InsertGapMacroTests.cpp
//
// Inserting between cards inside an OPEN macro: the module joins the macro, the members after it slide aside, the
// macro's border grows to hold them (and pushes what it then covers outside, in chain), and everything comes back on
// undo, on moving away, and when the inserted module is deleted
// (docs/macros/macros.md#adding-a-module-between-others-inside-an-open-macro). Includes the whole Osc / Filter / VCA
// story: delete the Filter (the gap closes), then drop a new module between Osc and VCA.

#include "InsertGapTestFixture.h"

#include "UI/Graph/InsertGap/InsertGapKeyboard.h"

using namespace insert_gap_test;

namespace {
struct OscFilterVca {
    Canvas c;
    NodeID osc, flt, vca;
    juce::String macroId;

    OscFilterVca() {
        osc = c.add(std::make_unique<OscillatorModule>(), 400, 3000);
        flt = c.add(std::make_unique<FilterModule>(), 400, 3600);
        vca = c.add(std::make_unique<VCAModule>(), 400, 4200);
        int x = 400;
        for (auto id : {osc, flt, vca}) {
            c.place(id, x, 400);
            x += c.rect(id).getWidth() + 40;
        }
        macroId = c.openMacro({osc, flt, vca});
    }
};
} // namespace

TEST(InsertGapMacro, OscFilterVcaDeleteTheFilterThenDropANewModuleBetweenOscAndVca) {
    OscFilterVca s;
    auto& c = s.c;
    const auto oscAt = c.rect(s.osc), filterAt = c.rect(s.flt);
    const auto hullFull = c.hull(s.macroId);

    // Delete the Filter: the gap closes, VCA moves into its place and the border shrinks.
    c.editor.setSelectedNodes({s.flt});
    c.editor.deleteSelection();
    c.editor.finishCardGlideForTest();
    ASSERT_EQ(c.rect(s.vca).getPosition(), filterAt.getPosition());
    const auto vcaClosed = c.rect(s.vca);
    const auto hullClosed = c.hull(s.macroId);
    ASSERT_LT(hullClosed.getWidth(), hullFull.getWidth());

    // Drag a new module between Osc and VCA: the gap opens live, VCA slides right, the border grows.
    const auto p = between(oscAt, vcaClosed);
    c.libraryEnter("Filter", p);
    ASSERT_TRUE(c.gap().isOpen());
    EXPECT_EQ(c.editor.getMacroDragJoinId(), s.macroId) << "the macro is the drop target";
    const int shift = Canvas::shiftFor("Filter");
    EXPECT_EQ(c.rect(s.osc), oscAt);
    EXPECT_EQ(c.rect(s.vca), vcaClosed.translated(shift, 0));
    EXPECT_EQ(c.hull(s.macroId).getWidth(), hullClosed.getWidth() + shift) << "the border grows to fit";

    const auto added = c.libraryDrop("Filter", p);
    c.editor.finishCardGlideForTest();
    ASSERT_NE(added.uid, 0u);
    EXPECT_TRUE(c.isMember(s.macroId, added)) << "the module joins the macro";
    EXPECT_EQ(c.rect(added).getPosition(), vcaClosed.getPosition()) << "it lands between Osc and VCA";
    EXPECT_EQ(c.rect(s.vca), vcaClosed.translated(shift, 0));
    EXPECT_EQ(c.rect(s.osc), oscAt);
    const auto inserted = c.allRects();
    const auto hullInserted = c.hull(s.macroId);

    // One undo takes the module out and VCA back; a second brings the deleted Filter back.
    ASSERT_TRUE(c.undo.undo());
    c.editor.finishCardGlideForTest();
    EXPECT_EQ(c.card(added), nullptr);
    EXPECT_EQ(c.rect(s.vca), vcaClosed);
    EXPECT_EQ(c.hull(s.macroId), hullClosed);
    ASSERT_TRUE(c.undo.redo());
    c.editor.finishCardGlideForTest();
    EXPECT_EQ(c.allRects(), inserted);
    EXPECT_EQ(c.hull(s.macroId), hullInserted);
    ASSERT_TRUE(c.undo.undo());
    ASSERT_TRUE(c.undo.undo());
    c.editor.finishCardGlideForTest();
    ASSERT_NE(c.card(s.flt), nullptr);
    EXPECT_EQ(c.rect(s.flt), filterAt);
    EXPECT_EQ(c.hull(s.macroId), hullFull);
}

TEST(InsertGapMacro, TheBorderGrowsWhileHoveringAndShrinksBackWhenTheDragLeaves) {
    OscFilterVca s;
    auto& c = s.c;
    const auto hull = c.hull(s.macroId);
    const auto before = c.allRects();
    c.libraryEnter("VCA", between(c.rect(s.osc), c.rect(s.flt)));
    ASSERT_TRUE(c.gap().isOpen());
    EXPECT_GT(c.hull(s.macroId).getWidth(), hull.getWidth());
    c.libraryMove("VCA", {400, 2400});
    EXPECT_EQ(c.hull(s.macroId), hull);
    EXPECT_EQ(c.allRects(), before);
    c.libraryExit("VCA", {400, 2400});
}

TEST(InsertGapMacro, DeletingTheInsertedMemberBringsTheOthersHomeAndClosesNothingElse) {
    OscFilterVca s;
    auto& c = s.c;
    const auto before = c.allRects();
    const auto hull = c.hull(s.macroId);
    const auto added = c.dropBetween("VCA", between(c.rect(s.osc), c.rect(s.flt)));
    c.editor.finishCardGlideForTest();
    ASSERT_TRUE(c.isMember(s.macroId, added));

    c.editor.setSelectedNodes({added});
    c.editor.deleteSelection();
    c.editor.finishCardGlideForTest();
    EXPECT_EQ(c.allRects(), before) << "every member is exactly where it was before the insert";
    EXPECT_EQ(c.hull(s.macroId), hull);
}

TEST(InsertGapMacro, TheGrownBorderPushesTheCardsAheadOfItOutside) {
    OscFilterVca s;
    auto& c = s.c;
    const auto hull = c.hull(s.macroId);
    const auto outside = c.filter(hull.getRight() + 40, hull.getY() + 20);
    const auto behind = c.filter(hull.getX(), hull.getBottom() + 400); // below the macro: never pushed
    c.editor.finishCardGlideForTest();
    const auto outsideAt = c.rect(outside), behindAt = c.rect(behind);

    c.libraryEnter("VCA", between(c.rect(s.osc), c.rect(s.flt)));
    ASSERT_TRUE(c.gap().isOpen());
    const auto grown = c.hull(s.macroId);
    EXPECT_GT(c.rect(outside).getX(), outsideAt.getX()) << "the border pushed the card it now covers";
    EXPECT_GE(c.rect(outside).getX(), grown.getRight() + synth::LayoutUtil::kCollisionGap);
    EXPECT_EQ(c.rect(outside).getY(), outsideAt.getY());
    EXPECT_EQ(c.rect(behind), behindAt);

    c.libraryExit("VCA", {0, 0});
    EXPECT_EQ(c.rect(outside), outsideAt);
    EXPECT_EQ(c.hull(s.macroId), hull);

    // Dropped, deleted: the outside card comes home too.
    const auto added = c.dropBetween("VCA", between(c.rect(s.osc), c.rect(s.flt)));
    c.editor.finishCardGlideForTest();
    c.editor.setSelectedNodes({added});
    c.editor.deleteSelection();
    c.editor.finishCardGlideForTest();
    EXPECT_EQ(c.rect(outside), outsideAt);
}

TEST(InsertGapMacro, ANestedMacroIsARowMemberAndMovesWhole) {
    Canvas c;
    const auto a = c.filter(400, 400);
    const auto b = c.filter(760, 400);
    const auto innerA = c.filter(1120, 400);
    const auto innerB = c.filter(1120, 1000);
    c.editor.finishCardGlideForTest();
    // The child macro (b's neighbour), collapsed, inside the parent with a and b.
    const auto child = c.openMacro({innerA, innerB});
    c.macros().setMacroCollapsed(child, true);
    c.editor.finishCardGlideForTest();
    c.editor.setSelectedNodes({a, b, innerA, innerB});
    const auto parent = c.macros().groupSelectionIntoMacro(false);
    c.macros().setMacroCollapsed(parent, false);
    c.editor.clearSelection();
    c.editor.finishCardGlideForTest();
    ASSERT_EQ(c.editor.getMacros().find(child)->parentId, parent);
    const auto childCard = c.editor.getMacros().find(child)->bounds;
    const auto hiddenA = c.stored(innerA), hiddenB = c.stored(innerB);

    c.dropBetween("VCA", between(c.rect(a), c.rect(b)));
    c.editor.finishCardGlideForTest();
    const int shift = Canvas::shiftFor("VCA", /*spacing=*/80); // a and b are 80 apart, the most an insert copies
    EXPECT_EQ(c.editor.getMacros().find(child)->bounds, childCard.translated(shift, 0)) << "the card moves";
    EXPECT_EQ(c.stored(innerA), hiddenA + juce::Point<int>(shift, 0)) << "with its hidden members";
    EXPECT_EQ(c.stored(innerB), hiddenB + juce::Point<int>(shift, 0));

    ASSERT_TRUE(c.undo.undo());
    c.editor.finishCardGlideForTest();
    EXPECT_EQ(c.editor.getMacros().find(child)->bounds, childCard);
    EXPECT_EQ(c.stored(innerA), hiddenA);
}

TEST(InsertGapMacro, DraggingAMemberWithinItsMacroInsertsAmongTheMembers) {
    OscFilterVca s;
    auto& c = s.c;
    const auto before = c.allRects();
    c.press(s.vca);
    c.dragCentreTo(s.vca, between(before.at(s.osc.uid), before.at(s.flt.uid)));
    ASSERT_TRUE(c.gap().isOpen());
    EXPECT_EQ(c.gap().container(), s.macroId);
    c.release(s.vca);
    c.editor.finishCardGlideForTest();
    EXPECT_TRUE(c.isMember(s.macroId, s.vca));
    EXPECT_EQ(c.rect(s.vca).getPosition(), before.at(s.flt.uid).getPosition());
    EXPECT_EQ(c.rect(s.flt).getX(),
              before.at(s.flt.uid).getX() + synth::insert_gap::snapUp(before.at(s.vca.uid).getWidth() + 40));
    ASSERT_TRUE(c.undo.undo());
    c.editor.finishCardGlideForTest();
    EXPECT_EQ(c.allRects(), before);
}

TEST(InsertGapMacro, TheKeyboardInsertJoinsTheSelectedMembersMacro) {
    OscFilterVca s;
    auto& c = s.c;
    const auto before = c.allRects();
    c.editor.setSelectedNodes({s.osc});
    const auto ids = c.nodeIds();
    ASSERT_TRUE(synth::insertModuleAfterSelectedCard(c.editor, "LFO"));
    c.editor.finishCardGlideForTest();
    NodeID added;
    for (auto* n : c.engine.getGraph().getNodes())
        if (ids.count(n->nodeID.uid) == 0 && !c.macros().nodeIsMacroPort(n->nodeID))
            added = n->nodeID;
    ASSERT_NE(added.uid, 0u);
    EXPECT_TRUE(c.isMember(s.macroId, added));
    EXPECT_EQ(c.rect(added).getPosition(), before.at(s.flt.uid).getPosition());
    EXPECT_GT(c.rect(s.flt).getX(), before.at(s.flt.uid).getX());
}
