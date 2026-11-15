// TimelineSectionStripOriginTests.cpp -- frame by frame, a block leaves the way it came and comes back the way it
// left: the undo of Cmd+D shrinks the copy into the original's right edge while the block it pushed glides back flush
// against it, a delete shrinks toward the middle and its undo grows from there, a second step mid-motion starts
// from the size the block is drawn at, and the clips under a block keep its origin.
#include "TimelineSectionStripThreeBlocksFixture.h"
#include "UI/Timeline/ClipSelectionModel.h"
#include "UI/Timeline/TimelineClipLaneArea/ClipLaneEnterExit.h"
#include "UI/Timeline/TimelineClipLaneArea/TimelineClipLaneArea.h"
#include <gtest/gtest.h>

using namespace sectionstrip_test;

namespace {

constexpr double kFrameMs = 20.0;
const juce::KeyPress kCmdD('d', juce::ModifierKeys::commandModifier, 0);

// ThreeBlocks with its drop glide live off screen; Cmd+D on the Intro puts a copy at [8,16) and the Verse at 16.
struct OriginRig : ThreeBlocks {
    OriginRig() { getDropGlideForTest().forceAnimateForTest(true); }
    KeyedPointTween& getDropGlideForTest() { return strip.getDropGlideForTest(); }
    PlacementId copyOfIntro() {
        strip.selectPlacements({intro}, intro);
        EXPECT_TRUE(strip.keyPressed(kCmdD));
        for (const auto& p : doc.getPlacements())
            if (p.id != intro && p.id != verse && p.id != bridge)
                return p.id;
        return {};
    }
    void land() {
        stepStrip(strip, 0.0);
        stepStrip(strip, 600.0);
    }
    juce::Rectangle<float> restRect(double start, double end) const {
        return sectionstrip::blockRect(x(start), x(end), strip.getHeight());
    }
};

} // namespace

// Regression test for FRO1757: the undone copy shrank toward its own middle, on top of the block that had already
// slid back.
TEST(TimelineSectionStripOriginTest, UndoOfCmdDShrinksTheCopyIntoTheOriginalsEdgeWithTheNextBlockFlushBehindIt) {
    FullMotion motion;
    OriginRig f;
    const auto copy = f.copyOfIntro();
    ASSERT_TRUE(copy.isValid());
    ASSERT_DOUBLE_EQ(f.doc.getPlacement(f.verse)->startBeat, 16.0);
    f.land();
    ASSERT_TRUE(f.undo.undo());
    ASSERT_EQ(f.doc.getPlacement(copy), nullptr);
    stepStrip(f.strip, 0.0);
    float lastRight = 1.0e9f;
    int frames = 0;
    for (double ms = kFrameMs; ms < SectionStripMotion::kExitMs; ms += kFrameMs) {
        stepStrip(f.strip, ms);
        const auto exits = f.strip.getMotionForTest().exitFrames();
        ASSERT_EQ(exits.size(), 1u) << ms;
        const auto rect = exits.front().rect;
        EXPECT_NEAR(rect.getX(), f.x(8.0), 1.0f) << ms << " ms: the edge touching the original stays put";
        EXPECT_FLOAT_EQ(rect.getHeight(), exits.front().ghost.bounds.getHeight()) << "an edge exit keeps its height";
        EXPECT_LT(rect.getRight(), lastRight) << ms << " ms: the far edge moves in";
        lastRight = rect.getRight();
        const float verseLeft = f.x(f.strip.getDrawnStartBeatForTest(f.verse));
        EXPECT_NEAR(verseLeft, rect.getRight(), 1.5f) << ms << " ms: the Verse comes back flush, never over it";
        ++frames;
    }
    EXPECT_GE(frames, 7);
    f.land();
    EXPECT_TRUE(f.strip.getMotionForTest().exitFrames().empty());
    EXPECT_DOUBLE_EQ(f.strip.getDrawnStartBeatForTest(f.verse), 8.0);
}

TEST(TimelineSectionStripOriginTest, ADeleteShrinksTowardTheMiddleAndItsUndoGrowsFromTheMiddle) {
    FullMotion motion;
    OriginRig f;
    const float middle = f.x(10.0);
    ASSERT_TRUE(f.strip.applySectionMenuChoice(f.verse, TimelineSectionStrip::SectionMenuChoice::DeleteWithBars));
    stepStrip(f.strip, 0.0);
    for (double ms = kFrameMs; ms < SectionStripMotion::kExitMs; ms += kFrameMs) {
        stepStrip(f.strip, ms);
        const auto exits = f.strip.getMotionForTest().exitFrames();
        ASSERT_EQ(exits.size(), 1u);
        EXPECT_NEAR(exits.front().rect.getCentreX(), middle, 0.5f) << ms << " ms: the middle stays put";
    }
    f.land();

    ASSERT_TRUE(f.undo.undo());
    ASSERT_NE(f.doc.getPlacement(f.verse), nullptr);
    auto& m = f.strip.getMotionForTest();
    m.stepForTest(0.0);
    const auto xAt = [&](double beat) { return f.x(beat); };
    for (double ms = kFrameMs; ms < SectionStripMotion::kGrowMs; ms += kFrameMs) {
        m.stepForTest(ms);
        ASSERT_LT(m.growScale(f.verse), 1.0f) << ms;
        const auto drawn = m.growRect(f.verse, f.restRect(8.0, 12.0), xAt);
        EXPECT_NEAR(drawn.getCentreX(), middle, 0.5f) << ms << " ms: it grows from the middle it shrank to";
    }
}

TEST(TimelineSectionStripOriginTest, ACopysUndoAndRedoStartedMidMotionStartFromTheDrawnSize) {
    FullMotion motion;
    OriginRig f;
    const auto copy = f.copyOfIntro();
    auto& m = f.strip.getMotionForTest();
    stepStrip(f.strip, 0.0);
    stepStrip(f.strip, 60.0);
    const float grown = m.growScale(copy);
    ASSERT_GT(grown, 0.1f);
    ASSERT_LT(grown, 1.0f);
    ASSERT_TRUE(f.undo.undo());
    auto exits = m.exitFrames();
    ASSERT_EQ(exits.size(), 1u);
    EXPECT_NEAR(exits.front().scale, grown, 0.02f) << "the undo shrinks from where the grow was, no jump to full";

    stepStrip(f.strip, 0.0);
    stepStrip(f.strip, 60.0);
    const float shrunk = m.exitFrames().front().scale;
    ASSERT_LT(shrunk, grown);
    ASSERT_TRUE(f.undo.redo());
    ASSERT_NE(f.doc.getPlacement(copy), nullptr);
    EXPECT_NEAR(m.growScale(copy), shrunk, 0.02f) << "the redo grows from where the shrink was, no jump to nothing";
    EXPECT_TRUE(m.exitFrames().empty()) << "one picture of the copy, never two";
}

TEST(TimelineSectionStripOriginTest, UnderReduceMotionTheUndoneCopyFadesAndWithAnimationsOffItIsGoneAtOnce) {
    {
        FullMotion motion;
        setAnimationMode(AnimationMode::reduced);
        OriginRig f;
        f.copyOfIntro();
        f.land();
        ASSERT_TRUE(f.undo.undo());
        stepStrip(f.strip, 0.0);
        stepStrip(f.strip, 40.0);
        const auto exits = f.strip.getMotionForTest().exitFrames();
        ASSERT_EQ(exits.size(), 1u);
        EXPECT_FLOAT_EQ(exits.front().scale, 1.0f) << "no shrink";
        EXPECT_LT(exits.front().alpha, 1.0f) << "a plain fade";
        EXPECT_EQ(exits.front().rect, exits.front().ghost.bounds);
    }
    {
        FullMotion motion;
        setAnimationMode(AnimationMode::off);
        OriginRig f;
        f.copyOfIntro();
        ASSERT_TRUE(f.undo.undo());
        EXPECT_TRUE(f.strip.getMotionForTest().exitFrames().empty());
        EXPECT_FALSE(f.strip.getMotionForTest().isAnimating());
    }
}

// Regression test for FRO1757: the clips of an undone copy shrank toward their own centres while the block above them
// shrank toward its edge.
TEST(TimelineSectionStripOriginTest, TheClipsOfAnUndoneCopyShrinkWithTheirBlockAsOnePicture) {
    FullMotion motion;
    OriginRig f;
    ClipSelectionModel selection;
    TimelineClipLaneArea lane{f.state, selection};
    lane.setTimelineDoc(&f.doc);
    lane.setUndoManager(&f.undo);
    lane.setSize(1200, 200);
    lane.refreshFromDoc();
    lane.getClipGlideForTest().forceAnimateForTest(true);
    lane.getEnterExitForTest().forceAnimateForTest(true);
    struct LaneRefresh : TimelineDoc::Listener {
        TimelineClipLaneArea& lane;
        explicit LaneRefresh(TimelineClipLaneArea& l)
            : lane(l) {}
        void timelineChanged(const TimelineDoc&) override { lane.refreshFromDoc(); }
    } refresh{lane};
    f.doc.addListener(&refresh);

    f.copyOfIntro();
    f.land();
    lane.getClipGlideForTest().stepForTest(600.0);
    ASSERT_TRUE(f.undo.undo());
    f.doc.removeListener(&refresh);
    auto& exits = lane.getEnterExitForTest();
    ASSERT_EQ(exits.ghosts().size(), 1u) << "the copy's clip leaves";
    const auto& ghost = exits.ghosts().front();
    EXPECT_EQ(ghost.origin, MotionOrigin::Left);
    EXPECT_NEAR(ghost.anchor(), f.x(8.0), 1.0f) << "it shrinks toward the block's edge, not its own middle";
    for (double ms = kFrameMs; ms < SectionStripMotion::kExitMs; ms += kFrameMs) {
        stepStrip(f.strip, ms);
        exits.stepForTest(ms);
        lane.getClipGlideForTest().stepForTest(ms);
        const auto block = f.strip.getMotionForTest().exitFrames().front();
        EXPECT_NEAR(exits.scaleOf(ghost), block.scale, 0.001f) << ms << " ms: clip and block share one clock";
        const auto clip = scaledTowardOrigin(ghost.rect.toFloat(), exits.scaleOf(ghost), ghost.origin, ghost.anchor());
        EXPECT_NEAR(clip.getX(), block.rect.getX(), 1.0f) << ms << " ms: the clip's left follows the block's";
        EXPECT_NEAR(lane.getDrawnClipRectForTest(f.verseClip).getX(), block.rect.getRight(), 1.5f)
            << ms << " ms: the Verse's clip comes back flush with the shrinking copy";
    }
}
