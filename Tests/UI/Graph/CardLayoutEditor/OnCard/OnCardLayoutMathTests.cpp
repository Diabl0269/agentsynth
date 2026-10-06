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
