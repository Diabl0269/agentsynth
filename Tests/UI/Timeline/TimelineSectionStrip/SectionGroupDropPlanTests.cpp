// SectionGroupDropPlanTests.cpp -- sectionstrip::planGroupDrop, the plan of dragging several section blocks as one
// group: one member is exactly the single-block plan, the members land back to back with their gaps closed, a copy
// closes nothing, and a cover drop covers what the group's span overlaps.
#include "UI/Timeline/TimelineSectionStrip/SectionDropPlan.h"
#include <gtest/gtest.h>

using namespace synth::ui::sectionstrip;

namespace {
// A over 0..8, B over 8..12, C over 12..20, D over 20..24, at 40 px per beat and four beats a bar.
const std::vector<PlacedSpan> kSong{{1, 0.0, 8.0}, {2, 8.0, 4.0}, {3, 12.0, 8.0}, {4, 20.0, 4.0}};
DropPlan group(std::vector<std::int64_t> ids, double rawStart, bool copy, bool makeRoom) {
    return planGroupDrop(kSong, ids, rawStart, copy, makeRoom, 4.0, 40.0);
}
double groupStart(const DropPlan& plan, std::int64_t id) {
    for (const auto& [member, start] : plan.group)
        if (member == id)
            return start;
    return -1.0;
}
} // namespace

TEST(SectionGroupDropPlanTest, OneMemberIsExactlyTheSingleBlockPlan) {
    for (std::int64_t id = 1; id <= 4; ++id)
        for (const bool copy : {false, true})
            for (const bool makeRoom : {false, true})
                for (double raw = -4.0; raw <= 30.0; raw += 0.25) {
                    const auto single = planSectionDrop(kSong, id, raw, copy, makeRoom, 4.0, 40.0);
                    const auto grouped = group({id}, raw, copy, makeRoom);
                    const std::string where = "id " + std::to_string(id) + " raw " + std::to_string(raw);
                    EXPECT_DOUBLE_EQ(grouped.landing, single.landing) << where;
                    ASSERT_EQ(grouped.others.size(), single.others.size()) << where;
                    for (std::size_t i = 0; i < single.others.size(); ++i) {
                        EXPECT_EQ(grouped.others[i].id, single.others[i].id) << where;
                        EXPECT_DOUBLE_EQ(grouped.others[i].start, single.others[i].start) << where;
                        EXPECT_EQ(grouped.others[i].covered, single.others[i].covered) << where;
                    }
                    ASSERT_EQ(grouped.group.size(), 1u) << where;
                    EXPECT_DOUBLE_EQ(grouped.group[0].second, grouped.landing) << where;
                }
}

TEST(SectionGroupDropPlanTest, TwoNeighboursMakeRoomAsOneBlockOfTheirSummedLength) {
    // A and B (12 beats) after D: C and D close up to 0..8 and 8..12, the group lands at 12.
    const auto p = group({1, 2}, 13.0, false, true);
    EXPECT_DOUBLE_EQ(p.landing, 12.0);
    EXPECT_DOUBLE_EQ(groupStart(p, 1), 12.0);
    EXPECT_DOUBLE_EQ(groupStart(p, 2), 20.0) << "back to back in time order";
    EXPECT_DOUBLE_EQ(*p.startOf(3), 0.0);
    EXPECT_DOUBLE_EQ(*p.startOf(4), 8.0);
    EXPECT_FALSE(p.startOf(1).has_value()) << "a member is not one of the others";
}

TEST(SectionGroupDropPlanTest, MembersWithABlockBetweenThemCloseUpAndTheBlockBetweenMovesOver) {
    // A and C (16 beats) to the front of D: B closes up to 0..4, D to 4..8; the group lands at 4 (before D).
    const auto p = group({3, 1}, 4.0, false, true);
    EXPECT_DOUBLE_EQ(p.landing, 4.0);
    EXPECT_DOUBLE_EQ(groupStart(p, 1), 4.0);
    EXPECT_DOUBLE_EQ(groupStart(p, 3), 12.0) << "the gap between the members is gone";
    EXPECT_DOUBLE_EQ(*p.startOf(2), 0.0);
    EXPECT_DOUBLE_EQ(*p.startOf(4), 20.0) << "pushed later by the group's 16 beats";
    EXPECT_EQ(p.group.front().first, 1) << "time order, whatever order the ids came in";
}

TEST(SectionGroupDropPlanTest, ACopyClosesNothingAndPushesWhatIsAtTheLanding) {
    const auto p = group({1, 2}, 20.0, true, true);
    EXPECT_DOUBLE_EQ(p.landing, 20.0);
    EXPECT_DOUBLE_EQ(groupStart(p, 1), 20.0);
    EXPECT_DOUBLE_EQ(groupStart(p, 2), 28.0);
    EXPECT_DOUBLE_EQ(*p.startOf(1), 0.0) << "the originals stay";
    EXPECT_DOUBLE_EQ(*p.startOf(2), 8.0);
    EXPECT_DOUBLE_EQ(*p.startOf(3), 12.0);
    EXPECT_DOUBLE_EQ(*p.startOf(4), 32.0);
}

TEST(SectionGroupDropPlanTest, CoverMarksEveryBlockTheGroupsSpanOverlaps) {
    // B and D (8 beats) dropped at 0: A (0..8) is covered, C stays.
    const auto p = group({2, 4}, 0.0, false, false);
    EXPECT_DOUBLE_EQ(p.landing, 0.0);
    EXPECT_DOUBLE_EQ(groupStart(p, 2), 0.0);
    EXPECT_DOUBLE_EQ(groupStart(p, 4), 4.0);
    EXPECT_TRUE(p.isCovered(1));
    EXPECT_FALSE(p.isCovered(3));
    EXPECT_DOUBLE_EQ(*p.startOf(3), 12.0) << "cover moves nothing";
}

TEST(SectionGroupDropPlanTest, UnknownIdsAreIgnoredAndNoKnownIdPlansNothing) {
    EXPECT_TRUE(group({9}, 4.0, false, true).group.empty());
    const auto p = group({2, 9}, 30.0, false, true);
    ASSERT_EQ(p.group.size(), 1u);
    EXPECT_DOUBLE_EQ(p.landing, planSectionDrop(kSong, 2, 30.0, false, true, 4.0, 40.0).landing);
}
