// InsertGapGhostTests.cpp
//
// The landing ghost of a drag slides to the spot it is aimed at instead of jumping there
// (docs/layout/insert-motion.md#the-hovered-module-slides-to-the-gap): small pointer steps are followed one to one, the
// jump into a gap a drag opens between two cards is a 160 ms easeOutCubic slide that carries on from where the ghost is
// drawn when the aim changes again, and Reduce Motion, Animations Off and an off-screen canvas land it at once. Real
// library drag calls on a real canvas; frames are stepped by hand through the editor's test seam.

#include "InsertGapTestFixture.h"

using namespace insert_gap_test;

namespace {
struct Sliding {
    Canvas c;
    std::vector<NodeID> row;
    explicit Sliding(AnimationMode mode = AnimationMode::full)
        : c(mode) {
        row = c.filterRow(3);
        c.editor.getDragDropController().getGhostSlide().setForceForTest(true);
    }
    juce::Rectangle<float> drawn() { return c.editor.getDragDropController().getGhostSlide().drawn(); }
    Rect target() { return c.editor.getDragDropController().getDragPreviewGhost(); }
    /** Hovers in open space, then over the gap between the first two cards. */
    void hoverThenGap() {
        c.libraryEnter("VCA", {600, 2400});
        c.libraryMove("VCA", between(c.rect(row[0]), c.rect(row[1])));
    }
};
} // namespace

TEST(InsertGapGhost, TheGhostSlidesIntoTheGapInsteadOfJumping) {
    Sliding s;
    s.c.libraryEnter("VCA", {600, 2400});
    const auto start = s.drawn();
    ASSERT_FALSE(start.isEmpty());
    s.c.libraryMove("VCA", between(s.c.rect(s.row[0]), s.c.rect(s.row[1])));
    ASSERT_TRUE(s.c.gap().isOpen());
    const auto target = s.target();
    ASSERT_NE(target.getPosition(), start.getPosition().toInt());
    ASSERT_TRUE(s.c.editor.getDragDropController().getGhostSlide().isSliding());

    s.c.editor.getDragDropController().getGhostSlide().applyTweenAt(0.0f);
    EXPECT_EQ(s.drawn().getPosition(), start.getPosition()) << "drawn where it was";
    s.c.editor.getDragDropController().getGhostSlide().applyTweenAt(0.5f);
    const auto mid = s.drawn();
    EXPECT_GT(mid.getY(), target.getY()) << "part of the way (it comes from below)";
    EXPECT_LT(mid.getY(), start.getY());
    s.c.editor.getDragDropController().getGhostSlide().applyTweenAt(1.0f);
    EXPECT_EQ(s.drawn().toNearestInt(), target) << "lands on the slot";
    EXPECT_FALSE(s.c.editor.getDragDropController().getGhostSlide().isSliding());
    s.c.libraryExit("VCA", {0, 0});
}

TEST(InsertGapGhost, AnotherAimMidSlideCarriesOnFromTheDrawnGhost) {
    Sliding s;
    s.hoverThenGap();
    s.c.editor.getDragDropController().getGhostSlide().applyTweenAt(0.5f);
    const auto drawnNow = s.drawn();
    s.c.libraryMove("VCA", between(s.c.rect(s.row[1]), s.c.rect(s.row[2]))); // the next gap along
    s.c.editor.getDragDropController().getGhostSlide().applyTweenAt(0.0f);
    EXPECT_EQ(s.drawn().getPosition(), drawnNow.getPosition())
        << "turns from where it is drawn, not from where it began";
    s.c.editor.getDragDropController().getGhostSlide().applyTweenAt(1.0f);
    EXPECT_EQ(s.drawn().toNearestInt(), s.target());
    s.c.libraryExit("VCA", {0, 0});
}

TEST(InsertGapGhost, SmallPointerStepsAreFollowedOneToOne) {
    Sliding s;
    s.c.libraryEnter("VCA", {600, 2400});
    for (int step = 1; step <= 5; ++step) {
        s.c.libraryMove("VCA", {600 + step * 8, 2400});
        EXPECT_FALSE(s.c.editor.getDragDropController().getGhostSlide().isSliding()) << "step " << step;
        EXPECT_EQ(s.drawn().toNearestInt(), s.target()) << "the ghost is where the aim is";
    }
    s.c.libraryExit("VCA", {0, 0});
}

TEST(InsertGapGhost, TheDropAndTheEndOfTheDragForgetTheGhost) {
    Sliding s;
    s.hoverThenGap();
    ASSERT_TRUE(s.c.editor.getDragDropController().getGhostSlide().isSliding());
    s.c.libraryExit("VCA", {0, 0});
    EXPECT_FALSE(s.c.editor.getDragDropController().getGhostSlide().isSliding());
    EXPECT_TRUE(s.drawn().isEmpty());
}

TEST(InsertGapGhost, ReduceMotionLandsTheGhostAtOnce) {
    Sliding s(AnimationMode::reduced);
    s.hoverThenGap();
    EXPECT_FALSE(s.c.editor.getDragDropController().getGhostSlide().isSliding());
    EXPECT_EQ(s.drawn().toNearestInt(), s.target());
    s.c.libraryExit("VCA", {0, 0});
}

TEST(InsertGapGhost, AnimationsOffLandsTheGhostAtOnce) {
    Sliding s(AnimationMode::off);
    s.hoverThenGap();
    EXPECT_FALSE(s.c.editor.getDragDropController().getGhostSlide().isSliding());
    EXPECT_EQ(s.drawn().toNearestInt(), s.target());
    s.c.libraryExit("VCA", {0, 0});
}

TEST(InsertGapGhost, OffScreenTheGhostLandsAtOnce) {
    Canvas c;
    const auto row = c.filterRow(3);
    c.libraryEnter("VCA", {600, 2400});
    c.libraryMove("VCA", between(c.rect(row[0]), c.rect(row[1])));
    EXPECT_FALSE(c.editor.getDragDropController().getGhostSlide().isSliding());
    EXPECT_EQ(c.editor.getDragDropController().getGhostSlide().drawn().toNearestInt(),
              c.editor.getDragDropController().getDragPreviewGhost());
    c.libraryExit("VCA", {0, 0});
}

TEST(InsertGapGhost, ACanvasCardDragSlidesItsGhostIntoTheGapToo) {
    Sliding s;
    const auto extra = s.c.filter(600, 2400);
    s.c.editor.finishCardGlideForTest();
    s.c.press(extra);
    s.c.dragCentreTo(extra, {900, 2700}); // somewhere free first
    const auto start = s.drawn();
    s.c.dragCentreTo(extra, between(s.c.rect(s.row[0]), s.c.rect(s.row[1])));
    ASSERT_TRUE(s.c.gap().isOpen());
    ASSERT_TRUE(s.c.editor.getDragDropController().getGhostSlide().isSliding());
    s.c.editor.getDragDropController().getGhostSlide().applyTweenAt(0.0f);
    EXPECT_EQ(s.drawn().getPosition(), start.getPosition());
    s.c.editor.getDragDropController().getGhostSlide().applyTweenAt(1.0f);
    EXPECT_EQ(s.drawn().toNearestInt(), s.target());
    s.c.release(extra);
}
