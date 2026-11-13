// ExitEnterTimelineTests.cpp: the shared delete / undo timeline (Source/UI/Layout/ExitEnterTimeline.h).
// Exit then gap on delete; gap, grow, outline on undo; strictly sequential so nothing overlaps.

#include "UI/Layout/ExitEnterTimeline.h"
#include <gtest/gtest.h>

using synth::ui::ExitEnterTimeline;

TEST(ExitEnterTimeline, DeleteRunsExitThenGapAndNeverTogether) {
    ExitEnterTimeline tl;
    tl.hasExit = true;
    tl.hasGap = true;
    EXPECT_DOUBLE_EQ(tl.totalMs(), 380.0);
    for (double ms = 0.0; ms <= 380.0; ms += 5.0) {
        const auto f = tl.at(ms);
        EXPECT_FALSE(f.exit > 0.0f && f.exit < 1.0f && f.gap > 0.0f) << "exit and gap overlap at " << ms;
        if (f.gap > 0.0f)
            EXPECT_FLOAT_EQ(f.exit, 1.0f) << ms;
    }
    EXPECT_FLOAT_EQ(tl.at(0.0).exit, 0.0f);
    EXPECT_FLOAT_EQ(tl.at(180.0).exit, 1.0f);
    EXPECT_FLOAT_EQ(tl.at(180.0).gap, 0.0f);
    EXPECT_FLOAT_EQ(tl.at(380.0).gap, 1.0f);
}

TEST(ExitEnterTimeline, UndoRunsGapThenGrowThenOutline) {
    ExitEnterTimeline tl;
    tl.hasGap = true;
    tl.hasEnter = true;
    EXPECT_DOUBLE_EQ(tl.totalMs(), 200.0 + 180.0 + 400.0);
    EXPECT_FLOAT_EQ(tl.at(199.0).grow, 0.0f);
    EXPECT_GT(tl.at(250.0).grow, 0.0f);
    EXPECT_FLOAT_EQ(tl.at(380.0).grow, 1.0f);
    EXPECT_FLOAT_EQ(tl.at(380.0).outline, 0.0f);
    EXPECT_NEAR(tl.at(580.0).outline, 0.5f, 1e-4f);
    EXPECT_FLOAT_EQ(tl.at(780.0).outline, 1.0f);
}

TEST(ExitEnterTimeline, AbsentPhasesTakeNoTime) {
    ExitEnterTimeline tl;
    tl.hasExit = true;
    EXPECT_DOUBLE_EQ(tl.totalMs(), 180.0);
    EXPECT_FLOAT_EQ(tl.at(180.0).gap, 1.0f);
}

TEST(ExitEnterTimeline, ReducedMotionFadesInsteadOfShrinking) {
    EXPECT_FLOAT_EQ(ExitEnterTimeline::ghostScale(0.5f, true, false), 0.5f);
    EXPECT_FLOAT_EQ(ExitEnterTimeline::ghostAlpha(0.5f, true, false), 1.0f);
    EXPECT_FLOAT_EQ(ExitEnterTimeline::ghostScale(0.5f, true, true), 1.0f);
    EXPECT_FLOAT_EQ(ExitEnterTimeline::ghostAlpha(0.25f, true, true), 0.75f);
    EXPECT_FLOAT_EQ(ExitEnterTimeline::ghostAlpha(0.25f, false, true), 0.25f);
}

TEST(ExitEnterTimeline, ScaledRectStaysCentred) {
    const auto r = ExitEnterTimeline::scaledAboutCentre({100, 200, 80, 40}, 0.5f);
    EXPECT_FLOAT_EQ(r.getCentreX(), 140.0f);
    EXPECT_FLOAT_EQ(r.getCentreY(), 220.0f);
    EXPECT_FLOAT_EQ(r.getWidth(), 40.0f);
}
