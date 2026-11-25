// LanePointMathTests.cpp -- the pure parts of the lane point selection (set algebra, rigid-block move, replace,
// box hit test, clipboard) and the removal/addition glide's state machine, without a panel.

#include "UI/Layout/PointEditorDuplicate.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Timeline/AutomationLanes/PointSelection/LanePointEdits.h"
#include "UI/Timeline/AutomationLanes/PointSelection/LanePointGlide.h"
#include "UI/Timeline/AutomationLanes/PointSelection/LanePointSelection.h"
#include <gtest/gtest.h>

using synth::ui::LaneBreakpoint;
using synth::ui::LanePointGlide;
using synth::ui::LanePointSelection;

namespace {
std::vector<LaneBreakpoint> sample() { return {{1.0, 20.0, 0.0f, 1}, {2.0, 60.0, 0.5f, 0}, {4.0, 40.0, 0.0f, 1}}; }
synth::ui::LanePointMapper grid() {
    return [](double beat, double value) { return juce::Point<float>((float)beat * 10.0f, (float)value); };
}
} // namespace

TEST(LanePointMathTest, TheSetKeepsAscendingBeatOrderAndReportsRealChangesOnly) {
    juce::Component owner;
    LanePointSelection s(owner);
    int fired = 0;
    s.onChange = [&] { ++fired; };
    EXPECT_TRUE(s.add(3.0));
    EXPECT_TRUE(s.add(1.0));
    EXPECT_FALSE(s.add(1.0));
    EXPECT_EQ(s.getSelected(), (std::vector<double>{1.0, 3.0}));
    EXPECT_EQ(fired, 2);
    EXPECT_FALSE(s.toggle(1.0));
    EXPECT_TRUE(s.toggle(2.0));
    s.setSelection({2.0, 3.0});
    EXPECT_EQ(fired, 4) << "an identical selection is not a change";
    EXPECT_TRUE(s.retainOnly({3.0, 9.0}));
    EXPECT_EQ(s.getSelected(), (std::vector<double>{3.0}));
    s.clear();
    s.clear();
    EXPECT_EQ(fired, 6);
}

TEST(LanePointMathTest, SelectedPointsAndTheirBoundingBox) {
    juce::Component owner;
    LanePointSelection s(owner);
    EXPECT_TRUE(s.boundingBox(sample(), grid()).isEmpty());
    s.setSelection({1.0, 4.0});
    const auto picked = s.selectedPoints(sample());
    ASSERT_EQ(picked.size(), 2u);
    const auto box = s.boundingBox(sample(), grid());
    EXPECT_FLOAT_EQ(box.getX(), 10.0f);
    EXPECT_FLOAT_EQ(box.getRight(), 40.0f);
    EXPECT_FLOAT_EQ(box.getY(), 20.0f);
    EXPECT_FLOAT_EQ(box.getBottom(), 40.0f);
}

TEST(LanePointMathTest, AMovedBlockKeepsOrderStopsAtZeroAndClampsEachValue) {
    const auto pts = sample();
    EXPECT_DOUBLE_EQ(synth::ui::clampBeatDelta(pts, -5.0), -1.0);
    EXPECT_DOUBLE_EQ(synth::ui::clampBeatDelta(pts, 3.0), 3.0);
    const auto moved = synth::ui::movePoints(pts, 1.5, 50.0, 0.0, 100.0);
    EXPECT_EQ(moved[0].beat, 2.5);
    EXPECT_EQ(moved[2].beat, 5.5);
    EXPECT_DOUBLE_EQ(moved[0].value, 70.0);
    EXPECT_DOUBLE_EQ(moved[1].value, 100.0) << "clamped on its own";
    EXPECT_FLOAT_EQ(moved[1].tension, 0.5f) << "everything but beat and value rides along";
}

TEST(LanePointMathTest, ReplacingPointsMatchesTheDocsRule) {
    const auto out = synth::ui::replacePoints(sample(), {1.0}, {{4.0, 99.0, 0.0f, 1}, {3.0, 5.0, 0.0f, 1}});
    ASSERT_EQ(out.size(), 3u);
    EXPECT_EQ(out[0].beat, 2.0);
    EXPECT_EQ(out[1].beat, 3.0);
    EXPECT_EQ(out[2].beat, 4.0);
    EXPECT_DOUBLE_EQ(out[2].value, 99.0) << "an insert on an existing beat replaces it";
}

TEST(LanePointMathTest, ABoxSelectsByPointCentreAndADegenerateBoxSelectsNothing) {
    EXPECT_EQ(synth::ui::beatsInBox(sample(), grid(), {5.0f, 10.0f, 20.0f, 60.0f}), (std::vector<double>{1.0, 2.0}));
    EXPECT_TRUE(synth::ui::beatsInBox(sample(), grid(), {10.0f, 20.0f, 0.0f, 0.0f}).empty());
}

TEST(LanePointMathTest, TheClipboardStoresOffsetsFromTheEarliestPointAndClampsOnPaste) {
    const auto board = synth::ui::copyPoints(sample());
    ASSERT_EQ(board.entries.size(), 3u);
    EXPECT_DOUBLE_EQ(board.entries[0].offset, 0.0);
    EXPECT_DOUBLE_EQ(board.entries[2].offset, 3.0);
    const auto pasted = synth::ui::pastedPoints(board, 10.0, 0.0, 30.0);
    EXPECT_EQ(pasted[0].beat, 10.0);
    EXPECT_EQ(pasted[2].beat, 13.0);
    EXPECT_DOUBLE_EQ(pasted[1].value, 30.0) << "into a narrower lane the value is clamped";
    EXPECT_FLOAT_EQ(pasted[1].tension, 0.5f);
    EXPECT_TRUE(synth::ui::copyPoints({}).isEmpty());
}

TEST(LanePointMathTest, TheBoxStateMachineDeselectsOnAnUnmovedPressAndSelectsOnADrag) {
    juce::Component owner;
    LanePointSelection s(owner);
    s.setSelection({4.0});
    s.pressEmpty({0, 0}, false);
    EXPECT_FALSE(s.isBoxActive()) << "a plain press only arms";
    EXPECT_TRUE(s.release());
    EXPECT_TRUE(s.isEmpty()) << "released without moving: deselect";

    s.pressEmpty({0, 0}, false);
    EXPECT_TRUE(s.dragTo({25, 70}, sample(), grid()));
    EXPECT_TRUE(s.isBoxActive());
    EXPECT_EQ(s.getSelected(), (std::vector<double>{1.0, 2.0}));
    s.cancelGesture();
    EXPECT_FALSE(s.isBoxActive());
    EXPECT_FALSE(s.release());

    s.pressEmpty({35, 0}, true);
    EXPECT_TRUE(s.isBoxActive()) << "an additive press starts the box at once";
    s.dragTo({45, 50}, sample(), grid());
    EXPECT_EQ(s.getSelected(), (std::vector<double>{1.0, 2.0, 4.0})) << "and keeps what was selected";
}

TEST(LanePointGlideTest, AHiddenOwnerLandsAtOnce) {
    juce::Component owner; // never shown
    LanePointGlide glide(owner);
    glide.reset(sample());
    auto fewer = sample();
    fewer.pop_back();
    glide.pointsChanged(fewer);
    EXPECT_FALSE(glide.isRunning());
    EXPECT_FLOAT_EQ(glide.presence(4.0), 1.0f);
}

TEST(LanePointGlideTest, APureRemovalOrAdditionRunsAndAMoveDoesNot) {
    synth::ui::setReducedMotionForTest(false);
    juce::Component owner; // no native window: one aborts the ASAN job's display with BadAtom
    LanePointGlide glide(owner);
    glide.forceAnimateForTest(true);
    glide.reset(sample());

    auto fewer = sample();
    fewer.pop_back();
    glide.pointsChanged(fewer);
    EXPECT_TRUE(glide.isRunning());
    ASSERT_EQ(glide.leaving().size(), 1u);
    EXPECT_EQ(glide.leaving()[0].beat, 4.0);
    EXPECT_EQ(glide.before().size(), 3u) << "the old shape stays to cross-fade from";
    EXPECT_FLOAT_EQ(glide.presence(4.0), 1.0f - glide.amount());
    EXPECT_FLOAT_EQ(glide.presence(1.0), 1.0f) << "the points that stay never fade";

    glide.stepForTest(1000.0);
    glide.pointsChanged(sample());
    EXPECT_TRUE(glide.isRunning());
    EXPECT_EQ(glide.leaving().size(), 0u) << "bringing the point back fades it in";
    EXPECT_FLOAT_EQ(glide.presence(4.0), glide.amount());

    auto moved = sample();
    moved[1].beat = 3.0;
    glide.pointsChanged(moved);
    EXPECT_FALSE(glide.isRunning()) << "a point that moved is one removal and one addition: lands at once";
    synth::ui::setReducedMotionForTest(std::nullopt);
}

// Regression test for FRO2291: a delete, add or undo cross-faded two curves instead of gliding the line.
TEST(LanePointGlideTest, APointComingOrGoingGlidesTheLineOnTheSameClockAsTheDot) {
    synth::ui::setAnimationMode(synth::ui::AnimationMode::full);
    synth::ui::setReducedMotionForTest(false);
    juce::Component owner;
    LanePointGlide glide(owner);
    glide.forceAnimateForTest(true);
    glide.reset(sample());
    auto fewer = sample();
    fewer.erase(fewer.begin() + 1);
    EXPECT_TRUE(glide.pointsChanged(fewer)) << "a pure removal starts a glide of the line";
    EXPECT_FALSE(glide.fadesOnly());
    EXPECT_FLOAT_EQ(glide.lineWeight(), 1.0f) << "the first frame draws the old line";
    glide.stepForTest(40.0);
    EXPECT_GT(glide.lineWeight(), 0.02f);
    EXPECT_LT(glide.lineWeight(), 0.98f) << "half way through it lies between the two";
    EXPECT_GT(glide.presence(2.0), 0.0f);
    EXPECT_LT(glide.presence(2.0), 1.0f) << "while the dot shrinks out";
    glide.stepForTest(1000.0);
    EXPECT_FALSE(glide.isRunning());
    EXPECT_FLOAT_EQ(glide.lineWeight(), 0.0f);
    synth::ui::setAnimationMode(synth::ui::AnimationMode::followSystem);
    synth::ui::setReducedMotionForTest(std::nullopt);
}

TEST(LanePointGlideTest, UnderReduceMotionThePointsOnlyFadeAndUnderAnimationsOffEverythingLands) {
    juce::Component owner;
    LanePointGlide glide(owner);
    glide.forceAnimateForTest(true);
    auto fewer = sample();
    fewer.pop_back();

    synth::ui::setAnimationMode(synth::ui::AnimationMode::reduced);
    glide.reset(sample());
    glide.pointsChanged(fewer);
    EXPECT_FLOAT_EQ(glide.lineWeight(), 0.0f) << "no line glide under Reduce Motion";
    EXPECT_TRUE(glide.isRunning()) << "but a fade";
    EXPECT_TRUE(glide.fadesOnly());
    glide.stepForTest(40.0);
    EXPECT_GT(glide.presence(4.0), 0.0f);
    EXPECT_LT(glide.presence(4.0), 1.0f);
    glide.stepForTest(100.0);
    EXPECT_FALSE(glide.isRunning()) << "the plain fade takes 80 ms";

    synth::ui::setAnimationMode(synth::ui::AnimationMode::off);
    glide.pointsChanged(sample());
    EXPECT_FALSE(glide.isRunning()) << "Animations Off lands at once";
    EXPECT_FLOAT_EQ(glide.presence(4.0), 1.0f);
    synth::ui::setAnimationMode(synth::ui::AnimationMode::followSystem);
}

// Regression test for FRO2291: an undo while a delete still faded restarted the dot from nothing (a blink).
TEST(LanePointGlideTest, ASecondChangeMidFadeTurnsTheDotAroundFromWhereItIsDrawn) {
    synth::ui::setAnimationMode(synth::ui::AnimationMode::full);
    juce::Component owner;
    LanePointGlide glide(owner);
    glide.forceAnimateForTest(true);
    glide.reset(sample());
    auto fewer = sample();
    fewer.pop_back();
    glide.pointsChanged(fewer);
    glide.stepForTest(60.0);
    const float drawn = glide.presence(4.0);
    ASSERT_GT(drawn, 0.05f);
    ASSERT_LT(drawn, 0.95f);
    glide.pointsChanged(sample());
    EXPECT_NEAR(glide.presence(4.0), drawn, 1e-4f) << "it grows back from the size it had shrunk to";
    EXPECT_FLOAT_EQ(glide.presence(1.0), 1.0f) << "the dots that stay never fade";
    glide.stepForTest(1000.0);
    EXPECT_FLOAT_EQ(glide.presence(4.0), 1.0f);
    synth::ui::setAnimationMode(synth::ui::AnimationMode::followSystem);
}

// Regression test for FRO1331: a move onto another point's beat replaced that point.
TEST(LanePointMathTest, AMoveOntoAPointItDoesNotCarrySettlesOneTickBesideItOnTheSideItCameFrom) {
    using synth::ui::settleBeatDelta;
    const double t = synth::ui::tickBeats();
    const auto lane = sample(); // beats 1, 2, 4
    const std::vector<LaneBreakpoint> first{lane[0]}, last{lane[2]};
    EXPECT_DOUBLE_EQ(settleBeatDelta(lane, first, 0.5, t), 0.5) << "a free landing is the move itself";
    EXPECT_DOUBLE_EQ(settleBeatDelta(lane, first, 4.0, t), 4.0) << "passing points is free";
    EXPECT_DOUBLE_EQ(settleBeatDelta(lane, first, 1.0, t), 1.0 - t) << "onto beat 2 from the left: a tick before it";
    EXPECT_DOUBLE_EQ(settleBeatDelta(lane, last, -2.0, t), -2.0 + t) << "onto beat 2 from the right: a tick after it";
    EXPECT_DOUBLE_EQ(settleBeatDelta(lane, first, -2.0, t), -1.0) << "beat 0 still stops the block";

    const std::vector<LaneBreakpoint> chain{{1.0, 0.0, 0.0f, 0}, {3.0 - t, 0.0, 0.0f, 0}, {3.0, 0.0, 0.0f, 0}};
    EXPECT_DOUBLE_EQ(settleBeatDelta(chain, {chain[0]}, 2.0, t), 2.0 - 2.0 * t)
        << "the tick beside holds a point too (a duplicate's joint): it settles beside that one";

    const std::vector<LaneBreakpoint> pair{{1.0, 0.0, 0.0f, 0}, {1.0 + t, 0.0, 0.0f, 0}};
    EXPECT_DOUBLE_EQ(settleBeatDelta(pair, {pair[1]}, -t, t), 0.0) << "settling back past the start is no move";

    const std::vector<LaneBreakpoint> block{lane[0], lane[1]};
    EXPECT_DOUBLE_EQ(settleBeatDelta(lane, block, 2.0, t), 2.0 - t)
        << "a block settles as a whole when one of its points lands on a point it does not carry";
    EXPECT_DOUBLE_EQ(settleBeatDelta(lane, block, 1.0, t), 1.0) << "landing on a beat it carries is free";
}

// Regression test for FRO2011: a dragged lane dot passed its neighbour instead of stopping at it.
TEST(LanePointMathTest, StopAtNeighbourEndsOneTickShortOfTheNextPointInEitherDirection) {
    using synth::ui::stopAtNeighbourBeatDelta;
    const double t = synth::ui::tickBeats();
    const auto lane = sample(); // beats 1, 2, 4
    const std::vector<LaneBreakpoint> first{lane[0]}, last{lane[2]};
    EXPECT_DOUBLE_EQ(stopAtNeighbourBeatDelta(lane, first, 0.5, t), 0.5) << "short of the neighbour is the move";
    EXPECT_DOUBLE_EQ(stopAtNeighbourBeatDelta(lane, first, 3.0, t), 1.0 - t) << "stops a tick before beat 2";
    EXPECT_DOUBLE_EQ(stopAtNeighbourBeatDelta(lane, last, -3.0, t), -2.0 + t) << "stops a tick after beat 2";
    const std::vector<LaneBreakpoint> block{lane[0], lane[1]};
    EXPECT_DOUBLE_EQ(stopAtNeighbourBeatDelta(lane, block, 5.0, t), 2.0 - t) << "a block stops at an unmoved point";
}
