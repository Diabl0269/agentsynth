// SectionGroupDropPlanTests.cpp -- sectionstrip::planGroupDrop, the plan of dragging several section blocks as one
// group: one member is exactly the single-block plan, the members keep their distances from the group's left edge, a
// copy closes nothing, and a cover drop covers only what a member lands on.
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
    EXPECT_DOUBLE_EQ(groupStart(p, 2), 20.0) << "touching members stay touching";
    EXPECT_DOUBLE_EQ(*p.startOf(3), 0.0);
    EXPECT_DOUBLE_EQ(*p.startOf(4), 8.0);
    EXPECT_FALSE(p.startOf(1).has_value()) << "a member is not one of the others";
}

TEST(SectionGroupDropPlanTest, MembersWithABlockBetweenThemKeepTheirGapAndTheBlockBetweenClosesUp) {
    // Regression test for FRO1753: a group of A and C landed back to back. A and C span 20 beats (0..20). B closes
    // up to 0..4, D to 4..8; the group lands at 4 (before D) with C 12 beats after A, as it was.
    const auto p = group({3, 1}, 4.0, false, true);
    EXPECT_DOUBLE_EQ(p.landing, 4.0);
    EXPECT_DOUBLE_EQ(groupStart(p, 1), 4.0);
    EXPECT_DOUBLE_EQ(groupStart(p, 3), 16.0) << "the gap between the members stays";
    EXPECT_DOUBLE_EQ(*p.startOf(2), 0.0);
    EXPECT_DOUBLE_EQ(*p.startOf(4), 24.0) << "pushed later by the group's 20-beat span";
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

TEST(SectionGroupDropPlanTest, CoverMarksEveryBlockAMemberLandsOn) {
    // B and D keep their 12-beat distance: dropped at 0 they land on 0..4 and 12..16, covering A (0..8) and C (12..20).
    const auto p = group({2, 4}, 0.0, false, false);
    EXPECT_DOUBLE_EQ(p.landing, 0.0);
    EXPECT_DOUBLE_EQ(groupStart(p, 2), 0.0);
    EXPECT_DOUBLE_EQ(groupStart(p, 4), 12.0);
    EXPECT_TRUE(p.isCovered(1));
    EXPECT_TRUE(p.isCovered(3));
    EXPECT_FALSE(p.startOf(3).has_value()) << "a covered block is gone";
}

TEST(SectionGroupDropPlanTest, UnknownIdsAreIgnoredAndNoKnownIdPlansNothing) {
    EXPECT_TRUE(group({9}, 4.0, false, true).group.empty());
    const auto p = group({2, 9}, 30.0, false, true);
    ASSERT_EQ(p.group.size(), 1u);
    EXPECT_DOUBLE_EQ(p.landing, planSectionDrop(kSong, 2, 30.0, false, true, 4.0, 40.0).landing);
}

TEST(SectionGroupDropPlanTest, CoverLeavesWhatSitsBetweenLandedMembersAlone) {
    // Regression test for FRO1753: the cover span was the summed lengths, so a gapped group covered the wrong blocks.
    // Members 1 (0..4) and 3 (8..12) land at 40 and 48; block 5 (44..48) sits between them and block 2 is not touched.
    const std::vector<PlacedSpan> song{{1, 0.0, 4.0},  {2, 4.0, 4.0},  {3, 8.0, 4.0},
                                       {4, 40.0, 4.0}, {5, 44.0, 4.0}, {6, 52.0, 4.0}};
    const auto p = planGroupDrop(song, {1, 3}, 40.0, false, false, 4.0, 40.0);
    EXPECT_DOUBLE_EQ(p.landing, 40.0);
    EXPECT_DOUBLE_EQ(groupStart(p, 1), 40.0);
    EXPECT_DOUBLE_EQ(groupStart(p, 3), 48.0);
    EXPECT_TRUE(p.isCovered(4)) << "under member 1";
    EXPECT_FALSE(p.isCovered(5)) << "in the gap between the landed members";
    EXPECT_FALSE(p.isCovered(6));
    EXPECT_FALSE(p.isCovered(2));
}

TEST(SectionGroupDropPlanTest, MakeRoomOpensTheWholeSpanOnceAtTheLanding) {
    // Members 1 (0..4) and 3 (8..12), span 12; block 2 (4..8) closes up to 0..4; block 4 at 12..16 closes to 4..8.
    // At a landing of 8 (after block 4), nothing is pushed; at a landing of 0, blocks 2 and 4 move by the span 12.
    const std::vector<PlacedSpan> song{{1, 0.0, 4.0}, {2, 4.0, 4.0}, {3, 8.0, 4.0}, {4, 12.0, 4.0}};
    const auto after = planGroupDrop(song, {1, 3}, 8.0, false, true, 4.0, 40.0);
    EXPECT_DOUBLE_EQ(after.landing, 8.0);
    EXPECT_DOUBLE_EQ(groupStart(after, 1), 8.0);
    EXPECT_DOUBLE_EQ(groupStart(after, 3), 16.0);
    EXPECT_DOUBLE_EQ(*after.startOf(2), 0.0);
    EXPECT_DOUBLE_EQ(*after.startOf(4), 4.0);
    const auto front = planGroupDrop(song, {1, 3}, 0.0, false, true, 4.0, 40.0);
    EXPECT_DOUBLE_EQ(front.landing, 0.0);
    EXPECT_DOUBLE_EQ(groupStart(front, 3), 8.0);
    EXPECT_DOUBLE_EQ(*front.startOf(2), 12.0);
    EXPECT_DOUBLE_EQ(*front.startOf(4), 16.0);
}

TEST(SectionGroupDropPlanTest, TheLandingIsChosenWithTheSpanNotTheSummedLengths) {
    // Members 1 (0..4) and 3 (8..12) span 12 beats (the summed lengths are 8). Closed up, blocks 2 and 4 are 0..4 and
    // 4..8. With the group's left edge at 1 (over block 2), its centre is at 7: past block 2's drawn centre (6), so
    // it lands after block 2; with the summed 8 beats the centre would be 5 and it would land before it.
    const std::vector<PlacedSpan> song{{1, 0.0, 4.0}, {2, 4.0, 4.0}, {3, 8.0, 4.0}, {4, 12.0, 4.0}};
    const auto p = planGroupDrop(song, {1, 3}, 1.0, false, true, 4.0, 40.0);
    EXPECT_DOUBLE_EQ(p.landing, 4.0) << "after the block it was over";
}

TEST(SectionGroupDropPlanTest, ACopyOfAGappedGroupKeepsTheGapAndPushesByTheSpan) {
    const std::vector<PlacedSpan> song{{1, 0.0, 4.0}, {2, 4.0, 4.0}, {3, 8.0, 4.0}, {4, 12.0, 4.0}};
    const auto p = planGroupDrop(song, {1, 3}, 12.0, true, true, 4.0, 40.0);
    EXPECT_DOUBLE_EQ(p.landing, 12.0);
    EXPECT_DOUBLE_EQ(groupStart(p, 1), 12.0);
    EXPECT_DOUBLE_EQ(groupStart(p, 3), 20.0);
    EXPECT_DOUBLE_EQ(*p.startOf(4), 24.0) << "pushed by the 12-beat span";
    EXPECT_DOUBLE_EQ(*p.startOf(2), 4.0);
}
