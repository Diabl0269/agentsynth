// InsertGapMotionTests.cpp
//
// How the insert-between gap moves (docs/layout/animation.md#adding-a-module-between-others): the neighbours glide
// aside and back (160 ms easeOutCubic, the make-room glide, geometry final at once), a retarget, a drop, an undo or a
// second drag mid-glide carries on from where each card is drawn, the dropped module grows in with the small bounce
// and no outline, Reduce Motion moves the cards at once and fades the module in, Animations Off and an off-screen
// canvas land at once. Frames are stepped by hand through the animator's test seams.

#include "InsertGapTestFixture.h"

#include "UI/Graph/InsertGap/InsertGapKeyboard.h"

using namespace insert_gap_test;
using synth::ui::ExitEnterTimeline;

namespace {
/** A canvas whose glide animates although it is not on screen, with a row of `n` filters. */
struct Animated {
    Canvas c;
    std::vector<NodeID> row;
    explicit Animated(int n = 4, AnimationMode mode = AnimationMode::full)
        : c(mode) {
        row = c.filterRow(n);
        c.glide().setForceAnimateForTest(true);
    }
    juce::Rectangle<int> drawn(NodeID id) { return c.glide().currentRectFor(c.card(id)); }
};
} // namespace

TEST(InsertGapMotion, TheNeighboursGlideAsideWhileTheGeometryIsFinalAtOnce) {
    Animated a;
    const auto home = a.c.rect(a.row[1]);
    a.c.libraryEnter("VCA", between(a.c.rect(a.row[0]), home));
    const auto final = a.c.rect(a.row[1]);
    ASSERT_NE(final, home);
    EXPECT_EQ(a.c.stored(a.row[1]), final.getPosition()) << "the model is final at once";
    ASSERT_TRUE(a.c.glide().isLive());

    a.c.glide().stepFrameForTest(0.0f);
    EXPECT_EQ(a.drawn(a.row[1]), home) << "drawn where it was";
    a.c.glide().stepFrameForTest(0.5f);
    const auto mid = a.drawn(a.row[1]);
    EXPECT_GT(mid.getX(), home.getX());
    EXPECT_LT(mid.getX(), final.getX());
    a.c.glide().stepFrameForTest(1.0f);
    EXPECT_EQ(a.drawn(a.row[1]), final);
    EXPECT_EQ(a.drawn(a.row[0]), juce::Rectangle<int>()) << "the card before the gap does not glide";
    a.c.libraryExit("VCA", {0, 0});
}

TEST(InsertGapMotion, ClosingTheGapGlidesTheCardsBackFromWhereTheyAreDrawn) {
    Animated a;
    const auto home = a.c.rect(a.row[2]);
    a.c.libraryEnter("VCA", between(a.c.rect(a.row[1]), home));
    a.c.glide().stepFrameForTest(0.4f);
    const auto drawn = a.drawn(a.row[2]);
    ASSERT_GT(drawn.getX(), home.getX());

    a.c.libraryMove("VCA", {400, 2600});
    EXPECT_EQ(a.c.rect(a.row[2]), home) << "the geometry is home at once";
    EXPECT_EQ(a.drawn(a.row[2]), drawn) << "the card turns back from where it is drawn, no jump";
    a.c.glide().stepFrameForTest(1.0f);
    EXPECT_EQ(a.drawn(a.row[2]), home);
    a.c.libraryExit("VCA", {400, 2600});
}

TEST(InsertGapMotion, MovingToAnotherGapMidGlideContinuesFromTheDrawnRects) {
    Animated a(5);
    a.c.libraryEnter("VCA", between(a.c.rect(a.row[0]), a.c.rect(a.row[1])));
    a.c.glide().stepFrameForTest(0.5f);
    std::vector<juce::Rectangle<int>> drawn;
    for (auto id : a.row)
        drawn.push_back(a.drawn(id));

    // Over the gap between cards 2 and 3, as drawn now.
    a.c.libraryMove("VCA", between(a.c.rect(a.row[2]), a.c.rect(a.row[3])));
    ASSERT_TRUE(a.c.gap().isOpen());
    EXPECT_EQ(a.c.gap().plan()->target.anchorKey, "n:" + juce::String((juce::int64)a.row[3].uid));
    for (size_t i = 1; i < a.row.size(); ++i)
        if (!drawn[i].isEmpty() && !a.drawn(a.row[i]).isEmpty())
            EXPECT_EQ(a.drawn(a.row[i]), drawn[i]) << "card " << i << " carries on from where it is drawn";
    a.c.libraryExit("VCA", {0, 0});
}

TEST(InsertGapMotion, DroppingMidGlideKeepsTheGlideAndGrowsTheModuleInWithABounce) {
    Animated a;
    const auto p = between(a.c.rect(a.row[0]), a.c.rect(a.row[1]));
    a.c.libraryEnter("VCA", p);
    a.c.glide().stepFrameForTest(0.5f);
    const auto drawn = a.drawn(a.row[1]);

    const auto added = a.c.libraryDrop("VCA", p);
    ASSERT_NE(added.uid, 0u);
    EXPECT_EQ(a.drawn(a.row[1]), drawn) << "the drop does not restart or snap the glide";
    EXPECT_EQ(a.c.glide().enterGhostCount(), 1);
    EXPECT_EQ(a.c.glide().timeline().outlineMs(), 0.0) << "a new module gets no undo outline";
    EXPECT_TRUE(a.c.glide().timeline().bounce);

    // The grow phase overshoots and settles back to the card's own size.
    const auto& t = a.c.glide().timeline();
    const double growStart = t.exitMs() + t.gapMs();
    const auto cardRect = a.c.rect(added).toFloat();
    float widest = 0.0f;
    for (double ms = growStart; ms <= growStart + ExitEnterTimeline::kGrowMs; ms += 10.0) {
        a.c.glide().applyTimelineAtMs(ms);
        widest = std::max(widest, a.c.glide().ghostRectFor(added.uid).getWidth());
    }
    EXPECT_GT(widest, cardRect.getWidth() * 1.04f) << "the small bounce";
    EXPECT_LT(widest, cardRect.getWidth() * 1.09f);
    a.c.glide().applyTimelineAtMs(t.totalMs());
    EXPECT_EQ(a.c.card(added)->getAlpha(), 1.0f) << "live again once grown";
}

TEST(InsertGapMotion, AnUndoMidGlideContinuesFromTheDrawnRects) {
    Animated a;
    const auto p = between(a.c.rect(a.row[1]), a.c.rect(a.row[2]));
    a.c.dropBetween("VCA", p);
    a.c.glide().applyTimelineAtMs(60.0);
    const auto drawn = a.drawn(a.row[3]);
    ASSERT_FALSE(drawn.isEmpty());

    ASSERT_TRUE(a.c.undo.undo());
    EXPECT_EQ(a.drawn(a.row[3]), drawn) << "the undo turns the card back from where it is drawn";
}

TEST(InsertGapMotion, ASecondDragMidGlideContinuesFromTheDrawnRects) {
    Animated a(5);
    a.c.dropBetween("VCA", between(a.c.rect(a.row[0]), a.c.rect(a.row[1])));
    a.c.glide().applyTimelineAtMs(60.0);
    const auto drawn = a.drawn(a.row[4]);
    ASSERT_FALSE(drawn.isEmpty());

    a.c.libraryEnter("VCA", between(a.c.rect(a.row[3]), a.c.rect(a.row[4])));
    ASSERT_TRUE(a.c.gap().isOpen());
    EXPECT_EQ(a.drawn(a.row[4]), drawn);
    a.c.libraryExit("VCA", {0, 0});
}

TEST(InsertGapMotion, ReduceMotionMovesTheCardsAtOnceAndFadesTheModuleIn) {
    Animated a(4, AnimationMode::reduced);
    const auto p = between(a.c.rect(a.row[0]), a.c.rect(a.row[1]));
    a.c.libraryEnter("VCA", p);
    ASSERT_TRUE(a.c.gap().isOpen());
    EXPECT_FALSE(a.c.glide().isLive()) << "no glide under Reduce Motion";
    EXPECT_EQ(a.drawn(a.row[1]), juce::Rectangle<int>());

    const auto added = a.c.libraryDrop("VCA", p);
    ASSERT_NE(added.uid, 0u);
    ASSERT_EQ(a.c.glide().enterGhostCount(), 1);
    a.c.glide().applyTimelineAtMs(ExitEnterTimeline::kGrowMs / 2);
    EXPECT_EQ(a.c.glide().ghostRectFor(added.uid), a.c.rect(added).toFloat()) << "a fade in place, no growing";
}

TEST(InsertGapMotion, AnimationsOffLandsAtOnce) {
    Animated a(4, AnimationMode::off);
    const auto p = between(a.c.rect(a.row[0]), a.c.rect(a.row[1]));
    a.c.libraryEnter("VCA", p);
    ASSERT_TRUE(a.c.gap().isOpen());
    EXPECT_FALSE(a.c.glide().isLive());
    const auto added = a.c.libraryDrop("VCA", p);
    ASSERT_NE(added.uid, 0u);
    EXPECT_FALSE(a.c.glide().isLive());
    EXPECT_EQ(a.c.glide().enterGhostCount(), 0);
    EXPECT_EQ(a.c.card(added)->getAlpha(), 1.0f);
}

TEST(InsertGapMotion, OffScreenTheGeometryLandsAndNothingGhosts) {
    Canvas c; // not on screen, nothing forced
    const auto row = c.filterRow(3);
    const auto before = c.allRects();
    const auto added = c.dropBetween("VCA", between(c.rect(row[0]), c.rect(row[1])));
    ASSERT_NE(added.uid, 0u);
    EXPECT_EQ(c.glide().enterGhostCount(), 0);
    EXPECT_EQ(c.rect(row[1]), before.at(row[1].uid).translated(Canvas::shiftFor("VCA"), 0));
    EXPECT_EQ(c.card(added)->getAlpha(), 1.0f);
}

TEST(InsertGapMotion, TheBorderOfTheMacroGlidesWithTheCards) {
    Canvas c;
    const auto a = c.filter(400, 400), b = c.filter(760, 400);
    const auto macroId = c.openMacro({a, b});
    c.glide().setForceAnimateForTest(true);
    const auto hullBefore = c.hull(macroId);
    c.libraryEnter("VCA", between(c.rect(a), c.rect(b)));
    ASSERT_TRUE(c.gap().isOpen());
    EXPECT_GT(c.hull(macroId).getWidth(), hullBefore.getWidth());
    EXPECT_NE(c.editor.paintedMacroHullBounds(macroId), c.hull(macroId)) << "the painted border is still on its way";
    c.libraryExit("VCA", {0, 0});
}

TEST(InsertGapMotion, TheKeyboardInsertGlidesTheCardsAsideAndAtOnceUnderReduceMotion) {
    for (const auto mode : {AnimationMode::full, AnimationMode::reduced}) {
        SCOPED_TRACE(mode == AnimationMode::full ? "full" : "reduced");
        Animated a(4, mode);
        const auto home = a.c.rect(a.row[2]);
        a.c.editor.setSelectedNodes({a.row[1]});
        ASSERT_TRUE(synth::insertModuleAfterSelectedCard(a.c.editor, "VCA"));
        EXPECT_EQ(a.c.rect(a.row[2]), home.translated(Canvas::shiftFor("VCA"), 0)) << "geometry final at once";
        if (mode == AnimationMode::full) {
            a.c.glide().applyTimelineAtMs(0.0);
            EXPECT_EQ(a.drawn(a.row[2]), home) << "glides from where it was";
        } else {
            EXPECT_EQ(a.drawn(a.row[2]), juce::Rectangle<int>()) << "no glide under Reduce Motion";
        }
        EXPECT_EQ(a.c.glide().enterGhostCount(), 1) << "the module still arrives visibly";
    }
}
