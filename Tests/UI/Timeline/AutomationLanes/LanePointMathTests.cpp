// LanePointMathTests.cpp -- the pure parts of the lane point selection (set algebra, rigid-block move, replace,
// box hit test, clipboard) and the removal/addition glide's state machine, without a panel.

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

TEST(LanePointGlideTest, AHiddenOwnerOrReducedMotionLandsAtOnce) {
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
    juce::Component owner;
    owner.setSize(100, 40);
    owner.addToDesktop(juce::ComponentPeer::windowIsTemporary);
    owner.setVisible(true);
    if (!owner.isShowing()) {
        synth::ui::setReducedMotionForTest(std::nullopt);
        GTEST_SKIP() << "no peer in this environment";
    }
    LanePointGlide glide(owner);
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

    glide.pointsChanged(sample());
    EXPECT_TRUE(glide.isRunning());
    EXPECT_EQ(glide.leaving().size(), 0u) << "bringing the point back fades it in";
    EXPECT_FLOAT_EQ(glide.presence(4.0), glide.amount());

    auto moved = sample();
    moved[1].beat = 3.0;
    glide.pointsChanged(moved);
    EXPECT_FALSE(glide.isRunning()) << "a point that moved is one removal and one addition: lands at once";
    owner.removeFromDesktop();
    synth::ui::setReducedMotionForTest(std::nullopt);
}
