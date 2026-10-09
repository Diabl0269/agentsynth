// InsertGapHoverTests.cpp
//
// While a module is dragged over the space between two cards the cards after it slide aside, moving on closes the gap,
// and a drag that ends without a drop (or is cancelled with Esc) leaves every card exactly where it started
// (docs/layout/layout.md#making-room-for-a-module-dropped-between-others). Drives the library's real DragAndDropTarget
// calls and real ModuleComponent mouse events on a real canvas.

#include "InsertGapTestFixture.h"

using namespace insert_gap_test;

namespace {
/** Every card at the place `before` recorded for it. */
void expectAllAt(Canvas& c, const std::map<juce::uint32, Rect>& before, const char* when) {
    const auto now = c.allRects();
    for (const auto& [uid, r] : before) {
        ASSERT_EQ(now.count(uid), 1u) << when;
        EXPECT_EQ(now.at(uid), r) << when << ": card " << uid;
        EXPECT_EQ(c.stored(NodeID(uid)), r.getPosition()) << when << ": stored position of card " << uid;
    }
}
} // namespace

TEST(InsertGapHover, TheLibraryCursorIsInCanvasCoordinates) {
    Canvas c;
    c.libraryEnter("VCA", {1234, 567});
    EXPECT_EQ(c.editor.getDragDropController().getDragPreviewAim().getCentre(), juce::Point<int>(1234, 567));
    c.libraryExit("VCA", {1234, 567});
}

TEST(InsertGapHover, HoveringBetweenTwoCardsOpensTheGapAndOnlyTheCardsAfterItMove) {
    Canvas c;
    const auto row = c.filterRow(3);
    const auto far = c.filter(400, 2400);
    c.editor.finishCardGlideForTest();
    const auto before = c.allRects();

    c.libraryEnter("VCA", between(c.rect(row[0]), c.rect(row[1])));

    ASSERT_TRUE(c.gap().isOpen());
    const int shift = Canvas::shiftFor("VCA");
    EXPECT_EQ(c.rect(row[0]), before.at(row[0].uid)) << "the card before the gap stays";
    EXPECT_EQ(c.rect(row[1]), before.at(row[1].uid).translated(shift, 0));
    EXPECT_EQ(c.rect(row[2]), before.at(row[2].uid).translated(shift, 0));
    EXPECT_EQ(c.rect(far), before.at(far.uid)) << "nothing else on the canvas moves";
    EXPECT_EQ(c.stored(row[1]), c.rect(row[1]).getPosition()) << "geometry is final at once";
    EXPECT_EQ(c.editor.getDragDropController().getDragPreviewGhost().getPosition(), before.at(row[1].uid).getPosition())
        << "the ghost shows the card landing in the gap";
    EXPECT_FALSE(c.undo.canUndo()) << "hovering records nothing";
    c.libraryExit("VCA", {0, 0});
}

TEST(InsertGapHover, MovingAwayClosesTheGapExactly) {
    Canvas c;
    const auto row = c.filterRow(4);
    const auto before = c.allRects();
    c.libraryEnter("VCA", between(c.rect(row[1]), c.rect(row[2])));
    ASSERT_TRUE(c.gap().isOpen());

    c.libraryMove("VCA", {400, 2600}); // empty canvas far below
    EXPECT_FALSE(c.gap().isOpen());
    expectAllAt(c, before, "after moving away");
    c.libraryExit("VCA", {400, 2600});
}

TEST(InsertGapHover, MovingInsideTheOpenGapKeepsIt) {
    Canvas c;
    const auto row = c.filterRow(3);
    const auto p = between(c.rect(row[0]), c.rect(row[1]));
    c.libraryEnter("VCA", p);
    const auto opened = c.allRects();
    for (int dx : {-20, -8, 0, 8, 30, 60, 120})
        for (int dy : {-100, 0, 100}) {
            c.libraryMove("VCA", p + juce::Point<int>(dx, dy));
            EXPECT_TRUE(c.gap().isOpen());
            EXPECT_EQ(c.allRects(), opened) << "dx " << dx << " dy " << dy;
        }
    c.libraryExit("VCA", p);
}

TEST(InsertGapHover, ThePointerDeepInsideACardPushesNothing) {
    Canvas c;
    const auto row = c.filterRow(3);
    const auto before = c.allRects();
    c.libraryEnter("VCA", c.rect(row[1]).getCentre());
    EXPECT_FALSE(c.gap().isOpen());
    expectAllAt(c, before, "over the middle of a card");
    c.libraryExit("VCA", {0, 0});
}

// Only one gap is open at a time, and sweeping across every gap of a long row and back, then leaving, leaves no drift.
TEST(InsertGapHover, SweepingAcrossManyGapsKeepsOneOpenAndLeavesNoDrift) {
    for (int n : {5, 10}) {
        SCOPED_TRACE(n);
        Canvas c;
        const auto row = c.filterRow(n, 200, 400);
        const auto before = c.allRects();
        const int y = c.rect(row[0]).getCentreY();
        const int x0 = c.rect(row[0]).getX() - 60, x1 = c.rect(row.back()).getRight() + 60;
        c.libraryEnter("VCA", {x0, y});
        for (int pass = 0; pass < 2; ++pass)
            for (int i = 0; i <= (x1 - x0) / 16; ++i) {
                const int x = pass == 0 ? x0 + i * 16 : x1 - i * 16;
                c.libraryMove("VCA", {x, y});
                // Every card has either stayed or moved by the one shared shift, and the moved ones are a tail.
                int movedFrom = n;
                for (int k = 0; k < n; ++k) {
                    const auto d = c.rect(row[(size_t)k]).getX() - before.at(row[(size_t)k].uid).getX();
                    ASSERT_TRUE(d == 0 || d == Canvas::shiftFor("VCA")) << "x " << x << " card " << k << " moved " << d;
                    if (d != 0 && movedFrom == n)
                        movedFrom = k;
                    if (k > movedFrom)
                        ASSERT_NE(d, 0) << "x " << x << ": a moved card is followed by one that did not move";
                }
                if (c.gap().isOpen())
                    EXPECT_EQ(c.gap().plan()->target.anchorKey,
                              "n:" + juce::String((juce::int64)row[(size_t)movedFrom].uid));
            }
        c.libraryExit("VCA", {x1, y});
        expectAllAt(c, before, "after the drag left");
        EXPECT_FALSE(c.undo.canUndo());
    }
}

TEST(InsertGapHover, EscOnALibraryDragPutsEverythingBack) {
    Canvas c;
    const auto row = c.filterRow(3);
    const auto before = c.allRects();
    c.libraryEnter("VCA", between(c.rect(row[0]), c.rect(row[1])));
    ASSERT_TRUE(c.gap().isOpen());
    // JUCE's drag image takes Esc and sends the target an itemDragExit.
    c.libraryExit("VCA", between(c.rect(row[0]), c.rect(row[1])));
    EXPECT_FALSE(c.gap().isOpen());
    expectAllAt(c, before, "after Esc");
    EXPECT_FALSE(c.undo.canUndo());
}

TEST(InsertGapHover, ANewDragAfterAnAbandonedOneStartsClean) {
    Canvas c;
    const auto row = c.filterRow(3);
    const auto before = c.allRects();
    c.libraryEnter("VCA", between(c.rect(row[0]), c.rect(row[1])));
    // A new gesture begins without the old one having exited (the preview restarts).
    c.libraryEnter("VCA", {400, 2600});
    EXPECT_FALSE(c.gap().isOpen());
    expectAllAt(c, before, "after a fresh drag began");
    c.libraryExit("VCA", {400, 2600});
}

TEST(InsertGapHover, DraggingAnExistingCardBetweenTwoOpensTheGap) {
    Canvas c;
    const auto row = c.filterRow(3);
    const auto mover = c.filter(400, 1600);
    c.editor.finishCardGlideForTest();
    const auto before = c.allRects();
    const int shift = synth::insert_gap::snapUp(c.rect(mover).getWidth() + 40);

    c.press(mover);
    c.dragCentreTo(mover, between(c.rect(row[0]), c.rect(row[1])));
    ASSERT_TRUE(c.gap().isOpen());
    EXPECT_EQ(c.rect(row[0]), before.at(row[0].uid));
    EXPECT_EQ(c.rect(row[1]), before.at(row[1].uid).translated(shift, 0));
    EXPECT_EQ(c.rect(row[2]), before.at(row[2].uid).translated(shift, 0));

    c.dragTo(mover, before.at(mover.uid).getPosition() + juce::Point<int>(0, 200)); // away again
    EXPECT_FALSE(c.gap().isOpen());
    for (auto id : row)
        EXPECT_EQ(c.rect(id), before.at(id.uid));
    c.release(mover);
}

TEST(InsertGapHover, EscDuringACardDragClosesTheGapForTheRestOfThatDrag) {
    Canvas c;
    const auto row = c.filterRow(3);
    const auto mover = c.filter(400, 1600);
    c.editor.finishCardGlideForTest();
    const auto before = c.allRects();

    c.press(mover);
    c.dragCentreTo(mover, between(c.rect(row[0]), c.rect(row[1])));
    ASSERT_TRUE(c.gap().isOpen());
    EXPECT_TRUE(c.pressEscape());
    EXPECT_FALSE(c.gap().isOpen());
    for (auto id : row)
        EXPECT_EQ(c.rect(id), before.at(id.uid)) << "Esc puts every card back";
    EXPECT_FALSE(c.editor.getSelectedNodes().empty()) << "Esc closed the gap, it did not also clear the selection";

    c.dragCentreTo(mover, between(c.rect(row[1]), c.rect(row[2])));
    EXPECT_FALSE(c.gap().isOpen()) << "stays closed while the drag goes on";
    c.release(mover);
    for (auto id : row)
        EXPECT_EQ(c.rect(id), before.at(id.uid)) << "the drop was an ordinary one";

    // The next drag can open a gap again.
    c.press(mover);
    c.dragCentreTo(mover, between(c.rect(row[0]), c.rect(row[1])));
    EXPECT_TRUE(c.gap().isOpen());
    c.dragTo(mover, c.rect(mover).getPosition() + juce::Point<int>(0, 1200));
    c.release(mover);
}

TEST(InsertGapHover, EscWithNoGapOpenStillClearsTheSelection) {
    Canvas c;
    const auto row = c.filterRow(2);
    c.editor.setSelectedNodes({row[0]});
    EXPECT_TRUE(c.pressEscape());
    EXPECT_TRUE(c.editor.getSelectedNodes().empty());
}

TEST(InsertGapHover, ACardClickedWithoutMovingNeverOpensAGap) {
    Canvas c;
    const auto row = c.filterRow(3);
    const auto before = c.allRects();
    c.press(row[1]);
    c.release(row[1]);
    EXPECT_FALSE(c.gap().isOpen());
    expectAllAt(c, before, "after a click");
}

TEST(InsertGapHover, AGroupDragNeverOpensAGap) {
    Canvas c;
    const auto row = c.filterRow(3);
    const auto a = c.filter(400, 1600), b = c.filter(800, 1600);
    c.editor.finishCardGlideForTest();
    const auto before = c.allRects();
    c.editor.setSelectedNodes({a, b});
    c.press(a);
    c.dragCentreTo(a, between(c.rect(row[0]), c.rect(row[1])));
    EXPECT_FALSE(c.gap().isOpen());
    for (auto id : row)
        EXPECT_EQ(c.rect(id), before.at(id.uid));
    c.dragTo(a, before.at(a.uid).getPosition());
    c.release(a);
}

TEST(InsertGapHover, TheOutputDockGoesBackExactlyWhenTheGapCloses) {
    Canvas c;
    const auto row = c.filterRow(3);
    const auto out = c.add(synth::AIStateMapper::createModule("Audio Output"), 3000, 400);
    c.editor.finishCardGlideForTest();
    const auto dockBefore = c.rect(out);

    c.libraryEnter("VCA", between(c.rect(row[0]), c.rect(row[1])));
    ASSERT_TRUE(c.gap().isOpen());
    EXPECT_GE(c.rect(out).getX(), c.rect(row[2]).getRight()) << "the dock stays right of the pushed cards";
    c.libraryExit("VCA", {0, 0});
    EXPECT_EQ(c.rect(out), dockBefore);
    EXPECT_EQ(c.stored(out), dockBefore.getPosition());
}

TEST(InsertGapHover, ASnippetDragNeverOpensAGap) {
    Canvas c;
    const auto row = c.filterRow(3);
    const auto before = c.allRects();
    c.libraryEnter("snippet:Anything", between(c.rect(row[0]), c.rect(row[1])));
    EXPECT_FALSE(c.gap().isOpen());
    expectAllAt(c, before, "a snippet hover");
    c.libraryExit("snippet:Anything", {0, 0});
}

TEST(InsertGapHover, ACardNudgedWithinItsOwnPlaceNeverPushes) {
    Canvas c;
    const auto row = c.filterRow(2, 400, 400, /*spacing=*/16);
    const auto before = c.allRects();
    // Nudged right towards its neighbour: its centre is still over where it was picked up.
    c.press(row[0]);
    c.dragTo(row[0], before.at(row[0].uid).getPosition() + juce::Point<int>(60, 0));
    EXPECT_FALSE(c.gap().isOpen());
    EXPECT_EQ(c.rect(row[1]), before.at(row[1].uid));
    c.dragTo(row[0], before.at(row[0].uid).getPosition());
    c.release(row[0]);
}
