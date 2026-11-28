// TimelineSectionStripExitTests.cpp -- a block leaves the Sections strip one way, whatever removed it: a menu delete,
// the Delete key on a selection, a cover drop, an undo or a redo each shrink every removed block away exactly once,
// a load shrinks nothing, and a drop that makes room glides the clips in the lanes below with the blocks. The strip
// (and the lane) are refreshed on every doc notification, the way the timeline panel does it.
#include "TimelineSectionStripThreeBlocksFixture.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Layout/KeyGlide/KeyedTween.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Timeline/ClipSelectionModel.h"
#include "UI/Timeline/TimelineClipLaneArea/TimelineClipLaneArea.h"
#include <functional>
#include <gtest/gtest.h>

using namespace sectionstrip_test;

namespace {

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

// Regression test for FRO1754: a delete closed up every block after it, so one past a gap was drawn sliding too.
TEST(TimelineSectionStripExitTest, DeletingBesideAGapGlidesOnlyTheTouchingRunAndNeverTheLaterBlock) {
    FullMotion motion;
    ThreeBlocks f;
    ASSERT_TRUE(f.doc.movePlacement(f.bridge, 24.0, SectionDropMode::Cover, 0.5)); // the Bridge now starts after a gap
    f.strip.getMotionForTest().stepForTest(0.0);
    f.strip.getMotionForTest().stepForTest(600.0);
    auto& glide = f.strip.getDropGlideForTest();
    glide.forceAnimateForTest(true);
    glide.completeForTest();
    ASSERT_TRUE(f.strip.applySectionMenuChoice(f.intro, TimelineSectionStrip::SectionMenuChoice::DeleteWithBars));
    EXPECT_DOUBLE_EQ(f.doc.getPlacement(f.verse)->startBeat, 0.0);
    EXPECT_DOUBLE_EQ(f.doc.getPlacement(f.bridge)->startBeat, 24.0);
    EXPECT_DOUBLE_EQ(f.strip.getDrawnStartBeatForTest(f.verse), 8.0) << "the Verse is drawn where it was...";
    glide.stepForTest(40.0);
    const double midway = f.strip.getDrawnStartBeatForTest(f.verse);
    EXPECT_GT(midway, 0.0) << "...and glides left...";
    EXPECT_LT(midway, 8.0);
    EXPECT_DOUBLE_EQ(f.strip.getDrawnStartBeatForTest(f.bridge), 24.0) << "...while the Bridge never moves";
    glide.completeForTest();
    EXPECT_DOUBLE_EQ(f.strip.getDrawnStartBeatForTest(f.verse), 0.0);
    ASSERT_TRUE(f.undo.undo());
    f.strip.refresh();
    EXPECT_DOUBLE_EQ(f.strip.getDrawnStartBeatForTest(f.verse), 0.0) << "an undo starts from where it was drawn";
    glide.stepForTest(40.0);
    const double back = f.strip.getDrawnStartBeatForTest(f.verse);
    EXPECT_GT(back, 0.0) << "and glides back to the right";
    EXPECT_LT(back, 8.0);
    EXPECT_DOUBLE_EQ(f.strip.getDrawnStartBeatForTest(f.bridge), 24.0);
    glide.completeForTest();
    EXPECT_DOUBLE_EQ(f.strip.getDrawnStartBeatForTest(f.verse), 8.0);
}

namespace {

constexpr double kFrameMs = 16.0;
constexpr double kSettleMs = 400.0;
constexpr int kProbeY = 7; // inside the block, below its outline and above its name
const juce::Colour kIntroColour{0xff56D4C0};
const juce::Colour kVerseColour{0xff4FC1FF};
const juce::Colour kBridgeColour{0xffE0A040};
constexpr int kSolidGap = 24; // closer than this to a section's colour is that colour drawn at full brightness

// What a cover drop painted at the middle of the block it covered, frame by frame from the release.
struct CoverRun {
    int exitsStarted = 0;
    float firstExitAlpha = 0.0f;
    int solidFrames = 0; // frames where the covered block's own colour sat on top at full brightness
    int bleedFrames = 0; // frames where the landed block is drawn and anything but its colour shows at the spot
    juce::Colour last;
};

// Drops with `flags` and steps 16 ms frames from the release; the pixel is at the middle of the strip's top edge over
// `centreBeat`, a spot with no name or chip on it.
CoverRun runCoverDrop(ThreeBlocks& f, std::function<void()> drop, double centreBeat, juce::Colour covered,
                      juce::Colour dropped = kIntroColour) {
    CoverRun run;
    const auto sample = [&] {
        const auto img = renderStrip(f.strip);
        return img.getPixelAt((int)f.x(centreBeat), kProbeY);
    };
    drop();
    run.exitsStarted = f.exits();
    const auto exits = f.strip.getMotionForTest().exitFrames();
    if (!exits.empty())
        run.firstExitAlpha = exits.front().alpha;
    stepStrip(f.strip, 0.0);
    for (double ms = 0.0; ms <= kSettleMs; ms += kFrameMs) {
        stepStrip(f.strip, ms);
        run.last = sample();
        if (channelGap(run.last, covered) < kSolidGap)
            ++run.solidFrames;
        if (f.strip.getMotionForTest().lift() <= 0.0f && channelGap(run.last, dropped) >= kSolidGap)
            ++run.bleedFrames;
    }
    return run;
}

} // namespace

// Regression test for FRO1752: a covered block's exit started at full brightness and was drawn over the dropped block.
TEST(TimelineSectionStripExitTest, ACoverDropShrinksTheCoveredBlockDimmedAndUnderTheIncomingOne) {
    FullMotion motion;
    ThreeBlocks f;
    f.strip.selectPlacements({f.intro}, f.intro);
    const auto run = runCoverDrop(f, [&] { f.dragBlockLifted(0.0, 8.0, kShift); }, 10.0, kVerseColour);
    ASSERT_EQ(f.doc.getPlacement(f.verse), nullptr) << "the drop covered the Verse";
    EXPECT_EQ(run.exitsStarted, 2) << "the Verse and the Bridge each shrink once";
    EXPECT_LE(run.firstExitAlpha, sectionstrip::kMovedOriginalAlpha + 0.001f) << "it starts as dim as it was drawn";
    EXPECT_EQ(run.solidFrames, 0) << "the covered block is never drawn at full brightness";
    EXPECT_EQ(run.bleedFrames, 0) << "the landed block is on top of the shrinking one in every frame";
    EXPECT_LT(channelGap(run.last, kIntroColour), kSolidGap) << "it lands as the dropped section's colour";
    EXPECT_EQ(f.exits(), 2) << "stepping the settle starts no further exit";
}

TEST(TimelineSectionStripExitTest, ACoverDropOfTwoSelectedBlocksPutsBothOverTheCoveredOne) {
    FullMotion motion;
    ThreeBlocks f;
    f.strip.selectPlacements({f.intro, f.verse}, f.intro);
    const auto run = runCoverDrop(f, [&] { f.dragBlockLifted(0.0, 12.0, kShift); }, 14.0, kBridgeColour);
    ASSERT_EQ(f.doc.getPlacement(f.bridge), nullptr) << "the group covered the Bridge";
    EXPECT_EQ(run.exitsStarted, 1);
    EXPECT_LE(run.firstExitAlpha, sectionstrip::kMovedOriginalAlpha + 0.001f);
    EXPECT_EQ(run.solidFrames, 0);
    EXPECT_EQ(run.bleedFrames, 0);
    EXPECT_LT(channelGap(run.last, kIntroColour), kSolidGap);
}

TEST(TimelineSectionStripExitTest, ACoverCopyDropShrinksTheCoveredBlockDimmedAndUnderTheCopy) {
    FullMotion motion;
    ThreeBlocks f;
    f.strip.selectPlacements({f.intro}, f.intro);
    const auto run = runCoverDrop(
        f, [&] { f.dragBlockLifted(0.0, 8.0, kShift | juce::ModifierKeys::altModifier); }, 10.0, kVerseColour);
    ASSERT_EQ(f.doc.getPlacement(f.verse), nullptr);
    ASSERT_NE(f.doc.getPlacement(f.intro), nullptr) << "a copy leaves the original";
    EXPECT_EQ(run.exitsStarted, 2);
    EXPECT_LE(run.firstExitAlpha, sectionstrip::kMovedOriginalAlpha + 0.001f);
    EXPECT_EQ(run.solidFrames, 0);
    EXPECT_EQ(run.bleedFrames, 0);
    EXPECT_LT(channelGap(run.last, kIntroColour), kSolidGap);
}

TEST(TimelineSectionStripExitTest, ACoverDropUnderReducedMotionAndAnimationsOffLandsOnTheDroppedColour) {
    for (const auto mode : {AnimationMode::reduced, AnimationMode::off}) {
        FullMotion motion;
        setAnimationMode(mode);
        ThreeBlocks f;
        f.strip.selectPlacements({f.intro}, f.intro);
        const auto run = runCoverDrop(f, [&] { f.dragBlockLifted(0.0, 8.0, kShift); }, 10.0, kVerseColour);
        ASSERT_EQ(f.doc.getPlacement(f.verse), nullptr);
        EXPECT_EQ(run.solidFrames, 0) << "mode " << (int)mode;
        EXPECT_EQ(run.bleedFrames, 0) << "mode " << (int)mode;
        EXPECT_LT(channelGap(run.last, kIntroColour), kSolidGap) << "mode " << (int)mode;
        if (mode == AnimationMode::off) {
            EXPECT_FALSE(f.strip.getMotionForTest().isExitingForTest());
            EXPECT_FALSE(f.strip.getMotionForTest().isLiftingForTest());
        }
    }
}

// A dropped block settles into the real block without a visible jump: from the release, the frames around the moment
// the real block takes over differ by no more than easing alone does, the selection outline and the focus ring fade in
// instead of appearing at once, and the last frame is the picture of a strip that was never dragged. Every frame is a
// real paint, 16 ms apart, into a software image.

namespace {

constexpr double kRestMs = 1000.0;
// Once the lift is below this the block has all but landed and only its look still changes. Easing alone moves a
// channel by less than kEasingStep between two frames then; a ring or outline that appears at once moves it by about
// the accent's full strength.
constexpr float kNearlyLanded = 0.02f;
constexpr int kEasingStep = 32;

struct FocusRingsShown {
    FocusRingsShown() { setFocusRingsShownForTest(true); }
    ~FocusRingsShown() { setFocusRingsShownForTest(false); }
};

// The part of the strip the settling block covers, a little wider for its outline and ring.
juce::Rectangle<int> landedArea(ThreeBlocks& f) {
    juce::Rectangle<float> area;
    for (const auto id : f.strip.getSelectedPlacements())
        if (const auto* placement = f.doc.getPlacement(id))
            if (const auto* section = f.doc.getSection(placement->section))
                area = area.getUnion({f.x(placement->startBeat), 0.0f,
                                      f.x(placement->startBeat + section->lengthBeats) - f.x(placement->startBeat),
                                      (float)f.strip.getHeight()});
    return area.toNearestInt().expanded(6, 0).getIntersection(f.strip.getLocalBounds());
}

int maxGap(const juce::Image& a, const juce::Image& b, juce::Rectangle<int> area) {
    int gap = 0;
    for (int y = area.getY(); y < area.getBottom(); ++y)
        for (int x = area.getX(); x < area.getRight(); ++x)
            gap = std::max(gap, channelGap(a.getPixelAt(x, y), b.getPixelAt(x, y)));
    return gap;
}

struct SettleRun {
    int handoverGap = 0; // the frame the lift reaches 0 against the one before it
    int windowGap = 0;   // the largest between consecutive frames once the block is all but landed
    int restGap = 0;     // the last frame against the same strip after a long rest
    bool handedOver = false;
};

// Releases with `drop`, renders every 16 ms frame to `kSettleMs`, then the rest picture.
SettleRun settle(ThreeBlocks& f, const std::function<void()>& drop) {
    SettleRun run;
    drop();
    const auto area = landedArea(f);
    stepStrip(f.strip, 0.0);
    auto before = renderStrip(f.strip);
    for (double ms = kFrameMs; ms <= kSettleMs; ms += kFrameMs) {
        stepStrip(f.strip, ms);
        const auto now = renderStrip(f.strip);
        const int gap = maxGap(before, now, area);
        if (f.strip.getMotionForTest().lift() < kNearlyLanded)
            run.windowGap = std::max(run.windowGap, gap);
        if (!run.handedOver && f.strip.getMotionForTest().lift() <= 0.0f) {
            run.handedOver = true;
            run.handoverGap = gap;
        }
        before = now;
    }
    stepStrip(f.strip, kRestMs);
    run.restGap = maxGap(before, renderStrip(f.strip), area);
    return run;
}

} // namespace

// Regression test for FRO1751: the selection outline and the focus ring appeared at once on the frame the dropped
// block took over from its settling ghost.
TEST(TimelineSectionStripSettleTest, AMoveSettlesWithoutAJumpAtTheHandover) {
    FullMotion motion;
    FocusRingsShown rings;
    ThreeBlocks f;
    const auto run = settle(f, [&] { f.dragBlockLifted(0.0, 16.0); });
    ASSERT_TRUE(run.handedOver);
    printf("[measure] move handover=%d window=%d rest=%d\n", run.handoverGap, run.windowGap, run.restGap);
    EXPECT_LT(run.handoverGap, kEasingStep);
    EXPECT_LT(run.windowGap, kEasingStep);
    EXPECT_EQ(run.restGap, 0) << "the last frame is the picture at rest";
}

TEST(TimelineSectionStripSettleTest, ACoverDropSettlesWithoutAJumpAtTheHandover) {
    FullMotion motion;
    FocusRingsShown rings;
    ThreeBlocks f;
    const auto run = settle(f, [&] { f.dragBlockLifted(0.0, 8.0, kShiftFlag); });
    ASSERT_TRUE(run.handedOver);
    printf("[measure] cover handover=%d window=%d rest=%d\n", run.handoverGap, run.windowGap, run.restGap);
    EXPECT_LT(run.handoverGap, kEasingStep);
    EXPECT_LT(run.windowGap, kEasingStep);
    EXPECT_EQ(run.restGap, 0);
}

TEST(TimelineSectionStripSettleTest, ACopyDropSettlesWithoutAJumpAtTheHandover) {
    FullMotion motion;
    FocusRingsShown rings;
    ThreeBlocks f;
    const auto run = settle(f, [&] { f.dragBlockLifted(0.0, 16.0, kAltFlag); });
    ASSERT_TRUE(run.handedOver);
    printf("[measure] copy handover=%d window=%d rest=%d\n", run.handoverGap, run.windowGap, run.restGap);
    EXPECT_LT(run.handoverGap, kEasingStep);
    EXPECT_LT(run.windowGap, kEasingStep);
    EXPECT_EQ(run.restGap, 0);
}

TEST(TimelineSectionStripSettleTest, AGroupDropSettlesWithoutAJumpAtTheHandover) {
    FullMotion motion;
    FocusRingsShown rings;
    ThreeBlocks f;
    f.strip.selectPlacements({f.intro, f.verse}, f.intro);
    const auto run = settle(f, [&] { f.dragBlockLifted(0.0, 16.0); });
    ASSERT_TRUE(run.handedOver);
    printf("[measure] group handover=%d window=%d rest=%d\n", run.handoverGap, run.windowGap, run.restGap);
    EXPECT_LT(run.handoverGap, kEasingStep);
    EXPECT_LT(run.windowGap, kEasingStep);
    EXPECT_EQ(run.restGap, 0);
}

TEST(TimelineSectionStripSettleTest, ADropUnderReducedMotionAndAnimationsOffEndsOnTheRestPicture) {
    for (const auto mode : {AnimationMode::reduced, AnimationMode::off}) {
        FullMotion motion;
        setAnimationMode(mode);
        FocusRingsShown rings;
        ThreeBlocks f;
        const auto run = settle(f, [&] { f.dragBlockLifted(0.0, 16.0); });
        EXPECT_EQ(run.restGap, 0) << "mode " << (int)mode;
    }
}

// The block whose section was made longer eases its right edge to the new length, and an undo eases it back.
TEST(TimelineSectionStripExitTest, ASectionMadeLongerGlidesItsRightEdgeAndAnUndoGlidesItBack) {
    FullMotion motion;
    ThreeBlocks f;
    f.strip.getMotionForTest().stepForTest(0.0);
    f.strip.getMotionForTest().stepForTest(600.0);
    auto& glide = f.strip.getLengthGlideForTest();
    glide.forceAnimateForTest(true);
    glide.completeForTest();
    const double before = f.doc.getSection(f.doc.getPlacement(f.verse)->section)->lengthBeats;
    ASSERT_GT(before, 0.0);

    f.undo.recordTimelineChange(f.doc, [&] {
        const double start = f.doc.getPlacement(f.verse)->startBeat;
        ASSERT_FALSE(f.doc.growSectionsToFit({{start + 1.0, start + before + 2.0}}, 4.0, 0.5).empty());
    });
    const double after = f.doc.getSection(f.doc.getPlacement(f.verse)->section)->lengthBeats;
    ASSERT_GT(after, before);
    EXPECT_DOUBLE_EQ(f.strip.getDrawnLengthBeatsForTest(f.verse), before) << "drawn as long as it was...";
    glide.stepForTest(40.0);
    const double midway = f.strip.getDrawnLengthBeatsForTest(f.verse);
    EXPECT_GT(midway, before) << "...and its right edge glides out...";
    EXPECT_LT(midway, after);
    glide.completeForTest();
    EXPECT_DOUBLE_EQ(f.strip.getDrawnLengthBeatsForTest(f.verse), after);

    ASSERT_TRUE(f.undo.undo());
    f.strip.refresh();
    EXPECT_DOUBLE_EQ(f.strip.getDrawnLengthBeatsForTest(f.verse), after) << "an undo starts from where it was drawn";
    glide.stepForTest(40.0);
    const double back = f.strip.getDrawnLengthBeatsForTest(f.verse);
    EXPECT_LT(back, after) << "and glides back in";
    EXPECT_GT(back, before);
    glide.completeForTest();
    EXPECT_DOUBLE_EQ(f.strip.getDrawnLengthBeatsForTest(f.verse), before);
}
