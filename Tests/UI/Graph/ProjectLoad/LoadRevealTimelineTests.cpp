// LoadRevealTimelineTests.cpp -- the timing of a project coming to life (LoadRevealTimeline.h): a load where
// everything is ready lands within about 400 ms however deep the patch, a cable draws only once both its ends have
// appeared, and a group still loading waits and pops when it is ready.

#include "UI/Graph/ProjectLoad/LoadRevealTimeline.h"
#include <gtest/gtest.h>

namespace lr = synth::ui::load_reveal;

TEST(LoadRevealTimeline, AFastLoadLandsWithinAbout400Ms) {
    for (int depth : {1, 2, 5, 40, 500}) {
        SCOPED_TRACE(depth);
        std::vector<int> levels;
        std::vector<std::pair<int, int>> cables;
        for (int i = 0; i <= depth; ++i) {
            levels.push_back(i);
            if (i > 0)
                cables.emplace_back(i - 1, i);
        }
        lr::Timeline t;
        t.start(lr::Motion::full, levels, {});
        EXPECT_LE(t.endMs(cables), 400.0 + 1e-6);
        EXPECT_GT(t.endMs(cables), 0.0);
    }
}

TEST(LoadRevealTimeline, LevelsPopInOrder) {
    lr::Timeline t;
    t.start(lr::Motion::full, {0, 1, 2}, {});
    EXPECT_EQ(t.popProgress(0, 0.0), 0.0f);
    EXPECT_GT(t.popProgress(0, 20.0), 0.0f);
    EXPECT_EQ(t.popProgress(2, 20.0), 0.0f) << "a later level is still hidden";
    EXPECT_EQ(t.popProgress(2, t.appearedMs(2)), 1.0f);
    EXPECT_LT(t.appearedMs(0), t.appearedMs(1));
    EXPECT_LT(t.appearedMs(1), t.appearedMs(2));
}

TEST(LoadRevealTimeline, ACableStartsOnlyOnceBothEndsHaveAppeared) {
    lr::Timeline t;
    t.start(lr::Motion::full, {0, 3}, {});
    const double bothIn = std::max(t.appearedMs(0), t.appearedMs(1));
    EXPECT_EQ(t.cableProgress(0, 1, bothIn - 1.0), 0.0f);
    EXPECT_GT(t.cableProgress(0, 1, bothIn + 10.0), 0.0f);
    EXPECT_LT(t.cableProgress(0, 1, bothIn + 10.0), 1.0f);
    EXPECT_EQ(t.cableProgress(0, 1, bothIn + lr::kCableMs), 1.0f);
}

TEST(LoadRevealTimeline, APendingGroupWaitsAndPopsWhenReady) {
    lr::Timeline t;
    t.start(lr::Motion::full, {0, 1}, {false, true});
    EXPECT_FALSE(t.allScheduled());
    EXPECT_EQ(t.popProgress(1, 5000.0), 0.0f) << "still loading, however long it takes";
    EXPECT_EQ(t.cableProgress(0, 1, 5000.0), 0.0f);
    EXPECT_GT(t.endMs({{0, 1}}), 1.0e9) << "nothing lands while a group is pending";
    t.markReady(1, 5000.0);
    EXPECT_EQ(t.popProgress(1, 5000.0), 0.0f);
    EXPECT_EQ(t.popProgress(1, 5000.0 + lr::kPopMs), 1.0f);
    EXPECT_DOUBLE_EQ(t.endMs({{0, 1}}), 5000.0 + lr::kPopMs + lr::kCableMs);
}

TEST(LoadRevealTimeline, AGroupReadyBeforeItsSlotKeepsItsSlot) {
    lr::Timeline t;
    t.start(lr::Motion::full, {0, 3}, {false, true});
    t.markReady(1, 1.0);
    EXPECT_EQ(t.popProgress(1, 2.0), 0.0f);
    EXPECT_GT(t.appearedMs(1), t.appearedMs(0));
}

TEST(LoadRevealTimeline, ReducedAndOffMotionAppearAtOnce) {
    for (auto motion : {lr::Motion::reduced, lr::Motion::off}) {
        lr::Timeline t;
        t.start(motion, {0, 4, 9}, {});
        EXPECT_EQ(t.popProgress(2, 0.0), 1.0f);
        EXPECT_EQ(t.cableProgress(0, 2, 0.0), 1.0f);
        EXPECT_EQ(t.endMs({{0, 2}}), 0.0);
    }
}

TEST(LoadRevealTimeline, AnUnknownEndHasAlwaysAppeared) {
    lr::Timeline t;
    t.start(lr::Motion::full, {0}, {});
    EXPECT_EQ(t.appearedMs(-1), 0.0);
    EXPECT_EQ(t.cableProgress(-1, -1, 0.0), 1.0f) << "a cable the reveal does not know is simply there";
    EXPECT_EQ(t.cableProgress(-1, 0, 0.0), 0.0f) << "one known end still waits for it";
}

namespace {
std::vector<double> slotsOf(double syncLoadMs, int depth) {
    std::vector<int> levels;
    for (int i = 0; i <= depth; ++i)
        levels.push_back(i);
    lr::Timeline t;
    t.start(lr::Motion::full, levels, {}, syncLoadMs);
    std::vector<double> starts;
    for (int g = 0; g <= depth; ++g)
        starts.push_back(t.appearedMs(g) - lr::kPopMs);
    return starts;
}
} // namespace

TEST(LoadRevealTimeline, AFastOpenRevealsExactlyAsItAlwaysDid) {
    for (int depth : {1, 3, 40}) {
        const auto base = slotsOf(0.0, depth);
        EXPECT_EQ(slotsOf(150.0, depth), base);
        EXPECT_EQ(slotsOf(lr::kFastLoadMs, depth), base);
        EXPECT_LE(base.back(), lr::kWaveSpanMs + 1e-6);
    }
}

TEST(LoadRevealTimeline, ASlowOpenSpreadsTheWaveInProportionToTheLoad) {
    // 600 ms: span = 120 + 400 = 520 ms, step capped at 40 * 520 / 120 -> min(100, 173) = 100 ms.
    const auto slow = slotsOf(600.0, 10);
    EXPECT_GT(slow.back(), lr::kWaveSpanMs);
    EXPECT_NEAR(slow.back(), 520.0, 1e-6);
    const auto few = slotsOf(600.0, 2);
    EXPECT_NEAR(few.back(), 200.0, 1e-6) << "few levels step at most 100 ms";
}

TEST(LoadRevealTimeline, TheWaveNeverStretchesPastTheCap) {
    const auto huge = slotsOf(5000.0, 40);
    EXPECT_NEAR(huge.back(), lr::kMaxWaveSpanMs, 1e-6);
    const auto fewHuge = slotsOf(5000.0, 3);
    EXPECT_LE(fewHuge.back(), lr::kMaxWaveSpanMs + 1e-6);
}

TEST(LoadRevealTimeline, ReducedMotionIgnoresTheLoadTime) {
    lr::Timeline t;
    t.start(lr::Motion::reduced, {0, 1, 2}, {}, 5000.0);
    EXPECT_EQ(t.levelStepMs(), 0.0);
    EXPECT_EQ(t.appearedMs(2), 0.0);
}
