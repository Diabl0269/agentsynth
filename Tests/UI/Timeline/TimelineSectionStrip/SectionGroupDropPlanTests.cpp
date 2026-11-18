// SectionGroupDropPlanTests.cpp -- sectionstrip::planGroupDrop, the plan of dragging several section blocks as one
// group: one member is exactly the single-block plan, the members keep their distances from the group's left edge, a
// copy closes nothing, and a cover drop covers only what a member lands on.
#include "UI/Timeline/TimelineSectionStrip/SectionDropPlan.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <random>
#include <string>

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

TEST(SectionGroupDropPlanTest, MembersWithABlockBetweenThemKeepTheirGapAndTakeTheBlockAlong) {
    // Regression test for FRO1753: a group of A and C landed back to back. A and C span 20 beats (0..20), B between
    // them goes along (closing B up while the span opened again pushed the rest by B's bars). D closes up to
    // 0..4; the group lands at 4 (after D) with C 12 beats after A and B in its old place between them.
    const auto p = group({3, 1}, 4.0, false, true);
    EXPECT_DOUBLE_EQ(p.landing, 4.0);
    EXPECT_DOUBLE_EQ(groupStart(p, 1), 4.0);
    EXPECT_DOUBLE_EQ(groupStart(p, 3), 16.0) << "the gap between the members stays";
    EXPECT_DOUBLE_EQ(*p.startOf(2), 12.0) << "B rides between them";
    EXPECT_TRUE(p.isCarried(2));
    EXPECT_DOUBLE_EQ(*p.startOf(4), 0.0);
    EXPECT_FALSE(p.isCarried(4));
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

TEST(SectionGroupDropPlanTest, MakeRoomTakesTheWholeSpanOutAndOpensItOnceAtTheLanding) {
    // Members 1 (0..4) and 3 (8..12), span 12, block 2 (4..8) between them; block 4 at 12..16 closes up to 0..4.
    // At a landing of 4 (after block 4) the group and block 2 land there, nothing is pushed; at a landing of 0, block 4
    // moves back by the span to where it was.
    const std::vector<PlacedSpan> song{{1, 0.0, 4.0}, {2, 4.0, 4.0}, {3, 8.0, 4.0}, {4, 12.0, 4.0}};
    const auto after = planGroupDrop(song, {1, 3}, 4.0, false, true, 4.0, 40.0);
    EXPECT_DOUBLE_EQ(after.landing, 4.0);
    EXPECT_DOUBLE_EQ(groupStart(after, 1), 4.0);
    EXPECT_DOUBLE_EQ(groupStart(after, 3), 12.0);
    EXPECT_DOUBLE_EQ(*after.startOf(2), 8.0);
    EXPECT_DOUBLE_EQ(*after.startOf(4), 0.0);
    const auto front = planGroupDrop(song, {1, 3}, 0.0, false, true, 4.0, 40.0);
    EXPECT_DOUBLE_EQ(front.landing, 0.0);
    EXPECT_DOUBLE_EQ(groupStart(front, 3), 8.0);
    EXPECT_DOUBLE_EQ(*front.startOf(2), 4.0);
    EXPECT_DOUBLE_EQ(*front.startOf(4), 12.0);
}

TEST(SectionGroupDropPlanTest, TheLandingIsChosenWithTheSpanNotTheSummedLengths) {
    // Members 1 (0..4) and 3 (16..20) with an empty gap span 20 beats (the summed lengths are 8). Closed up, block 2
    // (20..32) is 0..12. With the group's left edge at 1 (over block 2), its centre is at 11: short of block 2's drawn
    // centre (26), so it lands before block 2.
    const std::vector<PlacedSpan> song{{1, 0.0, 4.0}, {3, 16.0, 4.0}, {2, 20.0, 12.0}};
    EXPECT_DOUBLE_EQ(planGroupDrop(song, {1, 3}, 1.0, false, true, 4.0, 40.0).landing, 0.0);
    // Members 1 (0..4) and 3 (8..12) span 12; block 2 (12..44) closes up to 0..32, drawn centre 28. A left edge at 17
    // puts the span's centre at 23, short of 28: before it; at 23 the centre is 29: after it. With the summed 8 beats
    // the centre would be 27 and the group would land before it.
    const std::vector<PlacedSpan> wide{{1, 0.0, 4.0}, {3, 8.0, 4.0}, {2, 12.0, 32.0}};
    EXPECT_DOUBLE_EQ(planGroupDrop(wide, {1, 3}, 17.0, false, true, 4.0, 40.0).landing, 0.0);
    EXPECT_DOUBLE_EQ(planGroupDrop(wide, {1, 3}, 23.0, false, true, 4.0, 40.0).landing, 32.0)
        << "after the block it was over";
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

TEST(SectionGroupDropPlanTest, AGroupDroppedAtItsOwnStartWithABlockBetweenChangesNothing) {
    // Regression test for FRO1975: members 1 (0..4) and 3 (8..12) with block 2 (4..8) between them. Dropped back at 0,
    // the rest of the song stayed 4 beats later (the span counted block 2's bars, which were never closed up).
    const std::vector<PlacedSpan> song{{1, 0.0, 4.0}, {2, 4.0, 4.0}, {3, 8.0, 4.0}, {4, 12.0, 4.0}};
    const auto p = planGroupDrop(song, {1, 3}, 0.0, false, true, 4.0, 40.0);
    EXPECT_DOUBLE_EQ(p.landing, 0.0);
    EXPECT_DOUBLE_EQ(groupStart(p, 1), 0.0);
    EXPECT_DOUBLE_EQ(groupStart(p, 3), 8.0);
    EXPECT_DOUBLE_EQ(*p.startOf(2), 4.0) << "the block between stays between";
    EXPECT_DOUBLE_EQ(*p.startOf(4), 12.0) << "the block after stays where it was";
}

namespace {

// A random song: 2..7 blocks of 1..4 bars, with no gap or a 1..2 bar gap before each, starting on a bar.
std::vector<PlacedSpan> randomSong(std::mt19937& rng) {
    std::uniform_int_distribution<int> count(2, 7), bars(1, 4), gap(0, 4);
    std::vector<PlacedSpan> song;
    double at = 4.0 * std::uniform_int_distribution<int>(0, 2)(rng);
    const int n = count(rng);
    for (int i = 0; i < n; ++i) {
        const int g = gap(rng);
        at += g >= 3 ? 4.0 * (g - 2) : 0.0;
        const double length = 4.0 * bars(rng);
        song.push_back({i + 1, at, length});
        at += length;
    }
    return song;
}

// A non-empty random subset of the song's ids.
std::vector<std::int64_t> randomGroup(std::mt19937& rng, const std::vector<PlacedSpan>& song) {
    std::vector<std::int64_t> ids;
    while (ids.empty())
        for (const auto& block : song)
            if (std::uniform_int_distribution<int>(0, 2)(rng) == 0)
                ids.push_back(block.id);
    return ids;
}

// The song after a plan: the others where the plan puts them, the group members at their landed starts.
std::vector<PlacedSpan> applied(const std::vector<PlacedSpan>& song, const DropPlan& plan) {
    std::vector<PlacedSpan> out;
    for (const auto& block : song) {
        auto moved = block;
        if (const auto start = plan.startOf(block.id))
            moved.start = *start;
        for (const auto& [id, start] : plan.group)
            if (id == block.id)
                moved.start = start;
        out.push_back(moved);
    }
    return out;
}

double songEnd(const std::vector<PlacedSpan>& song) {
    double end = 0.0;
    for (const auto& block : song)
        end = std::max(end, block.start + block.length);
    return end;
}

double groupFirstStart(const std::vector<PlacedSpan>& song, const std::vector<std::int64_t>& ids) {
    double first = 1e9;
    for (const auto& block : song)
        if (std::find(ids.begin(), ids.end(), block.id) != ids.end())
            first = std::min(first, block.start);
    return first;
}

std::string describe(const std::vector<PlacedSpan>& song, const std::vector<std::int64_t>& ids) {
    std::string s = "song";
    for (const auto& block : song)
        s += " " + std::to_string(block.id) + "@" + std::to_string((int)block.start) + "+" +
             std::to_string((int)block.length);
    s += " group";
    for (const auto id : ids)
        s += " " + std::to_string(id);
    return s;
}

} // namespace

TEST(SectionGroupDropPlanTest, AMakeRoomMoveIsReversibleForEveryLayout) {
    // Guard for the drag-not-reversible kind: over random songs with gaps and blocks between members, a
    // make-room move dropped at its own start changes nothing; moved anywhere and dragged back to its start, the song
    // is the original; and a move that lands among the other blocks keeps the song's length.
    std::mt19937 rng(1975);
    for (int round = 0; round < 400; ++round) {
        const auto song = randomSong(rng);
        const auto ids = randomGroup(rng, song);
        const double origin = groupFirstStart(song, ids);
        const std::string where = describe(song, ids);

        const auto still = planGroupDrop(song, ids, origin, false, true, 4.0, 40.0);
        EXPECT_DOUBLE_EQ(still.landing, origin) << where;
        const auto same = applied(song, still);
        for (std::size_t i = 0; i < song.size(); ++i)
            EXPECT_DOUBLE_EQ(same[i].start, song[i].start) << where << ": block " << song[i].id << " at the origin";

        for (double raw = 0.0; raw <= songEnd(song) + 8.0; raw += 4.0) {
            const auto away = planGroupDrop(song, ids, raw, false, true, 4.0, 40.0);
            const auto moved = applied(song, away);
            const auto back = applied(moved, planGroupDrop(moved, ids, origin, false, true, 4.0, 40.0));
            for (std::size_t i = 0; i < song.size(); ++i)
                EXPECT_DOUBLE_EQ(back[i].start, song[i].start)
                    << where << ": block " << song[i].id << " after a move to " << raw << " and back";
            const bool groupEndsTheSong = std::any_of(song.begin(), song.end(), [&](const PlacedSpan& b) {
                return b.start + b.length == songEnd(song) && std::find(ids.begin(), ids.end(), b.id) != ids.end();
            });
            // The group's span, first start to last end; the others close up by it, so a landing at or before their
            // closed-up end puts it back inside the song.
            double span = 0.0;
            for (const auto& block : song)
                if (std::find(ids.begin(), ids.end(), block.id) != ids.end())
                    span = std::max(span, block.start + block.length - origin);
            if (!groupEndsTheSong && away.landing <= songEnd(song) - span)
                EXPECT_DOUBLE_EQ(songEnd(moved), songEnd(song)) << where << ": length after a move to " << raw;
        }
    }
}
