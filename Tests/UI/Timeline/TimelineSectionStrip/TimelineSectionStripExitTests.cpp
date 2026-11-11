// TimelineSectionStripExitTests.cpp -- a block leaves the Sections strip one way, whatever removed it: a menu delete,
// the Delete key on a selection, a cover drop, an undo or a redo each shrink every removed block away exactly once,
// a load shrinks nothing, and a drop that makes room glides the clips in the lanes below with the blocks. The strip
// (and the lane) are refreshed on every doc notification, the way the timeline panel does it.
#include "TimelineSectionStripFixture.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/KeyGlide/KeyedTween.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Timeline/ClipSelectionModel.h"
#include "UI/Timeline/TimelineClipLaneArea/TimelineClipLaneArea.h"
#include <gtest/gtest.h>

using namespace sectionstrip_test;

namespace {

struct FullMotion {
    FullMotion() {
        FadeVisibility::setAnimateOffScreenForTest(true);
        setAnimationMode(AnimationMode::full);
    }
    ~FullMotion() {
        FadeVisibility::setAnimateOffScreenForTest(false);
        setAnimationMode(AnimationMode::followSystem);
    }
};

// Intro over 0..8, Verse over 8..12, Bridge over 12..16, a clip at the start of each.
struct ThreeBlocks
    : StripFixture
    , TimelineDoc::Listener {
    PlacementId intro, verse, bridge;
    ClipId introClip, verseClip;

    ThreeBlocks() {
        introClip = doc.addClip(track, 0.0, 4.0, "Intro clip");
        verseClip = doc.addClip(track, 8.0, 4.0, "Verse clip");
        doc.addClip(track, 12.0, 4.0, "Bridge clip");
        doc.makeSection(0.0, 8.0, "Intro", 0xff56D4C0, 0.5);
        doc.makeSection(8.0, 12.0, "Verse", 0xff4FC1FF, 0.5);
        doc.makeSection(12.0, 16.0, "Bridge", 0xffE0A040, 0.5);
        strip.refresh();
        intro = doc.getPlacements()[0].id;
        verse = doc.getPlacements()[1].id;
        bridge = doc.getPlacements()[2].id;
        strip.getMotionForTest().stepForTest(0.0); // the three blocks grew in when the strip first saw them
        strip.getMotionForTest().stepForTest(600.0);
        doc.addListener(this);
    }
    ~ThreeBlocks() override { doc.removeListener(this); }
    void timelineChanged(const TimelineDoc&) override { strip.refresh(); }

    int exits() { return strip.getMotionForTest().getExitsStartedForTest(); }
    void dragBlock(double blockStart, double toBeat, int upFlags = 0) {
        const juce::Point<float> from{x(blockStart + 2.0), 12.0f}, to{x(toBeat + 2.0), 12.0f};
        strip.mouseDown(event(from, from, false));
        strip.mouseDrag(event(to, from, true));
        strip.mouseUp(event(to, from, true, upFlags));
    }
};

constexpr int kShift = juce::ModifierKeys::shiftModifier;

} // namespace

// Regression test for FRO1619: three removal paths started their own exit and the rest started none.
TEST(TimelineSectionStripExitTest, EveryRemovalPathShrinksEachRemovedBlockExactlyOnce) {
    FullMotion motion;
    {
        ThreeBlocks f;
        ASSERT_TRUE(f.strip.applySectionMenuChoice(f.verse, TimelineSectionStrip::SectionMenuChoice::DeleteWithBars));
        EXPECT_EQ(f.exits(), 1) << "menu: delete with its bars";
        ASSERT_TRUE(f.strip.applySectionMenuChoice(f.intro, TimelineSectionStrip::SectionMenuChoice::RemoveKeepMusic));
        EXPECT_EQ(f.exits(), 2) << "menu: remove, keep the music";
        ASSERT_TRUE(f.undo.undo());
        EXPECT_EQ(f.exits(), 2) << "an undo that brings a block back removes none";
        ASSERT_TRUE(f.undo.redo());
        EXPECT_EQ(f.exits(), 3) << "the redo of a delete";
    }
    {
        ThreeBlocks f;
        f.strip.selectPlacements({f.verse, f.bridge}, f.verse);
        ASSERT_TRUE(f.strip.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)));
        EXPECT_EQ(f.exits(), 2) << "the Delete key on two selected blocks";
    }
    {
        ThreeBlocks f;
        f.dragBlock(0.0, 8.0, kShift); // cover: the Intro lands on the Verse and the Bridge
        ASSERT_EQ(f.doc.getPlacement(f.verse), nullptr);
        ASSERT_EQ(f.doc.getPlacement(f.bridge), nullptr);
        EXPECT_EQ(f.exits(), 2) << "a cover drop: each block under it once, the dropped one never";
    }
    {
        ThreeBlocks f;
        ASSERT_TRUE(f.strip.applySectionMenuChoice(f.verse, TimelineSectionStrip::SectionMenuChoice::PlaceAgainAtEnd));
        ASSERT_TRUE(f.undo.undo());
        EXPECT_EQ(f.exits(), 1) << "the undo of a copy";
    }
}

TEST(TimelineSectionStripExitTest, UndoingAGroupCopyShrinksEveryCopy) {
    FullMotion motion;
    ThreeBlocks f;
    f.strip.selectPlacements({f.intro, f.verse}, f.intro);
    ASSERT_TRUE(f.strip.applySectionMenuChoice(f.intro, TimelineSectionStrip::SectionMenuChoice::PlaceAgainAfter));
    ASSERT_EQ(f.doc.getPlacements().size(), 5u);
    ASSERT_TRUE(f.undo.undo());
    EXPECT_EQ(f.exits(), 2);
    EXPECT_TRUE(f.strip.getMotionForTest().isExitingForTest());
    f.strip.getMotionForTest().stepForTest(0.0);
    f.strip.getMotionForTest().stepForTest(600.0);
    EXPECT_FALSE(f.strip.getMotionForTest().isExitingForTest());
}

TEST(TimelineSectionStripExitTest, ALoadAndAMoveStartNoExitAndNoGrow) {
    FullMotion motion;
    ThreeBlocks f;
    ASSERT_TRUE(f.doc.movePlacement(f.intro, 8.0, SectionDropMode::MakeRoom, 0.5));
    EXPECT_EQ(f.exits(), 0) << "a moved block glides, it does not leave and come back";
    EXPECT_FALSE(f.strip.getMotionForTest().isGrowingForTest());

    TimelineDoc other;
    const auto track = other.addTrack(TrackKind::Midi, "Other");
    other.addClip(track, 0.0, 4.0, "x");
    other.makeSection(0.0, 8.0, "A", 0xff56D4C0, 0.5);
    other.makeSection(8.0, 16.0, "B", 0xff56D4C0, 0.5);
    ASSERT_TRUE(f.doc.fromVar(other.toVar()));
    EXPECT_EQ(f.exits(), 0) << "a new song is not a delete";
    EXPECT_FALSE(f.strip.getMotionForTest().isGrowingForTest());
}

TEST(TimelineSectionStripExitTest, AMakeRoomDropGlidesTheClipsBelowWithTheBlocks) {
    FullMotion motion;
    ThreeBlocks f;
    ClipSelectionModel selection;
    TimelineClipLaneArea lane{f.state, selection};
    lane.setTimelineDoc(&f.doc);
    lane.setUndoManager(&f.undo);
    lane.setSize(1200, 200);
    lane.refreshFromDoc();
    lane.getClipGlideForTest().forceAnimateForTest(true);
    struct LaneRefresh : TimelineDoc::Listener {
        TimelineClipLaneArea& lane;
        explicit LaneRefresh(TimelineClipLaneArea& l)
            : lane(l) {}
        void timelineChanged(const TimelineDoc&) override { lane.refreshFromDoc(); }
    } refresh{lane};
    f.doc.addListener(&refresh);

    const int verseWas = lane.getDrawnClipRectForTest(f.verseClip).getX();
    f.dragBlock(8.0, 0.0); // the Verse onto the Intro: the Intro makes room
    f.doc.removeListener(&refresh);
    ASSERT_DOUBLE_EQ(f.doc.getPlacement(f.verse)->startBeat, 0.0);
    ASSERT_NE(f.doc.getClip(f.verseClip), nullptr) << "the dropped block's clip is the same clip";
    ASSERT_TRUE(lane.getClipGlideForTest().isAnimating());
    EXPECT_EQ(lane.getDrawnClipRectForTest(f.verseClip).getX(), verseWas) << "it glides from where it was";
    EXPECT_NE(lane.getDrawnClipRectForTest(f.introClip), lane.getClipRect(f.introClip))
        << "the clip that made room glides too";
    lane.getClipGlideForTest().stepForTest(600.0);
    EXPECT_EQ(lane.getDrawnClipRectForTest(f.verseClip), lane.getClipRect(f.verseClip));
}

// Regression test for FRO1619: the dropped block faded out while it settled and the block popped in after it.
TEST(TimelineSectionStripExitTest, ADroppedBlockFirmsUpAsItSettlesAndNeverGoesBlank) {
    FullMotion motion;
    ThreeBlocks f;
    f.dragBlock(8.0, 0.0);
    auto& lift = f.strip.getMotionForTest();
    lift.stepForTest(0.0);
    float last = f.strip.getDragGhostAlphaForTest();
    EXPECT_GE(last, 0.85f) << "the settle starts as solid as the dragged ghost";
    for (double ms = 20.0; ms <= 200.0; ms += 20.0) {
        lift.stepForTest(ms);
        const float now = f.strip.getDragGhostAlphaForTest();
        EXPECT_GE(now, last) << "at " << ms << " ms the ghost only gets more solid";
        last = now;
    }
    EXPECT_FLOAT_EQ(last, 1.0f) << "it lands as solid as the block that takes over";
}
