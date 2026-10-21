// StepGridModelTests.cpp -- the step grid's pure side: sizes, pages, the piano roll's range, how a lane value maps to
// a bar and back, and the words a screen reader hears.
#include "UI/ModuleViews/StepGrid/StepGridLayout.h"
#include "UI/ModuleViews/StepGrid/StepGridModel.h"
#include <gtest/gtest.h>

using namespace synth;
using namespace synth::ui::stepgrid;

TEST(StepGridModel, ThePageCountIsStepsOverSixteenRoundedUp) {
    EXPECT_EQ(pageCount(1), 1);
    EXPECT_EQ(pageCount(16), 1);
    EXPECT_EQ(pageCount(17), 2);
    EXPECT_EQ(pageCount(128), 8);
    EXPECT_EQ(pageLabel(0, 40), "1-16");
    EXPECT_EQ(pageLabel(2, 40), "33-40");
    EXPECT_EQ(pageDescription(1, 40), "Page 2, steps 17 to 32");
}

TEST(StepGridModel, MiddleCIsC3) {
    EXPECT_EQ(noteName(60), "C3");
    EXPECT_EQ(noteName(61), "C#3");
    EXPECT_EQ(noteName(48), "C2");
}

TEST(StepGridModel, LanesTheModuleCannotShowAreDropped) {
    EXPECT_NE(sanitizeLanes(kAllSequencerLanes, false) & sequencerLaneBit(SequencerLane::Slide), 0);
    EXPECT_EQ(sanitizeLanes(kAllSequencerLanes, true) & sequencerLaneBit(SequencerLane::Slide), 0);
    EXPECT_EQ(availableLanes(true).size() + 1, availableLanes(false).size());
}

TEST(StepGridModel, MoreLanesMakeAtallerView) {
    const int base = viewHeightFor(kDefaultSequencerLanes, false);
    const int all = viewHeightFor(kAllSequencerLanes, false);
    EXPECT_GT(all, base);
    EXPECT_EQ(all - base, [] {
        int sum = 0;
        for (const auto lane : availableLanes(false))
            if ((kDefaultSequencerLanes & sequencerLaneBit(lane)) == 0)
                sum += laneHeight(lane) + kGap;
        return sum;
    }());
    EXPECT_EQ(computeLayout(revealsOf(kAllSequencerLanes), 560).total, all);
    EXPECT_EQ(computeLayout(revealsOf(kDefaultSequencerLanes), 560).total, base);
}

TEST(StepGridModel, TheViewIsNeverTallerThanTheFullLayoutWhileLanesFold) {
    auto reveal = revealsOf(kDefaultSequencerLanes);
    reveal[(size_t)SequencerLane::Gate] = 0.5f;
    const int half = computeLayout(reveal, 560).total;
    EXPECT_GT(half, viewHeightFor(kDefaultSequencerLanes, false));
    EXPECT_LT(half, viewHeightFor(kDefaultSequencerLanes | sequencerLaneBit(SequencerLane::Gate), false));
}

TEST(StepGridModel, TheRollFitsThePitchesWithARowToSpare) {
    const auto range = fitPitchRange({60, 64, 67});
    EXPECT_LE(range.lo, 59);
    EXPECT_GE(range.hi, 68);
    EXPECT_GE(range.rows(), kMinPitchRows);
    EXPECT_LE(fitPitchRange({0, 127}).rows(), kMaxPitchRows);
    EXPECT_GE(fitPitchRange({0}).lo, 0);
    EXPECT_LE(fitPitchRange({127}).hi, 127);
}

TEST(StepGridModel, ABarLaneValueRoundTripsThroughItsNormal) {
    for (const auto lane : {SequencerLane::Gate, SequencerLane::Velocity, SequencerLane::Chance, SequencerLane::Repeat,
                            SequencerLane::Nudge, SequencerLane::Filter}) {
        SCOPED_TRACE(sequencerLaneId(lane));
        EXPECT_TRUE(isBarLane(lane));
        for (const float n : {0.0f, 0.25f, 0.5f, 1.0f}) {
            SequencerStep step;
            setLaneNormal(lane, step, n);
            SequencerStep again;
            setLaneNormal(lane, again, laneNormal(lane, step));
            EXPECT_EQ(step, again);
        }
    }
    EXPECT_FALSE(isBarLane(SequencerLane::Pitch));
    EXPECT_FALSE(isBarLane(SequencerLane::Slide));
    EXPECT_FALSE(isBarLane(SequencerLane::Condition));
}

TEST(StepGridModel, NudgeIsCentredAndTheValuesStayInRange) {
    SequencerStep step;
    step.nudge = 0.0f;
    EXPECT_FLOAT_EQ(laneNormal(SequencerLane::Nudge, step), 0.5f);
    setLaneNormal(SequencerLane::Velocity, step, 0.0f);
    EXPECT_EQ(step.velocity, 1);
    setLaneNormal(SequencerLane::Velocity, step, 1.0f);
    EXPECT_EQ(step.velocity, 127);
    setLaneNormal(SequencerLane::Repeat, step, 1.0f);
    EXPECT_EQ(step.repeat, 8);
    setLaneNormal(SequencerLane::Chance, step, 0.0f);
    EXPECT_EQ(step.chance, 0);
}

TEST(StepGridModel, TheConditionMenuListsEveryConditionOnce) {
    const auto& choices = conditionChoices();
    EXPECT_EQ(choices.front(), std::make_pair(1, 1));
    for (size_t i = 0; i < choices.size(); ++i)
        for (size_t j = i + 1; j < choices.size(); ++j)
            EXPECT_NE(choices[i], choices[j]);
    SequencerStep step;
    step.condHit = 2;
    step.condOf = 3;
    EXPECT_EQ(conditionText(step), "2/3");
}

TEST(StepGridModel, TheSpokenValueNamesTheLane) {
    SequencerStep step;
    step.chance = 50;
    EXPECT_TRUE(laneValueText(SequencerLane::Chance, step).contains("chance"));
    EXPECT_TRUE(laneValueText(SequencerLane::Chance, step).contains("50"));
}
