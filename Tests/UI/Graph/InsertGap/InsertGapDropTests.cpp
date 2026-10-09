// InsertGapDropTests.cpp
//
// A module dropped into the gap lands between the two cards; the whole insert (the module and every card it pushed)
// is one undo step, undo and redo put every card back exactly over repeated cycles, and deleting the inserted module
// later brings the pushed cards home (docs/layout/layout.md#making-room-for-a-module-dropped-between-others). Library
// drops, canvas card drops and the keyboard insert, on a real canvas with a real AppUndoManager.

#include "InsertGapTestFixture.h"

#include "UI/Graph/InsertGap/InsertGapKeyboard.h"

using namespace insert_gap_test;

namespace {
void expectRow(Canvas& c, const std::vector<NodeID>& row, const std::map<juce::uint32, Rect>& before, int from,
               int shift, const char* when) {
    for (int i = 0; i < (int)row.size(); ++i) {
        const auto expected = before.at(row[(size_t)i].uid).translated(i >= from ? shift : 0, 0);
        EXPECT_EQ(c.rect(row[(size_t)i]), expected) << when << ": card " << i;
        EXPECT_EQ(c.stored(row[(size_t)i]), expected.getPosition()) << when << ": stored position of card " << i;
    }
}
} // namespace

// Rows of 2, 3, 5 and 10: in front of the first card, in the middle, and in the last gap. Each drop is undone and
// redone three times and must land on exactly the same rects every time.
class InsertGapDrop : public ::testing::TestWithParam<int> {};

TEST_P(InsertGapDrop, LandsInTheGapAsOneUndoStepAndCyclesExactly) {
    const int n = GetParam();
    for (int k : {0, n / 2, n - 1}) {
        SCOPED_TRACE("n " + std::to_string(n) + " in front of card " + std::to_string(k));
        Canvas c;
        const auto row = c.filterRow(n, 200, 400);
        const auto before = c.allRects();
        const auto& anchor = before.at(row[(size_t)k].uid);
        const auto p = k == 0 ? juce::Point<int>(anchor.getX() - 20, anchor.getCentreY())
                              : between(before.at(row[(size_t)k - 1].uid), anchor);
        const int shift = Canvas::shiftFor("VCA");

        const auto added = c.dropBetween("VCA", p);
        ASSERT_NE(added.uid, 0u);
        c.editor.finishCardGlideForTest();
        EXPECT_FALSE(c.gap().isOpen());
        EXPECT_EQ(c.rect(added).getPosition(), anchor.getPosition()) << "the module takes the anchor's place";
        expectRow(c, row, before, k, shift, "after the drop");
        const auto inserted = c.allRects();

        for (int cycle = 0; cycle < 3; ++cycle) {
            ASSERT_TRUE(c.undo.undo());
            c.editor.finishCardGlideForTest();
            EXPECT_EQ(c.card(added), nullptr) << "one undo removes the module";
            expectRow(c, row, before, k, 0, "after undo");
            EXPECT_FALSE(c.undo.canUndo()) << "the module and the pushes were one step";
            ASSERT_TRUE(c.undo.redo());
            c.editor.finishCardGlideForTest();
            EXPECT_EQ(c.allRects(), inserted) << "redo, cycle " << cycle;
        }
    }
}

INSTANTIATE_TEST_SUITE_P(Sizes, InsertGapDrop, ::testing::Values(2, 3, 5, 10));

TEST(InsertGapDrop, ADropWithNoGapPlacesAsBeforeAndMovesNothing) {
    Canvas c;
    const auto row = c.filterRow(3);
    const auto before = c.allRects();
    const auto added = c.dropBetween("VCA", {500, 2400});
    ASSERT_NE(added.uid, 0u);
    expectRow(c, row, before, 99, 0, "after a drop in open space");
}

TEST(InsertGapDrop, DeletingTheInsertedModuleBringsTheCardsHome) {
    Canvas c;
    const auto row = c.filterRow(4);
    const auto before = c.allRects();
    const auto added = c.dropBetween("VCA", between(c.rect(row[1]), c.rect(row[2])));
    c.editor.finishCardGlideForTest();
    ASSERT_EQ(c.rect(row[2]).getX(), before.at(row[2].uid).getX() + Canvas::shiftFor("VCA"));

    c.editor.setSelectedNodes({added});
    c.editor.deleteSelection();
    c.editor.finishCardGlideForTest();
    expectRow(c, row, before, 99, 0, "after deleting the inserted module");

    ASSERT_TRUE(c.undo.undo()); // the delete
    c.editor.finishCardGlideForTest();
    expectRow(c, row, before, 2, Canvas::shiftFor("VCA"), "after undoing the delete");
}

TEST(InsertGapDrop, ACardTheUserMovedSincePushIsNotPulledBack) {
    Canvas c;
    const auto row = c.filterRow(3);
    const auto added = c.dropBetween("VCA", between(c.rect(row[0]), c.rect(row[1])));
    c.editor.finishCardGlideForTest();
    const auto moved = c.rect(row[2]).translated(0, 900);
    c.place(row[2], moved.getX(), moved.getY());

    c.editor.setSelectedNodes({added});
    c.editor.deleteSelection();
    c.editor.finishCardGlideForTest();
    EXPECT_EQ(c.rect(row[2]), moved) << "it is the user's now";
}

TEST(InsertGapDrop, ACanvasCardDroppedInTheGapIsOneUndoStep) {
    Canvas c;
    const auto row = c.filterRow(3);
    const auto mover = c.filter(400, 1600);
    c.editor.finishCardGlideForTest();
    const auto before = c.allRects();
    const int shift = synth::insert_gap::snapUp(c.rect(mover).getWidth() + 40);

    c.press(mover);
    c.dragCentreTo(mover, between(c.rect(row[1]), c.rect(row[2])));
    c.release(mover);
    c.editor.finishCardGlideForTest();
    EXPECT_FALSE(c.gap().isOpen());
    EXPECT_EQ(c.rect(mover).getPosition(), before.at(row[2].uid).getPosition()) << "the card settles into the gap";
    expectRow(c, row, before, 2, shift, "after the drop");
    const auto inserted = c.allRects();

    for (int cycle = 0; cycle < 3; ++cycle) {
        ASSERT_TRUE(c.undo.undo());
        c.editor.finishCardGlideForTest();
        EXPECT_EQ(c.rect(mover), before.at(mover.uid)) << "the card goes back to where it was picked up";
        expectRow(c, row, before, 99, 0, "after undo");
        ASSERT_TRUE(c.undo.redo());
        c.editor.finishCardGlideForTest();
        EXPECT_EQ(c.allRects(), inserted);
    }
}

TEST(InsertGapDrop, ACanvasCardMovedWithinItsOwnRowInsertsWhereItIsDropped) {
    Canvas c;
    const auto row = c.filterRow(4);
    const auto before = c.allRects();
    c.press(row[3]);
    c.dragCentreTo(row[3], between(c.rect(row[0]), c.rect(row[1])));
    c.release(row[3]);
    c.editor.finishCardGlideForTest();
    const int shift = synth::insert_gap::snapUp(before.at(row[3].uid).getWidth() + 40);
    EXPECT_EQ(c.rect(row[3]).getPosition(), before.at(row[1].uid).getPosition());
    EXPECT_EQ(c.rect(row[1]), before.at(row[1].uid).translated(shift, 0));
    EXPECT_EQ(c.rect(row[2]), before.at(row[2].uid).translated(shift, 0));
    EXPECT_EQ(c.rect(row[0]), before.at(row[0].uid));
}

TEST(InsertGapDrop, TheKeyboardInsertGoesRightAfterTheSelectedCard) {
    Canvas c;
    const auto row = c.filterRow(4);
    const auto before = c.allRects();
    c.editor.setSelectedNodes({row[1]});
    const auto ids = c.nodeIds();

    ASSERT_TRUE(synth::insertModuleAfterSelectedCard(c.editor, "VCA"));
    c.editor.finishCardGlideForTest();
    NodeID added;
    for (auto* n : c.engine.getGraph().getNodes())
        if (ids.count(n->nodeID.uid) == 0)
            added = n->nodeID;
    ASSERT_NE(added.uid, 0u);
    EXPECT_EQ(c.rect(added).getPosition(), before.at(row[2].uid).getPosition());
    expectRow(c, row, before, 2, Canvas::shiftFor("VCA"), "after the keyboard insert");
    EXPECT_FALSE(c.gap().hasPending());

    ASSERT_TRUE(c.undo.undo());
    c.editor.finishCardGlideForTest();
    EXPECT_EQ(c.card(added), nullptr);
    expectRow(c, row, before, 99, 0, "after undo");
    EXPECT_FALSE(c.undo.canUndo());
}

TEST(InsertGapDrop, TheKeyboardInsertAfterTheLastCardMovesNothing) {
    Canvas c;
    const auto row = c.filterRow(3);
    const auto before = c.allRects();
    c.editor.setSelectedNodes({row[2]});
    const auto ids = c.nodeIds();
    ASSERT_TRUE(synth::insertModuleAfterSelectedCard(c.editor, "VCA"));
    expectRow(c, row, before, 99, 0, "after the keyboard insert at the end");
    NodeID added;
    for (auto* n : c.engine.getGraph().getNodes())
        if (ids.count(n->nodeID.uid) == 0)
            added = n->nodeID;
    ASSERT_NE(added.uid, 0u);
    EXPECT_GE(c.rect(added).getX(), c.rect(row[2]).getRight()) << "it lands after the card";
}

TEST(InsertGapDrop, TheKeyboardInsertNeedsExactlyOneSelectedCard) {
    Canvas c;
    const auto row = c.filterRow(3);
    EXPECT_FALSE(synth::insertModuleAfterSelectedCard(c.editor, "VCA"));
    c.editor.setSelectedNodes({row[0], row[1]});
    EXPECT_FALSE(synth::insertModuleAfterSelectedCard(c.editor, "VCA"));
    EXPECT_EQ(c.engine.getGraph().getNumNodes(), 3);
}

TEST(InsertGapDrop, ARefusedDropPutsTheCardsBack) {
    Canvas c;
    const auto row = c.filterRow(3);
    c.add(synth::AIStateMapper::createModule("Audio Output"), 3000, 1800);
    c.editor.finishCardGlideForTest();
    const auto before = c.allRects();
    const auto nodes = c.engine.getGraph().getNumNodes();
    // A second Audio Output is refused.
    c.libraryEnter("Audio Output", between(c.rect(row[0]), c.rect(row[1])));
    c.libraryDrop("Audio Output", between(c.rect(row[0]), c.rect(row[1])));
    c.editor.finishCardGlideForTest();
    EXPECT_EQ(c.engine.getGraph().getNumNodes(), nodes);
    EXPECT_FALSE(c.gap().isOpen());
    for (auto id : row)
        EXPECT_EQ(c.rect(id), before.at(id.uid));
    EXPECT_FALSE(c.undo.canUndo());
}

TEST(InsertGapDrop, ACollapsedMacroInTheRowIsPushedAndComesBackOnUndo) {
    Canvas c;
    const auto row = c.filterRow(3);
    const auto inner = c.filter(c.rect(row[2]).getRight() + 40, 400);
    const auto innerBelow = c.filter(c.rect(row[2]).getRight() + 40, 1400);
    const auto macroId = c.openMacro({inner, innerBelow});
    ASSERT_TRUE(macroId.isNotEmpty());
    c.macros().setMacroCollapsed(macroId, true);
    c.editor.finishCardGlideForTest();
    ASSERT_NE(c.editor.getMacros().find(macroId), nullptr);
    const auto cardBefore = c.editor.getMacros().find(macroId)->bounds;

    c.dropBetween("VCA", between(c.rect(row[0]), c.rect(row[1])));
    c.editor.finishCardGlideForTest();
    const auto pushed = c.editor.getMacros().find(macroId)->bounds;
    EXPECT_EQ(pushed, cardBefore.translated(Canvas::shiftFor("VCA"), 0)) << "the collapsed card is a row member too";

    ASSERT_TRUE(c.undo.undo());
    c.editor.finishCardGlideForTest();
    EXPECT_EQ(c.editor.getMacros().find(macroId)->bounds, cardBefore);
    ASSERT_TRUE(c.undo.redo());
    c.editor.finishCardGlideForTest();
    EXPECT_EQ(c.editor.getMacros().find(macroId)->bounds, pushed);
}

TEST(InsertGapDrop, AColumnDropPushesTheCardsBelowDown) {
    Canvas c;
    std::vector<NodeID> column;
    for (int i = 0; i < 3; ++i)
        column.push_back(c.add(std::make_unique<VCAModule>(), 400, 400 + i * 1000));
    const int h = c.rect(column[0]).getHeight();
    for (int i = 0; i < 3; ++i)
        c.place(column[(size_t)i], 400, 400 + i * (h + 40));
    c.editor.finishCardGlideForTest();
    const auto before = c.allRects();
    const auto top = before.at(column[0].uid), mid = before.at(column[1].uid);

    const auto added = c.dropBetween("VCA", {top.getCentreX(), (top.getBottom() + mid.getY()) / 2});
    c.editor.finishCardGlideForTest();
    ASSERT_NE(added.uid, 0u);
    // The new card lands one spacing below the top card, on the grid, and the cards below make room for its real
    // height.
    const int slotY = synth::insert_gap::snapUp(top.getBottom() + 40);
    const int shift = synth::insert_gap::snapUp(slotY + c.rect(added).getHeight() + 40 - mid.getY());
    EXPECT_EQ(c.rect(added).getPosition(), juce::Point<int>(mid.getX(), slotY));
    EXPECT_EQ(c.rect(column[0]), top);
    EXPECT_EQ(c.rect(column[1]), mid.translated(0, shift));
    EXPECT_EQ(c.rect(column[2]), before.at(column[2].uid).translated(0, shift));
    ASSERT_TRUE(c.undo.undo());
    c.editor.finishCardGlideForTest();
    for (auto id : column)
        EXPECT_EQ(c.rect(id), before.at(id.uid));
}
