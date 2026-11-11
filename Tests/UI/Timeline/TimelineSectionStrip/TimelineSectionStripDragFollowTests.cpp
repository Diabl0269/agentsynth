// TimelineSectionStripDragFollowTests.cpp -- a section drag shows what it will do and follows the view: the preview
// (drop line, neighbours making room, the settle) equals the commit in every mode and position, Shift flips the mode
// mid-drag, and a pointer held at the strip's edge scrolls the timeline with the preview following.
#include "TimelineSectionStripFixture.h"
#include "UI/Layout/KeyGlide/KeyedTween.h"
#include <algorithm>
#include <gtest/gtest.h>

using namespace sectionstrip_test;

namespace {

constexpr int kAlt = juce::ModifierKeys::altModifier;
constexpr int kShift = juce::ModifierKeys::shiftModifier;

// Intro over 0..8, Verse over 8..12, Chorus over 12..20: unequal lengths, so a wrong coordinate shows.
struct ThreeSections : StripFixture {
    std::vector<PlacementId> ids;
    ThreeSections() {
        doc.addClip(track, 0.0, 4.0, "A");
        doc.addClip(track, 8.0, 4.0, "B");
        doc.addClip(track, 12.0, 4.0, "C");
        doc.makeSection(0.0, 8.0, "Intro", 0xff56D4C0, 0.5);
        doc.makeSection(8.0, 12.0, "Verse", 0xff4FC1FF, 0.5);
        doc.makeSection(12.0, 20.0, "Chorus", 0xffC792EA, 0.5);
        strip.refresh();
        for (const auto& placement : doc.getPlacements())
            ids.push_back(placement.id);
    }
    juce::Point<float> grabPoint(PlacementId id) const { return {x(doc.getPlacement(id)->startBeat + 1.0), 12.0f}; }
};

} // namespace

// Every block, moved or copied, make-room or cover, dropped at every quarter bar along the strip: the doc after the
// drop is exactly the plan the drag showed.
TEST(TimelineSectionStripDragFollowTest, ThePreviewIsTheCommitForEveryModeAndPosition) {
    for (int which = 0; which < 3; ++which)
        for (const bool copy : {false, true})
            for (const bool cover : {false, true})
                for (double left = 0.0; left <= 26.0; left += 1.0) {
                    ThreeSections f;
                    const auto dragged = f.ids[(size_t)which];
                    const auto from = f.grabPoint(dragged);
                    const juce::Point<float> to{f.x(left + 1.0), 12.0f};
                    const int flags = (copy ? kAlt : 0) | (cover ? kShift : 0);
                    f.strip.mouseDown(f.event(from, from, false, flags));
                    f.strip.mouseDrag(f.event(to, from, true, flags));
                    if (!f.strip.isBlockDraggingForTest())
                        continue; // too small a move to start a drag
                    const auto plan = f.strip.getBlockDragPlanForTest();
                    f.strip.mouseUp(f.event(to, from, true, flags));
                    const std::string where = "block " + std::to_string(which) + (copy ? " copy" : " move") +
                                              (cover ? " cover" : " make-room") + " at " + std::to_string(left);
                    const SectionPlacement* placed = copy ? nullptr : f.doc.getPlacement(dragged);
                    for (const auto& placement : f.doc.getPlacements())
                        if (copy && std::find(f.ids.begin(), f.ids.end(), placement.id) == f.ids.end())
                            placed = &placement;
                    ASSERT_NE(placed, nullptr) << where;
                    EXPECT_DOUBLE_EQ(placed->startBeat, plan.landing) << where;
                    for (const auto& other : plan.others) {
                        const auto* now = f.doc.getPlacement(PlacementId{other.id});
                        if (other.covered) {
                            EXPECT_EQ(now, nullptr) << where << ": covered " << other.id;
                        } else {
                            ASSERT_NE(now, nullptr) << where << ": lost " << other.id;
                            EXPECT_DOUBLE_EQ(now->startBeat, other.start) << where << ": other " << other.id;
                        }
                    }
                }
}

TEST(TimelineSectionStripDragFollowTest, ShiftPressedMidDragFlipsThePreviewAndTheDrop) {
    ThreeSections f;
    const auto from = f.grabPoint(f.ids[0]);
    const juce::Point<float> to{f.x(8.0 + 1.0), 12.0f};
    f.strip.mouseDown(f.event(from, from, false));
    f.strip.mouseDrag(f.event(to, from, true));
    EXPECT_FALSE(f.strip.getBlockDragPlanForTest().isCovered(f.ids[2].value)) << "make room by default";
    EXPECT_TRUE(f.strip.getTooltip().isEmpty()) << "no tooltip over a drag in flight";
    f.strip.modifierKeysChanged(juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | kShift));
    EXPECT_TRUE(f.strip.getBlockDragPlanForTest().isCovered(f.ids[1].value)) << "Shift shows the cover at once";
    f.strip.modifierKeysChanged(juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier));
    EXPECT_FALSE(f.strip.getBlockDragPlanForTest().isCovered(f.ids[1].value)) << "and back on release";
    f.strip.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
}

TEST(TimelineSectionStripDragFollowTest, TheNeighboursGlideToTheirPlannedPlacesAndBackOnEsc) {
    ThreeSections f;
    f.strip.getDropGlideForTest().forceAnimateForTest(true);
    const auto intro = f.ids[0];
    const auto from = f.grabPoint(f.ids[2]);
    const juce::Point<float> to{f.x(0.0 + 1.0), 12.0f};
    f.strip.mouseDown(f.event(from, from, false));
    f.strip.mouseDrag(f.event(to, from, true)); // the Chorus to the front: the Intro makes room
    ASSERT_DOUBLE_EQ(*f.strip.getBlockDragPlanForTest().startOf(intro.value), 8.0);
    f.strip.getDropGlideForTest().stepForTest(40.0);
    const double midway = f.strip.getDrawnStartBeatForTest(intro);
    EXPECT_GT(midway, 0.0) << "it moves...";
    EXPECT_LT(midway, 8.0) << "...over time, not in one jump";
    f.strip.getDropGlideForTest().completeForTest();
    EXPECT_DOUBLE_EQ(f.strip.getDrawnStartBeatForTest(intro), 8.0);
    EXPECT_DOUBLE_EQ(f.doc.getPlacement(intro)->startBeat, 0.0) << "nothing is written while dragging";
    f.strip.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    f.strip.getDropGlideForTest().completeForTest();
    EXPECT_DOUBLE_EQ(f.strip.getDrawnStartBeatForTest(intro), 0.0) << "Esc eases it back";
}

TEST(TimelineSectionStripDragFollowTest, TheGhostSettlesWhereTheDropLineWas) {
    ThreeSections f;
    const auto from = f.grabPoint(f.ids[0]);
    const juce::Point<float> to{f.x(4.6 + 1.0), 12.0f};
    f.strip.mouseDown(f.event(from, from, false));
    f.strip.mouseDrag(f.event(to, from, true));
    const double landing = f.strip.getBlockDragTargetBeatForTest();
    f.strip.mouseUp(f.event(to, from, true));
    f.strip.getMotionForTest().stepForTest(5000.0);
    EXPECT_FLOAT_EQ(f.strip.getDragGhostBoundsForTest().getX(), f.x(landing));
    EXPECT_DOUBLE_EQ(f.doc.getPlacement(f.ids[0])->startBeat, landing);
}

TEST(TimelineSectionStripDragFollowTest, HoldingABlockAtTheRightEdgeScrollsTheViewAndTheDropFollows) {
    ThreeSections f;
    int scrolled = 0;
    f.strip.onViewScrolledByDrag = [&] { ++scrolled; };
    const auto from = f.grabPoint(f.ids[0]);
    const juce::Point<float> edge{(float)f.strip.getWidth() - 4.0f, 12.0f};
    f.strip.mouseDown(f.event(from, from, false));
    f.strip.mouseDrag(f.event(edge, from, true));
    EXPECT_TRUE(f.strip.isEdgeScrollRunningForTest());
    const double before = f.strip.getBlockDragTargetBeatForTest();
    for (int i = 0; i < 20; ++i)
        f.strip.tickEdgeScrollForTest();
    EXPECT_GT(f.state.firstVisibleBeat, 0.0);
    EXPECT_GT(f.strip.getBlockDragTargetBeatForTest(), before) << "the drop line follows the scrolled view";
    EXPECT_EQ(scrolled, 20) << "the panel hears each scroll (follow-playhead off, ruler repainted)";
    const double landing = f.strip.getBlockDragTargetBeatForTest();
    f.strip.mouseUp(f.event(edge, from, true));
    EXPECT_FALSE(f.strip.isEdgeScrollRunningForTest());
    EXPECT_DOUBLE_EQ(f.doc.getPlacement(f.ids[0])->startBeat, landing);
}

TEST(TimelineSectionStripDragFollowTest, TheLeftEdgeStopsAtTheSongStartAndEscStopsTheScroll) {
    ThreeSections f;
    const auto from = f.grabPoint(f.ids[2]);
    const juce::Point<float> edge{3.0f, 12.0f};
    f.strip.mouseDown(f.event(from, from, false));
    f.strip.mouseDrag(f.event(edge, from, true));
    EXPECT_TRUE(f.strip.isEdgeScrollRunningForTest());
    f.strip.tickEdgeScrollForTest();
    EXPECT_DOUBLE_EQ(f.state.firstVisibleBeat, 0.0) << "nothing before beat 0";
    f.strip.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    EXPECT_FALSE(f.strip.isEdgeScrollRunningForTest());
    f.strip.mouseUp(f.event(edge, from, true));
}

TEST(TimelineSectionStripDragFollowTest, MakingASectionAtTheEdgeScrollsAndGrowsTheOutline) {
    ThreeSections f;
    const juce::Point<float> start{f.x(22.0), 12.0f}, edge{(float)f.strip.getWidth() - 4.0f, 12.0f};
    f.strip.mouseDown(f.event(start, start, false));
    f.strip.mouseDrag(f.event(edge, start, true));
    ASSERT_TRUE(f.strip.isMakeDraggingForTest());
    EXPECT_TRUE(f.strip.isEdgeScrollRunningForTest());
    const double end = f.strip.getMakeDragSpanForTest().end;
    for (int i = 0; i < 20; ++i)
        f.strip.tickEdgeScrollForTest();
    EXPECT_GT(f.strip.getMakeDragSpanForTest().end, end);
    const auto shown = f.strip.getMakeDragSpanForTest();
    f.strip.mouseUp(f.event(edge, start, true));
    EXPECT_FALSE(f.strip.isEdgeScrollRunningForTest());
    const auto& made = f.doc.getPlacements().back();
    EXPECT_DOUBLE_EQ(made.startBeat, shown.start);
    EXPECT_DOUBLE_EQ(f.doc.getSection(made.section)->lengthBeats, shown.length());
}

TEST(TimelineSectionStripDragFollowTest, UndoOfAMoveGlidesTheBlocksBackInsteadOfJumping) {
    ThreeSections f;
    auto& glide = f.strip.getDropGlideForTest();
    glide.forceAnimateForTest(true);
    const auto from = f.grabPoint(f.ids[2]);
    const juce::Point<float> to{f.x(0.0 + 1.0), 12.0f};
    f.strip.mouseDown(f.event(from, from, false));
    f.strip.mouseDrag(f.event(to, from, true));
    f.strip.mouseUp(f.event(to, from, true));
    glide.completeForTest();
    const auto intro = f.ids[0];
    ASSERT_DOUBLE_EQ(f.doc.getPlacement(intro)->startBeat, 8.0);
    EXPECT_DOUBLE_EQ(f.strip.getDrawnStartBeatForTest(intro), 8.0) << "the commit lands where the plan drew it";
    ASSERT_TRUE(f.undo.undo());
    f.strip.refresh();
    EXPECT_DOUBLE_EQ(f.doc.getPlacement(intro)->startBeat, 0.0);
    EXPECT_DOUBLE_EQ(f.strip.getDrawnStartBeatForTest(intro), 8.0) << "drawn where it was at the undo...";
    glide.stepForTest(40.0);
    const double midway = f.strip.getDrawnStartBeatForTest(intro);
    EXPECT_GT(midway, 0.0);
    EXPECT_LT(midway, 8.0) << "...and easing back";
    glide.completeForTest();
    EXPECT_DOUBLE_EQ(f.strip.getDrawnStartBeatForTest(intro), 0.0);
}
