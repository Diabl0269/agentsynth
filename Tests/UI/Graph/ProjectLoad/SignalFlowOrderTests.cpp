// SignalFlowOrderTests.cpp -- the order a project comes to life in: sources first, what they feed next, the output
// last, and a feedback loop still ordered deterministically (SignalFlowOrder.h).

#include "UI/Graph/ProjectLoad/SignalFlowOrder.h"
#include <gtest/gtest.h>

using synth::ui::signalFlowLevels;

TEST(SignalFlowOrder, SourcesFirstThenWhatTheyFeed) {
    // 0 LFO -> 2 Filter, 1 Osc -> 2 Filter -> 3 VCA -> 4 Output
    const auto levels = signalFlowLevels(5, {{0, 2}, {1, 2}, {2, 3}, {3, 4}});
    EXPECT_EQ(levels, (std::vector<int>{0, 0, 1, 2, 3}));
}

TEST(SignalFlowOrder, LevelIsTheDeepestFeederPlusOne) {
    // 0 -> 1 -> 2, and 0 -> 2 directly: 2 waits for 1.
    const auto levels = signalFlowLevels(3, {{0, 1}, {1, 2}, {0, 2}});
    EXPECT_EQ(levels[2], 2);
}

TEST(SignalFlowOrder, OutputComesLastEvenWhenSomethingElseRunsDeeper) {
    // 0 Osc -> 1 Output; 0 -> 2 -> 3 -> 4 (a long side chain into a scope).
    std::vector<bool> last(5, false);
    last[1] = true;
    const auto levels = signalFlowLevels(5, {{0, 1}, {0, 2}, {2, 3}, {3, 4}}, last);
    for (int v : {0, 2, 3, 4})
        EXPECT_LT(levels[(size_t)v], levels[1]) << "vertex " << v;
}

TEST(SignalFlowOrder, SeveralOutputsKeepTheirOwnOrder) {
    // 0 Osc -> 1 Master -> 2 Rec Tap -> 3 Output, plus 0 -> 4 -> 5 -> 6 deeper than any of them.
    std::vector<bool> last(7, false);
    last[1] = last[2] = last[3] = true;
    const auto levels = signalFlowLevels(7, {{0, 1}, {1, 2}, {2, 3}, {0, 4}, {4, 5}, {5, 6}}, last);
    EXPECT_GT(levels[1], levels[6]);
    EXPECT_LT(levels[1], levels[2]);
    EXPECT_LT(levels[2], levels[3]);
}

TEST(SignalFlowOrder, CyclesAreBrokenDeterministically) {
    // 0 -> 1 -> 2 -> 1 (feedback) and 2 -> 3: everything gets a level, the same one every time.
    const std::vector<std::pair<int, int>> edges{{0, 1}, {1, 2}, {2, 1}, {2, 3}};
    const auto a = signalFlowLevels(4, edges);
    const auto b = signalFlowLevels(4, edges);
    EXPECT_EQ(a, b);
    EXPECT_EQ(a[0], 0);
    EXPECT_LT(a[1], a[2]);
    EXPECT_LT(a[2], a[3]);
}

TEST(SignalFlowOrder, APureCycleStillOrders) {
    // 0 -> 1 -> 2 -> 0: nothing is free, the lowest index starts.
    const auto levels = signalFlowLevels(3, {{0, 1}, {1, 2}, {2, 0}});
    EXPECT_EQ(levels, (std::vector<int>{0, 1, 2}));
}

TEST(SignalFlowOrder, IgnoresSelfAndOutOfRangeEdges) {
    const auto levels = signalFlowLevels(2, {{0, 0}, {0, 5}, {-1, 1}, {0, 1}});
    EXPECT_EQ(levels, (std::vector<int>{0, 1}));
}
