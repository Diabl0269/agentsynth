// SectionDropPlanTests.cpp -- sectionstrip::planSectionDrop, the one function the Sections strip's drop line, its
// neighbours' make-room glide and the drop's commit all read: where a dragged block lands and where the rest end up.
#include "UI/Timeline/TimelineSectionStrip/SectionDropPlan.h"
#include <gtest/gtest.h>

using namespace synth::ui::sectionstrip;

namespace {
// A over 0..8, B over 8..12, C over 12..20, at 40 px per beat and four beats a bar.
const std::vector<PlacedSpan> kSong{{1, 0.0, 8.0}, {2, 8.0, 4.0}, {3, 12.0, 8.0}};
DropPlan plan(std::int64_t id, double rawStart, bool copy, bool makeRoom) {
    return planSectionDrop(kSong, id, rawStart, copy, makeRoom, 4.0, 40.0);
}
} // namespace

TEST(SectionDropPlanTest, AMakeRoomMoveLandsInTheLayoutWithItsOwnGapClosed) {
    // B and C close up to 0..4 and 4..12 once A leaves; over B with its centre short of B's centre: before B.
    auto p = plan(1, 3.0, false, true);
    EXPECT_DOUBLE_EQ(p.landing, 0.0);
    EXPECT_DOUBLE_EQ(*p.startOf(2), 8.0);
    EXPECT_DOUBLE_EQ(*p.startOf(3), 12.0);
    // Over C, centre short of where C was drawn: between B and C.
    p = plan(1, 7.0, false, true);
    EXPECT_DOUBLE_EQ(p.landing, 4.0);
    EXPECT_DOUBLE_EQ(*p.startOf(2), 0.0);
    EXPECT_DOUBLE_EQ(*p.startOf(3), 12.0);
    // Past C's end: after C, at the nearest bar.
    p = plan(1, 13.0, false, true);
    EXPECT_DOUBLE_EQ(p.landing, 12.0);
    EXPECT_DOUBLE_EQ(*p.startOf(3), 4.0);
    EXPECT_FALSE(p.startOf(1).has_value()) << "the moved block is not one of the others";
}

TEST(SectionDropPlanTest, TheCentrePassingANeighboursCentreSendsTheBlockAfterIt) {
    // C (8 long) dragged back over A: its centre passes A's centre at 4 once its left edge is past -4.
    EXPECT_DOUBLE_EQ(plan(3, 1.0, false, true).landing, 8.0) << "centre 5 is past A's centre 4: after A";
    EXPECT_DOUBLE_EQ(plan(3, 0.0, false, true).landing, 0.0) << "left edge at A's start is not inside A";
}

TEST(SectionDropPlanTest, CoverLandsAgainstTheBlocksAsTheyAreAndMarksWhatItOverlaps) {
    const auto p = plan(1, 9.0, false, false);
    EXPECT_DOUBLE_EQ(p.landing, 8.0);
    EXPECT_TRUE(p.isCovered(2));
    EXPECT_TRUE(p.isCovered(3));
    EXPECT_FALSE(p.startOf(2).has_value()) << "a covered block has no start after the drop";
}

TEST(SectionDropPlanTest, ACopyKeepsTheOriginalAndPushesWhatIsAtTheLanding) {
    const auto p = plan(1, 8.0, true, true);
    EXPECT_DOUBLE_EQ(p.landing, 8.0);
    EXPECT_DOUBLE_EQ(*p.startOf(1), 0.0) << "the original stays";
    EXPECT_DOUBLE_EQ(*p.startOf(2), 16.0);
    EXPECT_DOUBLE_EQ(*p.startOf(3), 20.0);
}

TEST(SectionDropPlanTest, TheLandingIsNeverBeforeTheSongStartNorInsideABlock) {
    EXPECT_DOUBLE_EQ(plan(2, -5.0, false, true).landing, 0.0);
    for (double raw = -4.0; raw < 30.0; raw += 0.25)
        for (const bool copy : {false, true})
            for (const bool makeRoom : {false, true})
                for (std::int64_t id = 1; id <= 3; ++id) {
                    const auto p = plan(id, raw, copy, makeRoom);
                    EXPECT_GE(p.landing, 0.0);
                    if (!makeRoom)
                        continue;
                    for (const auto& other : p.others) {
                        double length = 0.0;
                        for (const auto& s : kSong)
                            if (s.id == other.id)
                                length = s.length;
                        EXPECT_FALSE(p.landing > other.start + 1e-6 && p.landing < other.start + length - 1e-6)
                            << "raw " << raw << " id " << id << " lands inside " << other.id;
                    }
                }
}
