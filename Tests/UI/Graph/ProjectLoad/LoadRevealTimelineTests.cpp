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
