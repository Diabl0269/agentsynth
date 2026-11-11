// TimelineSectionStripMotionTests.cpp -- what the Sections strip animates: a new block grows from its anchor, a
// deleted one shrinks away, a dragged one lifts and settles, stamped placements flash an outline. Each effect starts
// only when the strip can animate, honours Reduce Motion and Animations Off, and has stopped once it has landed.
#include "TimelineSectionStripFixture.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/ReducedMotion.h"
#include <gtest/gtest.h>

using namespace sectionstrip_test;

namespace {

// Lets an off-screen strip animate (no VBlank reaches it, so the tests step the drivers by hand) and puts every
// global back afterwards.
struct MotionScope {
    explicit MotionScope(bool animate = true, std::optional<bool> reduced = false) {
        FadeVisibility::setAnimateOffScreenForTest(animate);
        setAnimationMode(AnimationMode::followSystem);
        setReducedMotionForTest(reduced);
    }
    ~MotionScope() {
        FadeVisibility::setAnimateOffScreenForTest(false);
        setReducedMotionForTest(std::nullopt);
        setAnimationMode(AnimationMode::followSystem);
    }
};

struct MotionFixture : StripFixture {
    PlacementId first, second;
    ClipId clip;
    MotionFixture() {
        clip = doc.addClip(track, 0.0, 4.0, "A");
        const auto id = doc.makeSection(0.0, 8.0, "Chorus", 0xff56D4C0, 0.5);
        doc.placeSection(id, 16.0, SectionDropMode::MakeRoom, 0.5);
        strip.refresh();
        first = doc.getPlacements()[0].id;
        second = doc.getPlacements()[1].id;
    }
    PlacementId newest() const { return doc.getPlacements().back().id; }
};

} // namespace

TEST(TimelineSectionStripMotionTest, ANewBlockGrowsInFromTheSideTheDragStartedAndThenStops) {
    MotionScope scope;
    MotionFixture f;
    f.dragAcross(24.0, 28.0); // left to right: anchored on the left
    auto& motion = f.strip.getMotionForTest();
    const auto made = f.newest();
    ASSERT_TRUE(motion.isGrowingForTest()) << "the animator is started";
    EXPECT_FALSE(motion.growsFromRight(made));
    EXPECT_LT(motion.growScale(made), 0.1f) << "frame zero is nearly nothing";
    EXPECT_FLOAT_EQ(motion.growAlpha(made), 1.0f);
    EXPECT_DOUBLE_EQ(motion.getGrowDurationMsForTest(), SectionStripMotion::kGrowMs);
    motion.stepForTest(0.0);
    motion.stepForTest(80.0);
    EXPECT_GT(motion.growScale(made), 0.3f);
    EXPECT_LT(motion.growScale(made), 1.0f);
    motion.stepForTest(400.0);
    EXPECT_FLOAT_EQ(motion.growScale(made), 1.0f);
    EXPECT_FALSE(motion.isAnimating()) << "nothing runs once it has landed";
}

TEST(TimelineSectionStripMotionTest, ADragFromTheRightEdgeGrowsFromTheRight) {
    MotionScope scope;
    MotionFixture f;
    f.dragAcross(32.0, 28.0); // right to left
    EXPECT_TRUE(f.strip.getMotionForTest().growsFromRight(f.newest()));
}

TEST(TimelineSectionStripMotionTest, UnderReduceMotionAGrowIsAShortFadeAndTheWidthIsFinalAtOnce) {
    MotionScope scope(true, true);
    MotionFixture f;
    f.dragAcross(24.0, 28.0);
    auto& motion = f.strip.getMotionForTest();
    const auto made = f.newest();
    ASSERT_TRUE(motion.isGrowingForTest());
    EXPECT_DOUBLE_EQ(motion.getGrowDurationMsForTest(), SectionStripMotion::kReducedMs);
    EXPECT_FLOAT_EQ(motion.growScale(made), 1.0f) << "no growing under Reduce Motion";
    EXPECT_LT(motion.growAlpha(made), 0.1f);
}

TEST(TimelineSectionStripMotionTest, UnderAnimationsOffOrOffScreenNothingStartsAndEverythingIsFinal) {
    {
        MotionScope scope;
        setAnimationMode(AnimationMode::off);
        MotionFixture f;
        f.dragAcross(24.0, 28.0);
        EXPECT_FALSE(f.strip.getMotionForTest().isAnimating());
        EXPECT_FLOAT_EQ(f.strip.getMotionForTest().growScale(f.newest()), 1.0f);
    }
    {
        MotionScope scope(false); // the strip is not showing: no frames would ever come
        MotionFixture f;
        f.dragAcross(24.0, 28.0);
        EXPECT_FALSE(f.strip.getMotionForTest().isAnimating());
    }
}

TEST(TimelineSectionStripMotionTest, DeletingABlockShrinksItsPictureTowardItsCentreThenStops) {
    MotionScope scope;
    MotionFixture f;
    ASSERT_TRUE(f.strip.applySectionMenuChoice(f.second, TimelineSectionStrip::SectionMenuChoice::DeleteWithBars));
    auto& motion = f.strip.getMotionForTest();
    ASSERT_TRUE(motion.hasExitGhost());
    EXPECT_FLOAT_EQ(motion.exitFrames().front().scale, 1.0f);
    motion.stepForTest(0.0);
    motion.stepForTest(90.0);
    EXPECT_LT(motion.exitFrames().front().scale, 1.0f);
    EXPECT_GT(motion.exitFrames().front().scale, 0.0f);
    motion.stepForTest(400.0);
    EXPECT_FALSE(motion.hasExitGhost());
    EXPECT_FALSE(motion.isAnimating());
}

TEST(TimelineSectionStripMotionTest, UnderReduceMotionADeleteFadesInsteadOfShrinking) {
    MotionScope scope(true, true);
    MotionFixture f;
    f.strip.applySectionMenuChoice(f.second, TimelineSectionStrip::SectionMenuChoice::DeleteWithBars);
    auto& motion = f.strip.getMotionForTest();
    ASSERT_TRUE(motion.hasExitGhost());
    motion.stepForTest(0.0);
    motion.stepForTest(40.0);
    EXPECT_FLOAT_EQ(motion.exitFrames().front().scale, 1.0f);
    EXPECT_LT(motion.exitFrames().front().alpha, 1.0f);
}

TEST(TimelineSectionStripMotionTest, UndoingADeleteGrowsTheBlockBackButALoadDoesNot) {
    MotionScope scope;
    MotionFixture f;
    f.strip.applySectionMenuChoice(f.second, TimelineSectionStrip::SectionMenuChoice::DeleteWithBars);
    f.strip.getMotionForTest().stepForTest(0.0);
    f.strip.getMotionForTest().stepForTest(400.0);
    ASSERT_TRUE(f.undo.undo());
    f.strip.refresh();
    EXPECT_TRUE(f.strip.getMotionForTest().isGrowingForTest()) << "undo glides like the edit did";

    MotionFixture g;
    g.strip.setTimelineDoc(&g.doc); // a new document: the blocks are simply there
    EXPECT_FALSE(g.strip.getMotionForTest().isAnimating());
}

TEST(TimelineSectionStripMotionTest, TheLiftEasesUpOnDragAndSettlesBackOnEscAndLandsAtOnceUnderReduceMotion) {
    {
        MotionScope scope;
        MotionFixture f;
        const juce::Point<float> from{f.x(2.0), 12.0f}, to{f.x(26.0), 12.0f};
        f.strip.mouseDown(f.event(from, from, false));
        f.strip.mouseDrag(f.event(to, from, true));
        auto& motion = f.strip.getMotionForTest();
        ASSERT_TRUE(motion.isLiftingForTest());
        EXPECT_FLOAT_EQ(motion.lift(), 0.0f);
        motion.stepForTest(0.0);
        motion.stepForTest(300.0);
        EXPECT_FLOAT_EQ(motion.lift(), 1.0f);
        f.strip.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
        EXPECT_TRUE(motion.isLiftingForTest()) << "the settle is animated";
        motion.stepForTest(0.0);
        motion.stepForTest(300.0);
        EXPECT_FLOAT_EQ(motion.lift(), 0.0f);
        EXPECT_FALSE(motion.isAnimating());
    }
    {
        MotionScope scope(true, true);
        MotionFixture f;
        const juce::Point<float> from{f.x(2.0), 12.0f}, to{f.x(26.0), 12.0f};
        f.strip.mouseDown(f.event(from, from, false));
        f.strip.mouseDrag(f.event(to, from, true));
        EXPECT_FALSE(f.strip.getMotionForTest().isLiftingForTest()) << "a lift is movement: no fade form, so none";
        EXPECT_FLOAT_EQ(f.strip.getMotionForTest().lift(), 1.0f);
        f.strip.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
        EXPECT_FLOAT_EQ(f.strip.getMotionForTest().lift(), 0.0f);
    }
}

TEST(TimelineSectionStripMotionTest, AnEditThatReachesAnotherCopyOutlinesTheRewrittenPlacementsForFourHundredMs) {
    MotionScope scope;
    MotionFixture f;
    ClipId inSecond;
    for (const auto& c : f.doc.getTrack(f.track)->clips)
        if (c.startBeat == 16.0)
            inSecond = c.id;
    ASSERT_TRUE(inSecond.isValid());
    f.doc.setClipName(inSecond, "Edited");
    ASSERT_FALSE(f.doc.getLastStampedPlacements().empty());
    const auto stamped = f.doc.getLastStampedPlacements().front();
    f.strip.refresh(); // what the panel does on every doc notification
    auto& motion = f.strip.getMotionForTest();
    ASSERT_TRUE(motion.isOutliningForTest());
    EXPECT_TRUE(motion.isOutlined(stamped));
    const auto other = stamped == f.first ? f.second : f.first;
    EXPECT_FALSE(motion.isOutlined(other)) << "only the placements that were rewritten";
    EXPECT_DOUBLE_EQ(motion.getOutlineDurationMsForTest(), SectionStripMotion::kOutlineMs);
    motion.stepForTest(0.0);
    motion.stepForTest(200.0);
    EXPECT_GT(motion.outlineAlpha(), 0.0f);
    EXPECT_LT(motion.outlineAlpha(), 1.0f);
    motion.stepForTest(500.0);
    EXPECT_FALSE(motion.isOutlined(stamped));
    EXPECT_FALSE(motion.isAnimating());
}

TEST(TimelineSectionStripMotionTest, UnderReduceMotionTheOutlineIsAShortFade) {
    MotionScope scope(true, true);
    MotionFixture f;
    ClipId inSecond;
    for (const auto& c : f.doc.getTrack(f.track)->clips)
        if (c.startBeat == 16.0)
            inSecond = c.id;
    f.doc.setClipName(inSecond, "Edited");
    f.strip.refresh();
    EXPECT_DOUBLE_EQ(f.strip.getMotionForTest().getOutlineDurationMsForTest(), SectionStripMotion::kReducedMs);
}

TEST(TimelineSectionStripMotionTest, AnEditThatStampsNothingOutlinesNothing) {
    MotionScope scope;
    MotionFixture f;
    f.doc.addClip(f.track, 40.0, 2.0, "Outside any section");
    f.strip.refresh();
    EXPECT_FALSE(f.strip.getMotionForTest().isOutliningForTest());
}
