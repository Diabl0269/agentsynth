// LanePointStretchTests.cpp -- the pure parts of stretching a point selection: proportional beat scaling about the
// opposite edge, the limits (beat 0, closest gap), pushing unselected points, value scaling, the handle geometry and
// the drag state, without a panel.

#include "UI/Layout/ReducedMotion.h"
#include "UI/Timeline/AutomationLanes/PointSelection/LanePointStretch.h"
#include <gtest/gtest.h>

using synth::ui::LaneBreakpoint;
using synth::ui::stretchBeats;
using synth::ui::StretchHandle;

namespace {
std::vector<LaneBreakpoint> at(std::initializer_list<double> beats) {
    std::vector<LaneBreakpoint> out;
    for (double b : beats)
        out.push_back({b, 50.0, 0.25f, 1});
    return out;
}
std::vector<double> beatsOf(const std::vector<LaneBreakpoint>& points) {
    std::vector<double> out;
    for (const auto& p : points)
        out.push_back(p.beat);
    return out;
}
using Beats = std::vector<double>;
} // namespace

TEST(LanePointStretchMathTest, DraggingTheRightEdgeOutSpreadsThePointsEvenlyAboutTheLeftOne) {
    const auto r = stretchBeats(at({1, 2, 3, 4}), at({1, 2, 3, 4}), StretchHandle::Right, 7.0, 1.0);
    EXPECT_EQ(beatsOf(r.moved), Beats({1, 3, 5, 7}));
    EXPECT_DOUBLE_EQ(r.edgeBeat, 7.0);
    EXPECT_TRUE(r.pushed.empty());
    EXPECT_EQ(r.moved[1].tension, 0.25f) << "only the beat changes";
}

TEST(LanePointStretchMathTest, DraggingTheLeftEdgeScalesAboutTheRightOne) {
    const auto r = stretchBeats(at({2, 3, 4}), at({2, 3, 4}), StretchHandle::Left, 1.0, 1.0);
    EXPECT_EQ(beatsOf(r.moved), Beats({1, 2.5, 4}));
}

TEST(LanePointStretchMathTest, SqueezingBringsThePointsTogetherAndNeverPushes) {
    const auto all = at({1, 2, 3, 4, 5});
    const auto r = stretchBeats(at({1, 2, 3, 4}), all, StretchHandle::Right, 2.5, 1.0);
    EXPECT_EQ(beatsOf(r.moved), Beats({1, 1.5, 2, 2.5}));
    EXPECT_TRUE(r.pushed.empty());
}

TEST(LanePointStretchMathTest, TheClosestTwoPointsNeverCollapseOntoOneBeat) {
    const auto r = stretchBeats(at({1, 2, 3, 4}), at({1, 2, 3, 4}), StretchHandle::Right, 0.0, 1.0);
    for (std::size_t i = 1; i < r.moved.size(); ++i)
        EXPECT_GE(r.moved[i].beat - r.moved[i - 1].beat, synth::ui::kStretchMinGapBeats - 1e-12);
    EXPECT_GT(r.edgeBeat, 1.0) << "the edge stops short of the anchor";
}

TEST(LanePointStretchMathTest, NoBeatGoesBelowZero) {
    const auto r = stretchBeats(at({2, 3, 4}), at({2, 3, 4}), StretchHandle::Left, -5.0, 1.0);
    EXPECT_DOUBLE_EQ(r.edgeBeat, 0.0);
    EXPECT_EQ(beatsOf(r.moved), Beats({0, 2, 4}));
}

TEST(LanePointStretchMathTest, GrowingIntoTheNextUnselectedPointPushesItAndEveryPointAfterIt) {
    const auto all = at({1, 2, 3, 5, 6});
    const auto selected = at({1, 2, 3});

    auto r = stretchBeats(selected, all, StretchHandle::Right, 4.0, 1.0);
    EXPECT_TRUE(r.pushed.empty()) << "a grid step short of the next point: nothing moves yet";

    r = stretchBeats(selected, all, StretchHandle::Right, 5.0, 1.0);
    EXPECT_EQ(beatsOf(r.moved), Beats({1, 3, 5}));
    EXPECT_EQ(beatsOf(r.pushed), Beats({6, 7})) << "kept a step beyond the edge, spacing kept";
    EXPECT_EQ(r.pushedFrom, Beats({5, 6}));

    r = stretchBeats(selected, all, StretchHandle::Right, 7.0, 1.0);
    EXPECT_EQ(beatsOf(r.pushed), Beats({8, 9}));
}

TEST(LanePointStretchMathTest, AnUnselectedPointBetweenTheSelectedOnesStaysPut) {
    const auto all = at({1, 2, 3, 4});
    const auto r = stretchBeats(at({1, 3, 4}), all, StretchHandle::Right, 7.0, 1.0);
    EXPECT_TRUE(r.pushed.empty()) << "only points beyond the edge are pushed";
}

TEST(LanePointStretchMathTest, PushingLeftStopsWhenThePushedPointsReachBeatZero) {
    const auto all = at({0.5, 1, 3, 4});
    const auto r = stretchBeats(at({3, 4}), all, StretchHandle::Left, 0.0, 0.5);
    EXPECT_EQ(beatsOf(r.pushed), Beats({0.5, 0.0})) << "the pushed block ends at 0, not below";
    EXPECT_NEAR(r.edgeBeat, 1.0, 1e-9) << "so the edge stops one gap ahead of it";
    EXPECT_NEAR(r.moved.front().beat, 1.0, 1e-9);
}

TEST(LanePointStretchMathTest, AnEdgeLeftWhereItWasChangesNothing) {
    const auto all = at({1, 2, 3, 5});
    const auto r = stretchBeats(at({1, 2, 3}), all, StretchHandle::Right, 3.0, 1.0);
    EXPECT_FALSE(synth::ui::stretchChanged(at({1, 2, 3}), r)) << "no move, no change";
}

TEST(LanePointStretchMathTest, ScalingValuesKeepsTheOppositeEdgeAndClampsToTheRange) {
    auto sel = at({1, 2, 3});
    sel[0].value = 20.0;
    sel[1].value = 60.0;
    sel[2].value = 40.0;

    auto top = synth::ui::scaleValues(sel, StretchHandle::Top, 100.0, 0.0, 100.0);
    EXPECT_DOUBLE_EQ(top[0].value, 20.0) << "the lowest value stays";
    EXPECT_DOUBLE_EQ(top[1].value, 100.0);
    EXPECT_DOUBLE_EQ(top[2].value, 60.0);

    top = synth::ui::scaleValues(sel, StretchHandle::Top, 400.0, 0.0, 100.0);
    EXPECT_DOUBLE_EQ(top[1].value, 100.0) << "the edge cannot leave the lane's range";

    auto bottom = synth::ui::scaleValues(sel, StretchHandle::Bottom, 0.0, 0.0, 100.0);
    EXPECT_DOUBLE_EQ(bottom[1].value, 60.0) << "the highest value stays";
    EXPECT_DOUBLE_EQ(bottom[0].value, 0.0);
    EXPECT_DOUBLE_EQ(bottom[2].value, 30.0);

    const auto flipped = synth::ui::scaleValues(sel, StretchHandle::Top, -50.0, -100.0, 100.0);
    EXPECT_DOUBLE_EQ(flipped[1].value, 20.0) << "dragging past the opposite edge flattens, never flips";
}

TEST(LanePointStretchMathTest, PointsWithOneValueHaveNothingToScale) {
    const auto sel = at({1, 2});
    const auto scaled = synth::ui::scaleValues(sel, StretchHandle::Top, 90.0, 0.0, 100.0);
    EXPECT_DOUBLE_EQ(scaled[0].value, 50.0);
    EXPECT_DOUBLE_EQ(scaled[1].value, 50.0);
}

TEST(LanePointStretchMathTest, AnEditRemovesTheOriginalsAndThePushedAndAddsTheirNewPlaces) {
    const auto r = stretchBeats(at({1, 2, 3}), at({1, 2, 3, 5}), StretchHandle::Right, 5.0, 1.0);
    EXPECT_EQ(synth::ui::stretchRemoveBeats(at({1, 2, 3}), r), Beats({1, 2, 3, 5}));
    EXPECT_EQ(beatsOf(synth::ui::stretchAddPoints(r)), Beats({1, 3, 5, 6}));
}

TEST(LanePointStretchGeometryTest, HandlesSitCentredOnTheFourEdgesOfThePaddedBox) {
    const juce::Rectangle<float> bounds(0, 0, 400, 100);
    const auto box = synth::ui::stretchBoxAround({100, 40, 200, 20});
    EXPECT_GT(box.getWidth(), 200.0f);
    EXPECT_EQ(synth::ui::stretchHandleRect(StretchHandle::Left, box, bounds).getCentre(),
              juce::Point<float>(box.getX(), box.getCentreY()));
    EXPECT_EQ(synth::ui::stretchHandleRect(StretchHandle::Right, box, bounds).getCentre(),
              juce::Point<float>(box.getRight(), box.getCentreY()));
    EXPECT_EQ(synth::ui::stretchHandleRect(StretchHandle::Top, box, bounds).getCentre(),
              juce::Point<float>(box.getCentreX(), box.getY()));
    EXPECT_EQ(synth::ui::stretchHandleRect(StretchHandle::Bottom, box, bounds).getCentre(),
              juce::Point<float>(box.getCentreX(), box.getBottom()));
}

TEST(LanePointStretchGeometryTest, AHandleStaysInsideTheCanvasAndOnlyItsReachGrabs) {
    const juce::Rectangle<float> bounds(0, 0, 400, 100);
    const auto box = synth::ui::stretchBoxAround({100, 0, 200, 100});
    const auto top = synth::ui::stretchHandleRect(StretchHandle::Top, box, bounds);
    EXPECT_TRUE(bounds.contains(top)) << "pulled in from above the lane";

    const auto right = synth::ui::stretchHandleRect(StretchHandle::Right, box, bounds).getCentre();
    EXPECT_EQ(synth::ui::hitStretchHandle(right, box, bounds), StretchHandle::Right);
    EXPECT_EQ(synth::ui::hitStretchHandle(right + juce::Point<float>(5.0f, 0.0f), box, bounds), StretchHandle::Right);
    EXPECT_EQ(synth::ui::hitStretchHandle(right + juce::Point<float>(9.0f, 0.0f), box, bounds), StretchHandle::None);
    EXPECT_EQ(synth::ui::hitStretchHandle(box.getCentre(), box, bounds), StretchHandle::None)
        << "the inside of the box takes no clicks";
    EXPECT_EQ(synth::ui::hitStretchHandle(right, {}, bounds), StretchHandle::None);
}

TEST(LanePointStretchGeometryTest, EvenANarrowSelectionsHandlesStayApartAndEachGrabsItself) {
    const juce::Rectangle<float> bounds(0, 0, 400, 100);
    const auto box = synth::ui::stretchBoxAround({100, 40, 1, 20}); // a very narrow selection
    const auto left = synth::ui::stretchHandleRect(StretchHandle::Left, box, bounds).getCentre();
    const auto right = synth::ui::stretchHandleRect(StretchHandle::Right, box, bounds).getCentre();
    EXPECT_EQ(synth::ui::hitStretchHandle(left, box, bounds), StretchHandle::Left);
    EXPECT_EQ(synth::ui::hitStretchHandle(right, box, bounds), StretchHandle::Right);
}

TEST(LanePointStretchTest, TheDragPreviewsAndChangedTellsWhetherAnythingMoved) {
    juce::Component owner;
    synth::ui::LanePointStretch stretch(owner);
    EXPECT_FALSE(stretch.isActive());
    stretch.begin(StretchHandle::Right, at({1, 2, 3}), at({1, 2, 3, 5}), 0.0, 100.0, 1.0);
    ASSERT_TRUE(stretch.isActive());
    EXPECT_DOUBLE_EQ(stretch.edgeStart(), 3.0);
    EXPECT_FALSE(stretch.changed());

    stretch.update(5.0);
    EXPECT_TRUE(stretch.changed());
    EXPECT_EQ(beatsOf(stretch.result().moved), Beats({1, 3, 5}));
    EXPECT_EQ(beatsOf(stretch.original()), Beats({1, 2, 3})) << "the originals stay for the undo";

    stretch.cancel();
    EXPECT_FALSE(stretch.isActive());
    stretch.begin(StretchHandle::Top, at({1}), at({1}), 0.0, 100.0, 1.0);
    EXPECT_FALSE(stretch.isActive()) << "one point is nothing to stretch";
}

TEST(LanePointStretchTest, AHiddenOwnerOrReducedMotionShowsAndHidesTheBoxAtOnce) {
    juce::Component owner; // never shown
    synth::ui::LanePointStretch stretch(owner);
    stretch.setBoxShown(true);
    EXPECT_FLOAT_EQ(stretch.boxAlpha(), 1.0f);
    stretch.setBoxShown(false);
    EXPECT_FLOAT_EQ(stretch.boxAlpha(), 0.0f);
}

TEST(LanePointStretchTest, TheBoxFadesOnAShowingOwner) {
    synth::ui::setReducedMotionForTest(false);
    juce::Component owner;
    owner.setSize(100, 40);
    owner.addToDesktop(juce::ComponentPeer::windowIsTemporary);
    owner.setVisible(true);
    if (!owner.isShowing()) {
        synth::ui::setReducedMotionForTest(std::nullopt);
        GTEST_SKIP() << "no peer in this environment";
    }
    {
        synth::ui::LanePointStretch stretch(owner);
        stretch.setBoxShown(true);
        EXPECT_LT(stretch.boxAlpha(), 1.0f) << "it fades in rather than appearing";
    }
    owner.removeFromDesktop();
    synth::ui::setReducedMotionForTest(std::nullopt);
}
