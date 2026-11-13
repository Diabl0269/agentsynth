// computeOutputDock: the pure core of the output dock (LayoutUtil.h) -- where Master / Rec Tap / Audio Output sit,
// left to right, right of every other layout unit. The canvas wiring is covered in Tests/UI/Graph/OutputDockTests.cpp.

#include "UI/Layout/LayoutUtil.h"
#include <gtest/gtest.h>

using namespace synth::LayoutUtil;

namespace {
LayoutUnit unit(const char* key, int x, int y, int w = 100, int h = 100) { return {key, {x, y, w, h}, false}; }
const juce::Point<int> kCard{280, 160};
} // namespace

TEST(LayoutUtilOutputDock, EmptyCanvasPutsTheDockAtTheArrangeOrigin) {
    const auto positions = computeOutputDock({}, {kCard}, kArrangeOriginY);
    ASSERT_EQ(positions.size(), 1u);
    EXPECT_EQ(positions[0], juce::Point<int>(kArrangeOriginX, kArrangeOriginY));
}

TEST(LayoutUtilOutputDock, DockSitsOneLayerGapRightOfTheWidestContent) {
    const std::vector<LayoutUnit> content{unit("a", 40, 40, 200, 100), unit("b", 400, 300, 320, 100),
                                          unit("c", 100, 600, 100, 100)};
    const auto positions = computeOutputDock(content, {kCard}, 40);
    ASSERT_EQ(positions.size(), 1u);
    EXPECT_EQ(positions[0].x, 720 + kLayerGapX);
}

TEST(LayoutUtilOutputDock, ContentEdgeIsSnappedUpBeforeTheGapIsAdded) {
    const auto positions = computeOutputDock({unit("a", 0, 0, 101, 100)}, {kCard}, 40);
    EXPECT_EQ(positions[0].x, 104 + kLayerGapX);
    EXPECT_EQ(positions[0].x % kGridSize, 0);
}

TEST(LayoutUtilOutputDock, CardsFollowInChainOrderSpacedByTheCardGap) {
    const std::vector<juce::Point<int>> sizes{{280, 300}, {200, 100}, {280, 160}};
    const auto positions = computeOutputDock({unit("a", 0, 0, 400, 100)}, sizes, 64);
    ASSERT_EQ(positions.size(), 3u);
    const int left = 400 + kLayerGapX;
    EXPECT_EQ(positions[0].x, left);
    EXPECT_EQ(positions[1].x, left + 280 + kOutputDockCardGapX);
    EXPECT_EQ(positions[2].x, positions[1].x + 200 + kOutputDockCardGapX);
    for (const auto& p : positions)
        EXPECT_EQ(p.y, 64) << "one shared top y";
}

TEST(LayoutUtilOutputDock, TopYPassesThroughAndIsSnapped) {
    EXPECT_EQ(computeOutputDock({}, {kCard}, 240)[0].y, 240);
    EXPECT_EQ(computeOutputDock({}, {kCard}, 243)[0].y, 240);
    EXPECT_EQ(computeOutputDock({}, {kCard}, 245)[0].y, 248);
}

TEST(LayoutUtilOutputDock, NoDockCardsYieldsNothing) {
    EXPECT_TRUE(computeOutputDock({unit("a", 0, 0)}, {}, 40).empty());
}
