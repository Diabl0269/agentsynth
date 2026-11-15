// TimelineSectionStripFreeGapTests.cpp -- make-room only pushes when the landing span is taken: a drop, a copy and the
// keys that place or move a block leave every other block where it is when the span is free, and the doc after the
// drop equals the plan the preview showed.
#include "TimelineSectionStripFixture.h"
#include <gtest/gtest.h>

using namespace sectionstrip_test;

namespace {

constexpr int kAlt = juce::ModifierKeys::altModifier;
constexpr int kCmd = juce::ModifierKeys::commandModifier;

// Section 1 over bars 5-6 (16..24) with bars 1-4 free before it; Section 2 over 32..36 with a gap between them.
struct GapSong : StripFixture {
    PlacementId one, two;
    GapSong() {
        doc.addClip(track, 16.0, 4.0, "One clip");
        doc.addClip(track, 32.0, 2.0, "Two clip");
        doc.makeSection(16.0, 24.0, "Section 1", 0xff56D4C0, 0.5);
        doc.makeSection(32.0, 36.0, "Section 2", 0xff4FC1FF, 0.5);
        strip.refresh();
        one = doc.getPlacements()[0].id;
        two = doc.getPlacements()[1].id;
    }
    double startOf(PlacementId id) const { return doc.getPlacement(id)->startBeat; }
    std::vector<double> starts() const {
        std::vector<double> out;
        for (const auto& p : doc.getPlacements())
            out.push_back(p.startBeat);
        return out;
    }
};

} // namespace

TEST(TimelineSectionStripFreeGapTest, OptionDragIntoTheFreeBarsBeforeTheOriginalPushesNothing) {
    GapSong f;
    const juce::Point<float> from{f.x(18.0), 12.0f}, to{f.x(2.0), 12.0f}; // grabbed 2 beats in; the copy's edge at 0
    f.strip.mouseDown(f.event(from, from, false, kAlt));
    f.strip.mouseDrag(f.event(to, from, true, kAlt));
    EXPECT_FALSE(f.strip.getBlockDragPlanForTest().opensRoom);
    EXPECT_EQ(f.strip.getDropHintTextForTest(), "Release to place a copy here");
    f.strip.mouseUp(f.event(to, from, true, kAlt));
    ASSERT_EQ(f.doc.getPlacements().size(), 3u);
    EXPECT_DOUBLE_EQ(f.doc.getPlacements()[0].startBeat, 0.0) << "the copy at bar 1";
    EXPECT_DOUBLE_EQ(f.startOf(f.one), 16.0) << "Section 1 stays at bar 5";
    EXPECT_DOUBLE_EQ(f.startOf(f.two), 32.0);
    EXPECT_TRUE(f.undo.undo());
    EXPECT_EQ(f.doc.getPlacements().size(), 2u) << "one undo step takes the copy away";
    EXPECT_DOUBLE_EQ(f.startOf(f.one), 16.0);
    EXPECT_FALSE(f.undo.undo());
}

TEST(TimelineSectionStripFreeGapTest, AnOccupiedSpotStillMakesRoomAndSaysInsert) {
    GapSong f;
    const juce::Point<float> from{f.x(18.0), 12.0f}, to{f.x(30.0), 12.0f}; // the copy's span 28..36 overlaps Section 2
    f.strip.mouseDown(f.event(from, from, false, kAlt));
    f.strip.mouseDrag(f.event(to, from, true, kAlt));
    EXPECT_TRUE(f.strip.getBlockDragPlanForTest().opensRoom);
    EXPECT_EQ(f.strip.getDropHintTextForTest(), "Release to insert a copy here");
    f.strip.mouseUp(f.event(to, from, true, kAlt));
    EXPECT_GT(f.startOf(f.two), 32.0) << "Section 2 moved later to make room";
}

// The doc after a drop equals the plan the drag showed, for every spot, copy or move, in make-room mode.
TEST(TimelineSectionStripFreeGapTest, TheDocAfterTheDropEqualsThePlanOverEverySpot) {
    for (const bool copy : {false, true})
        for (const int pressed : {0, 1})
            for (double toBeat = 0.0; toBeat <= 44.0; toBeat += 2.0) {
                GapSong f;
                const double blockStart = pressed == 0 ? 16.0 : 32.0;
                if (toBeat == blockStart)
                    continue; // no pointer travel: not a drag
                const juce::Point<float> from{f.x(blockStart + 1.0), 12.0f}, to{f.x(toBeat + 1.0), 12.0f};
                const int flags = copy ? kAlt : 0;
                f.strip.mouseDown(f.event(from, from, false, flags));
                f.strip.mouseDrag(f.event(to, from, true, flags));
                const auto plan = f.strip.getBlockDragPlanForTest();
                const auto before = f.doc.getPlacements();
                f.strip.mouseUp(f.event(to, from, true, flags));
                const auto label = juce::String("copy ") + juce::String((int)copy) + " pressed " +
                                   juce::String(pressed) + " to " + juce::String(toBeat);
                for (const auto& other : plan.others) {
                    const auto* now = f.doc.getPlacement(PlacementId{other.id});
                    ASSERT_NE(now, nullptr) << label;
                    EXPECT_DOUBLE_EQ(now->startBeat, other.start) << label;
                }
                for (const auto& [id, start] : plan.group) {
                    if (copy)
                        continue; // the copy has a new id: checked below
                    EXPECT_DOUBLE_EQ(f.doc.getPlacement(PlacementId{id})->startBeat, start) << label;
                }
                if (copy) {
                    ASSERT_EQ(f.doc.getPlacements().size(), before.size() + 1) << label;
                    bool found = false;
                    for (const auto& p : f.doc.getPlacements())
                        found |=
                            std::abs(p.startBeat - plan.landing) < 1e-9 &&
                            std::none_of(before.begin(), before.end(), [&](const auto& b) { return b.id == p.id; });
                    EXPECT_TRUE(found) << label << ": the copy is at the landing";
                }
            }
}

// Cmd+D places the section again after the block: into the gap there, nothing moves; without room, what follows moves.
TEST(TimelineSectionStripFreeGapTest, CmdDPlacesIntoAFreeGapAndPushesOnlyWithoutRoom) {
    GapSong f;
    f.strip.selectPlacement(f.one);
    ASSERT_TRUE(f.strip.keyPressed(juce::KeyPress('d', juce::ModifierKeys(kCmd), 0)));
    EXPECT_DOUBLE_EQ(f.startOf(f.two), 32.0) << "the 8-beat gap after Section 1 holds the copy";
    EXPECT_EQ(f.doc.getPlacements().size(), 3u);
    EXPECT_DOUBLE_EQ(f.doc.getPlacements()[1].startBeat, 24.0);
    f.strip.selectPlacement(f.one);
    ASSERT_TRUE(f.strip.keyPressed(juce::KeyPress('d', juce::ModifierKeys(kCmd), 0)));
    EXPECT_DOUBLE_EQ(f.doc.getPlacements()[1].startBeat, 24.0) << "a copy is placed after Section 1 again, inserted";
    EXPECT_GT(f.startOf(f.two), 32.0) << "the gap is full now, so Section 2 moves later";
}

// Option+Left/Right swap a block with its neighbour in the song order: the same plan as the drag, so a free span after
// the move leaves the blocks it does not cross where they are.
TEST(TimelineSectionStripFreeGapTest, OptionArrowsFollowThePlan) {
    GapSong f;
    f.strip.selectPlacement(f.one);
    ASSERT_TRUE(f.strip.keyPressed(juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys(kAlt), 0)));
    // Section 1's bars close up (Section 2 to 24) and it lands right after Section 2 (28..36), a free span: the plan
    // pushes nothing.
    EXPECT_DOUBLE_EQ(f.startOf(f.two), 24.0);
    EXPECT_DOUBLE_EQ(f.startOf(f.one), 28.0);
    EXPECT_TRUE(f.undo.undo());
    EXPECT_DOUBLE_EQ(f.startOf(f.one), 16.0);
    EXPECT_DOUBLE_EQ(f.startOf(f.two), 32.0);
}

// Music loose between blocks is never covered: a copy that would land on it makes room instead of stacking on it.
TEST(TimelineSectionStripFreeGapTest, ALooseClipInTheSpanStillMakesRoom) {
    GapSong f;
    f.doc.addClip(f.track, 26.0, 2.0, "Loose");
    f.strip.refresh();
    f.strip.selectPlacement(f.one);
    ASSERT_TRUE(f.strip.keyPressed(juce::KeyPress('d', juce::ModifierKeys(kCmd), 0)));
    EXPECT_DOUBLE_EQ(f.doc.getPlacements()[1].startBeat, 24.0);
    EXPECT_GT(f.startOf(f.two), 32.0) << "the copy at 24..32 met the loose clip, so what follows moved";
}
