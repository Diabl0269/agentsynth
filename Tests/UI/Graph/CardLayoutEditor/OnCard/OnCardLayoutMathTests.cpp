// OnCardLayoutMathTests.cpp -- the on-card editor's snapping and pushing, as pure functions on rectangles.

#include "UI/Graph/CardLayoutEditor/OnCard/OnCardLayoutMath.h"
#include <gtest/gtest.h>

using juce::Rectangle;
using namespace synth::ui::oncard;

namespace {

const Limits kLimits{12, 300, 40};

bool allClear(Rectangle<int> a, const std::vector<Rectangle<int>>& others) {
    for (const auto& o : others)
        if (tooClose(a, o))
            return false;
    return true;
}

} // namespace

TEST(OnCardLayoutMath, ALeftEdgeWithinFourPixelsSnapsToAnotherControlsLeftEdgeAndDrawsAGuide) {
    const std::vector<Rectangle<int>> others{{100, 40, 80, 76}};
    const auto snapped = snapDraggedRect({103, 160, 80, 76}, others, true);
    EXPECT_EQ(snapped.rect.getX(), 100);
    EXPECT_EQ(snapped.rect.getY(), 160) << "nothing lines up vertically";
    ASSERT_EQ(snapped.guides.size(), 1u);
    EXPECT_TRUE(snapped.guides[0].vertical);
    EXPECT_EQ(snapped.guides[0].position, 100);
    EXPECT_LE(snapped.guides[0].from, 40);
    EXPECT_GE(snapped.guides[0].to, 160 + 76);
}

TEST(OnCardLayoutMath, FiveOrMorePixelsOffDoesNotSnap) {
    const auto snapped = snapDraggedRect({105, 160, 80, 76}, {{100, 40, 80, 76}}, true);
    EXPECT_EQ(snapped.rect.getX(), 105);
    EXPECT_TRUE(snapped.guides.empty());
}

TEST(OnCardLayoutMath, CentresAndBottomsAlignToo) {
    const auto snapped =
        snapDraggedRect({141, 199, 40, 76}, {{100, 120, 120, 76}}, true); // centre 161 vs 160, top 199 vs bottom 196
    EXPECT_EQ(snapped.rect.getCentreX(), 160) << "centre to centre";
    EXPECT_EQ(snapped.rect.getY(), 196) << "top to the other's bottom";
}

TEST(OnCardLayoutMath, HoldingCommandPlacesFreely) {
    const auto snapped = snapDraggedRect({103, 160, 80, 76}, {{100, 40, 80, 76}}, false);
    EXPECT_EQ(snapped.rect, Rectangle<int>(103, 160, 80, 76));
    EXPECT_TRUE(snapped.guides.empty());
}

TEST(OnCardLayoutMath, TheClosestLineWins) {
    const auto snapped = snapDraggedRect({102, 160, 80, 76}, {{100, 40, 80, 76}, {103, 40, 80, 76}}, true);
    EXPECT_EQ(snapped.rect.getX(), 103);
}

TEST(OnCardLayoutMath, ClampKeepsAControlInsideTheContentAndBelowTheSectionTop) {
    EXPECT_EQ(clampToLimits({0, 0, 80, 76}, kLimits), Rectangle<int>(12, 40, 80, 76));
    EXPECT_EQ(clampToLimits({290, 100, 80, 76}, kLimits), Rectangle<int>(220, 100, 80, 76));
    EXPECT_EQ(clampToLimits({50, 400, 80, 76}, kLimits), Rectangle<int>(50, 400, 80, 76)) << "may go below";
}

TEST(OnCardLayoutMath, ACellOverlappedByTheDropIsPushedTheShortestWayOutKeepingTheGap) {
    const Rectangle<int> dropped{100, 100, 80, 76};
    const std::vector<Rectangle<int>> others{{150, 100, 80, 76}};
    const auto pushed = pushAside(dropped, dropped, others, kLimits);
    ASSERT_EQ(pushed.size(), 1u);
    EXPECT_EQ(pushed[0], Rectangle<int>(188, 100, 80, 76)) << "right is the shortest way: 38 px";
    EXPECT_TRUE(allClear(pushed[0], {dropped}));
}

TEST(OnCardLayoutMath, APushThatWouldLeaveTheContentGoesAnotherWay) {
    const Rectangle<int> dropped{200, 100, 80, 76};
    const std::vector<Rectangle<int>> others{{150, 100, 80, 76}};
    const auto pushed = pushAside(dropped, dropped, others, kLimits);
    EXPECT_TRUE(allClear(pushed[0], {dropped}));
    EXPECT_GE(pushed[0].getX(), kLimits.minX);
    EXPECT_LE(pushed[0].getRight(), kLimits.maxX);
    EXPECT_GE(pushed[0].getY(), kLimits.top);
}

TEST(OnCardLayoutMath, ACellThatIsAGapAwayAlreadyStays) {
    const std::vector<Rectangle<int>> others{{188, 100, 80, 76}, {12, 300, 80, 76}};
    EXPECT_EQ(pushAside({100, 100, 80, 76}, {100, 100, 80, 76}, others, kLimits), others) << "8 px apart is clear";
}

TEST(OnCardLayoutMath, ACellThatAlreadyAbuttedTheControlStaysUnlessTheDropOverlapsIt) {
    const Rectangle<int> start{12, 100, 80, 76};
    const std::vector<Rectangle<int>> below{{12, 176, 80, 76}}; // flush under it, as a default layout has it
    EXPECT_EQ(pushAside({13, 100, 80, 76}, start, below, kLimits), below) << "a nudge leaves the flush row alone";
    const auto overlapped = pushAside({12, 110, 80, 76}, start, below, kLimits);
    EXPECT_NE(overlapped, below) << "moved onto it, the row is pushed";
    EXPECT_FALSE(tooClose(Rectangle<int>(12, 110, 80, 76), overlapped[0]));
}

TEST(OnCardLayoutMath, APushedCellThenPushesWhatItLandsOn) {
    const Rectangle<int> dropped{12, 100, 80, 76};
    const std::vector<Rectangle<int>> others{{60, 100, 80, 76}, {148, 100, 80, 76}};
    const auto pushed = pushAside(dropped, dropped, others, kLimits);
    std::vector<Rectangle<int>> all{dropped, pushed[0], pushed[1]};
    for (size_t i = 0; i < all.size(); ++i)
        for (size_t j = i + 1; j < all.size(); ++j)
            EXPECT_FALSE(tooClose(all[i], all[j])) << i << " and " << j;
}

TEST(OnCardLayoutMath, DescribeMoveNamesTheDirectionAndDistance) {
    EXPECT_EQ(describeMove("Cutoff", 8, 0), "Cutoff moved right 8");
    EXPECT_EQ(describeMove("Cutoff", -1, 0), "Cutoff moved left 1");
    EXPECT_EQ(describeMove("Cutoff", 0, -8), "Cutoff moved up 8");
    EXPECT_EQ(describeMove("Cutoff", 60, 12), "Cutoff moved right 60, down 12");
}

TEST(OnCardLayoutMath, AFreeSpotInAnEmptyGroupIsItsTopLeft) {
    EXPECT_EQ(findFreeSpot({80, 76}, {}, kLimits), juce::Point<int>(12, 40));
}

TEST(OnCardLayoutMath, AFreeSpotSitsBesideTheControlsWithTheGapBetween) {
    const std::vector<Rectangle<int>> occupied{{12, 40, 80, 76}, {100, 40, 80, 76}};
    const auto spot = findFreeSpot({80, 76}, occupied, kLimits);
    EXPECT_EQ(spot, juce::Point<int>(188, 40)) << "the third column of the first row";
    EXPECT_TRUE(allClear({spot.x, spot.y, 80, 76}, occupied));
}

TEST(OnCardLayoutMath, AFreeSpotGoesBelowARowThatHasNoRoomLeft) {
    const std::vector<Rectangle<int>> occupied{{12, 40, 90, 76}, {104, 40, 90, 76}, {196, 40, 90, 76}};
    const auto spot = findFreeSpot({90, 76}, occupied, kLimits);
    EXPECT_EQ(spot, juce::Point<int>(12, 124));
}

TEST(OnCardLayoutMath, AFreeSpotTakesTheHighestOpenPlaceEvenInALowerRow) {
    const std::vector<Rectangle<int>> occupied{
        {12, 40, 90, 76}, {104, 40, 90, 76}, {196, 40, 90, 76}, {104, 124, 90, 76}};
    const auto spot = findFreeSpot({90, 76}, occupied, kLimits);
    EXPECT_EQ(spot, juce::Point<int>(202, 124)) << "beside the lower row's one control, not under the first";
    EXPECT_TRUE(allClear({spot.x, spot.y, 90, 76}, occupied));
}

TEST(OnCardLayoutMath, AControlWiderThanTheGroupStillGetsAPlaceBelowEverything) {
    const std::vector<Rectangle<int>> occupied{{12, 40, 80, 76}};
    const auto spot = findFreeSpot({400, 24}, occupied, kLimits);
    EXPECT_EQ(spot.x, kLimits.minX);
    EXPECT_GE(spot.y, 116 + kControlGap);
}

// A row of three cells with `slack` spare pixels: where the other two stand when the first is dropped
// `shift` px to the right of its place.
namespace {
std::vector<Rectangle<int>> pushedRow(int shift, int slack) {
    const Limits limits{12, 12 + 3 * 80 + 2 * 8 + slack, 40};
    const Rectangle<int> first{12, 40, 80, 76};
    const std::vector<Rectangle<int>> homes{{100, 40, 80, 76}, {188, 40, 80, 76}};
    return pushAside(first.translated(shift, 0), first, homes, limits);
}
} // namespace

TEST(OnCardLayoutMath, NeighboursShiftSidewaysWhileTheRowHasRoomForThem) {
    const auto pushed = pushedRow(40, 60);
    ASSERT_EQ(pushed.size(), 2u);
    EXPECT_EQ(pushed[0].getY(), 40) << "still on the row";
    EXPECT_EQ(pushed[1].getY(), 40) << "still on the row";
    EXPECT_GT(pushed[0].getX(), 100);
    EXPECT_GT(pushed[1].getX(), 188);
}

TEST(OnCardLayoutMath, AFullRowWrapsTheNeighbourThatHasNoRoomLeft) {
    const auto pushed = pushedRow(40, 0);
    EXPECT_GT(pushed[0].getY() + pushed[1].getY(), 80) << "one of them goes to a new row";
    EXPECT_FALSE(tooClose(Rectangle<int>(52, 40, 80, 76), pushed[0]));
    EXPECT_FALSE(tooClose(Rectangle<int>(52, 40, 80, 76), pushed[1]));
}

TEST(OnCardLayoutMath, APushedCellReturnsHomeOnceTheDropMovesAwayAndOtherwiseStaysPushed) {
    const Rectangle<int> first{12, 40, 80, 76};
    const std::vector<Rectangle<int>> homes{{100, 40, 80, 76}, {188, 40, 80, 76}};
    const std::vector<Rectangle<int>> pushed{{100, 124, 80, 76}, {188, 40, 80, 76}};
    EXPECT_EQ(returnHome(pushed, homes, homes, first, first), homes) << "the drop is back at its place: home is free";
    EXPECT_EQ(returnHome(pushed, homes, homes, first.translated(40, 0), first), pushed)
        << "the drop still covers the home";
}

TEST(OnCardLayoutMath, AHomeThatAMovedControlNowCoversIsNotReturnedTo) {
    const std::vector<Rectangle<int>> origins{{100, 40, 80, 76}, {188, 40, 80, 76}};
    const std::vector<Rectangle<int>> homes{{100, 40, 80, 76}, {120, 40, 80, 76}}; // the second chose its place
    const std::vector<Rectangle<int>> standing{{100, 124, 80, 76}, {120, 40, 80, 76}};
    EXPECT_EQ(returnHome(standing, homes, origins, {12, 300, 80, 76}, {12, 300, 80, 76})[0], standing[0]);
}

TEST(OnCardLayoutMath, CellsTheLayoutPlacedFlushToEachOtherMayReturnFlush) {
    const std::vector<Rectangle<int>> homes{{12, 40, 80, 76}, {12, 116, 80, 76}}; // abut, as a default layout has it
    const std::vector<Rectangle<int>> standing{{12, 300, 80, 76}, {12, 116, 80, 76}};
    const Rectangle<int> dropped{100, 40, 80, 76};
    EXPECT_EQ(returnHome(standing, homes, homes, dropped, dropped), homes);
    const Rectangle<int> flushDrop{92, 40, 80, 76}; // the dropped control back at the origin, flush to a home
    const std::vector<Rectangle<int>> next{{100, 124, 80, 76}};
    const std::vector<Rectangle<int>> nextHomes{{172, 40, 80, 76}};
    EXPECT_EQ(returnHome(next, nextHomes, nextHomes, flushDrop, flushDrop), nextHomes)
        << "its own origin keeps the flush";
    EXPECT_EQ(returnHome(next, nextHomes, nextHomes, flushDrop, flushDrop.translated(-50, 0)), next)
        << "not at its origin: the gap applies";
}

// ---- Rows (dropIntoRow, repackRow) ---------------------------------------------------------------

namespace {

// layout-overlap: no two cells closer than the gap, every cell inside the limits.
void expectTidy(const std::vector<Rectangle<int>>& cells, const Limits& limits) {
    for (size_t i = 0; i < cells.size(); ++i) {
        EXPECT_GE(cells[i].getX(), limits.minX) << i;
        EXPECT_LE(cells[i].getRight(), limits.maxX) << i;
        EXPECT_GE(cells[i].getY(), limits.top) << i;
        for (size_t j = i + 1; j < cells.size(); ++j)
            EXPECT_FALSE(tooClose(cells[i], cells[j])) << i << " and " << j;
    }
}

std::vector<Rectangle<int>> withDropped(const RowDrop& drop) {
    auto all = drop.others;
    all.push_back(drop.dropped);
    return all;
}

const std::vector<Rectangle<int>> kFullRow{{12, 40, 90, 76}, {110, 40, 90, 76}, {208, 40, 90, 76}};

} // namespace

TEST(OnCardLayoutRows, CellsWhoseVerticalExtentsOverlapAreOneRowOrderedByX) {
    const std::vector<Rectangle<int>> cells{{110, 60, 90, 76}, {12, 40, 90, 76}, {12, 144, 90, 76}};
    const auto rows = rowsOf(cells);
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0], (std::vector<int>{1, 0})) << "y 40..116 and 60..136 overlap, left one first";
    EXPECT_EQ(rows[1], (std::vector<int>{2}));
}

TEST(OnCardLayoutRows, InsertingIntoAFullRowWrapsTheTrailingCellToANewRowBelow) {
    const auto drop = dropIntoRow({100, 40, 90, 76}, kFullRow, kLimits);
    EXPECT_EQ(drop.newRowAt, -1);
    EXPECT_EQ(drop.others[0], kFullRow[0]) << "the cell left of the drop stays";
    EXPECT_LT(drop.dropped.getX(), drop.others[1].getX()) << "inserted before the second cell";
    EXPECT_EQ(drop.dropped.getY(), 40);
    EXPECT_EQ(drop.others[2].getPosition(), juce::Point<int>(12, 124)) << "the trailing cell wrapped";
    expectTidy(withDropped(drop), kLimits);
}

TEST(OnCardLayoutRows, DraggingACellOutOfAFullRowAndBackRestoresOneRow) {
    const std::vector<Rectangle<int>> rest{kFullRow[1], kFullRow[2]};
    for (int x : {12, 14, 20}) {
        const auto drop = dropIntoRow({x, 40, 90, 76}, rest, kLimits);
        const auto all = withDropped(drop);
        EXPECT_EQ(rowsOf(all).size(), 1u) << "dropped at x " << x;
        expectTidy(all, kLimits);
        if (x == 12) {
            EXPECT_EQ(drop.dropped, kFullRow[0]);
            EXPECT_EQ(drop.others, rest);
        }
    }
}

TEST(OnCardLayoutRows, DroppingBelowTheLastRowStartsANewRowThere) {
    const auto drop = dropIntoRow({100, 100, 90, 76}, {kFullRow[0], kFullRow[1]}, kLimits);
    EXPECT_EQ(drop.newRowAt, 116 + kControlGap);
    EXPECT_EQ(drop.dropped.getY(), drop.newRowAt);
    EXPECT_EQ(drop.others, (std::vector<Rectangle<int>>{kFullRow[0], kFullRow[1]})) << "the others stay";
    expectTidy(withDropped(drop), kLimits);
}

TEST(OnCardLayoutRows, ADropIntoARowAlignsToItsTopAndPushesTheRowsBelowDown) {
    std::vector<Rectangle<int>> cells = kFullRow;
    cells.emplace_back(12, 124, 90, 76);
    const auto drop = dropIntoRow({100, 60, 90, 76}, cells, kLimits);
    EXPECT_EQ(drop.dropped.getY(), 40);
    EXPECT_GT(drop.others[3].getY(), 124) << "the wrapped cell took the row below's place";
    expectTidy(withDropped(drop), kLimits);
}

TEST(OnCardLayoutRows, ALoneCellInAnEmptyGroupJustLandsInsideTheLimits) {
    const auto drop = dropIntoRow({400, 90, 90, 76}, {}, kLimits);
    EXPECT_EQ(drop.dropped.getRight(), kLimits.maxX);
}

TEST(OnCardLayoutRows, ACellThatShrinksPullsTheCellsThatAbuttedItLeft) {
    const Limits limits{12, 300, 40};
    const std::vector<Rectangle<int>> faders{
        {12, 40, 60, 150}, {80, 40, 60, 150}, {148, 40, 60, 150}, {216, 40, 60, 150}};
    auto cells = faders;
    cells[0] = {12, 40, 30, 76}; // a knob where the fader was
    const auto packed = repackRow(cells, 0, faders[0], limits);
    EXPECT_EQ(packed[1].getX(), 50);
    EXPECT_EQ(packed[3].getX(), 186);
    EXPECT_EQ(rowsOf(packed).size(), 1u);
    expectTidy(packed, limits);
}

TEST(OnCardLayoutRows, ACellThatGrowsPushesItsRowAlongAndWrapsWhatNoLongerFits) {
    const Limits limits{12, 300, 40};
    const std::vector<Rectangle<int>> row{{12, 40, 60, 150}, {80, 40, 60, 150}, {148, 40, 60, 150}, {216, 40, 60, 150}};
    auto cells = row;
    cells[0] = {12, 40, 80, 150};
    auto packed = repackRow(cells, 0, row[0], limits);
    EXPECT_EQ(rowsOf(packed).size(), 1u) << "20 px wider still fits one row";
    expectTidy(packed, limits);
    cells[0] = {12, 40, 120, 150};
    packed = repackRow(cells, 0, row[0], limits);
    EXPECT_EQ(rowsOf(packed).size(), 2u) << "the trailing cell wrapped";
    EXPECT_EQ(packed[3].getPosition(), juce::Point<int>(12, 198));
    expectTidy(packed, limits);
}

TEST(OnCardLayoutRows, ADropIntoAFullGroupLeavesItTidyWhereverItLands) {
    std::vector<Rectangle<int>> group = kFullRow;
    group.emplace_back(12, 124, 90, 76);
    group.emplace_back(110, 124, 90, 76);
    for (int y = 30; y <= 260; y += 20)
        for (int x = 0; x <= 300; x += 50) {
            const auto drop = dropIntoRow({x, y, 90, 76}, group, kLimits);
            expectTidy(withDropped(drop), kLimits);
        }
}

TEST(OnCardLayoutRows, AFlushRowStaysOneRowWhenACellLeavesAndComesBack) {
    const std::vector<Rectangle<int>> flush{{12, 40, 96, 76}, {108, 40, 96, 76}, {204, 40, 96, 76}};
    const std::vector<Rectangle<int>> rest{flush[1], flush[2]};
    for (int x : {12, 15}) {
        const auto drop = dropIntoRow({x, 40, 96, 76}, rest, kLimits);
        EXPECT_EQ(rowsOf(withDropped(drop)).size(), 1u) << x;
        EXPECT_EQ(drop.dropped, flush[0]) << x;
        EXPECT_EQ(drop.others, rest) << x;
    }
}

TEST(OnCardLayoutRows, ADropBetweenTwoRowsMakesARowOfItsOwnAndLeavesAFlushNeighbourAlone) {
    const std::vector<Rectangle<int>> rows{{12, 40, 90, 98}, {12, 138, 90, 76}};
    const auto back = dropIntoRow({12, 20, 90, 98}, {rows[1]}, kLimits);
    EXPECT_EQ(back.dropped.getY(), kLimits.top) << "above the only row: its own row at the top";
    EXPECT_EQ(back.others[0], rows[1]) << "flush against it, so it does not move";
}
