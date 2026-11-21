// MacroCountKnobsRideTheGlideTests.cpp
//
// Regression test for FRO2147: the card grew first and its new knobs only after it.
//
// When the Macros bank's Controllers count changes, the knobs it adds or takes away move WITH the card's height glide
// (CardHeightGlide::durationMs()), not after it: the arriving knobs are already part-grown a quarter into the glide
// and all landed on its last frame, the leaving ones are still drawn, part-shrunk, a quarter in and gone on its last
// frame. Through the real arrows and through undo / redo (which rebuild the card), with the animated paths forced on
// and the card glide and the swap stepped by hand to the same elapsed time.

#include "MacroCountRideRig.h"

#include "UI/Graph/CardBody/CardBodySwapMotion.h"
#include <cmath>
#include <gtest/gtest.h>

using namespace macro_ride_test;
using synth::test::flushMessageQueue;

namespace {

// The arriving knobs (widget and caption) are part-grown `ms` into the glide: shown, with some opacity, scaled between
// nothing and full.
void expectPartGrown(Rig& rig, NodeID id, int lo, int hi, double ms) {
    const auto knobs = knobsBetween(rig, id, lo, hi);
    ASSERT_EQ((int)knobs.size(), hi - lo);
    for (const auto& knob : knobs)
        for (auto* part : {knob.widget, knob.label}) {
            ASSERT_NE(part, nullptr) << knob.id;
            EXPECT_TRUE(part->isVisible()) << knob.id << " at " << ms << " ms";
            EXPECT_GT(part->getAlpha(), 0.0f) << knob.id << " at " << ms << " ms";
            EXPECT_LT(part->getAlpha(), 1.0f) << knob.id << " at " << ms << " ms";
            EXPECT_GT(scaleOf(*part), 0.0) << knob.id << " at " << ms << " ms";
            EXPECT_LT(scaleOf(*part), 1.0) << knob.id << " at " << ms << " ms";
        }
}

void expectLanded(Rig& rig, NodeID id, int lo, int hi, int finalHeight, bool arriving) {
    auto& card = *rig.card(id);
    for (const auto& knob : knobsBetween(rig, id, lo, hi))
        for (auto* part : {knob.widget, knob.label}) {
            if (!arriving)
                continue; // a knob the count took away is gone, not landed
            EXPECT_TRUE(part->isVisible()) << knob.id;
            EXPECT_FLOAT_EQ(part->getAlpha(), 1.0f) << knob.id;
            EXPECT_TRUE(part->getTransform().isIdentity()) << knob.id;
        }
    EXPECT_FALSE(synth::cardbody_test::isSwapRunning(card));
    EXPECT_FALSE(card.heightGlideForTest().isGliding());
    EXPECT_EQ(card.getHeight(), finalHeight);
}

float tallestGhost(const std::vector<juce::Rectangle<float>>& rects) {
    float tallest = 0.0f;
    for (const auto& r : rects)
        tallest = std::max(tallest, r.getHeight());
    return tallest;
}

// The swap ends with the card: still running just before the glide's duration, done on it.
void expectEndsWithTheCard(Rig& rig, NodeID id) {
    rig.stepTo(id, glideMs() - 1.0);
    EXPECT_TRUE(synth::cardbody_test::isSwapRunning(*rig.card(id))) << "one ms before the card lands";
    rig.stepTo(id, glideMs());
    EXPECT_FALSE(synth::cardbody_test::isSwapRunning(*rig.card(id))) << "it lands with the card, not after it";
}

// A change of count from `from` to `to` has just been made: the knobs ride the glide at 25% and at 100%.
void expectKnobsRide(Rig& rig, NodeID id, int from, int to, int finalHeight) {
    const int lo = std::min(from, to);
    const int hi = std::max(from, to);
    ASSERT_TRUE(rig.card(id)->heightGlideForTest().isGliding());
    ASSERT_TRUE(synth::cardbody_test::isSwapRunning(*rig.card(id)));
    if (to > from) {
        rig.stepTo(id, glideMs() * 0.25);
        expectPartGrown(rig, id, lo, hi, glideMs() * 0.25);
    } else {
        rig.stepTo(id, 0.0);
        const auto start = synth::cardbody_test::swapGhostRects(*rig.card(id));
        ASSERT_FALSE(start.empty());
        rig.stepTo(id, glideMs() * 0.25);
        const auto mid = synth::cardbody_test::swapGhostRects(*rig.card(id));
        EXPECT_EQ(mid.size(), start.size()) << "every leaving knob is still drawn a quarter in";
        EXPECT_GT(tallestGhost(mid), 0.0f);
        EXPECT_LT(tallestGhost(mid), tallestGhost(start)) << "drawn smaller than at full";
    }
    expectEndsWithTheCard(rig, id);
    EXPECT_TRUE(synth::cardbody_test::swapGhostRects(*rig.card(id)).empty());
    EXPECT_EQ(rig.card(id)->getHeight(), finalHeight) << "the card is final on the same frame";
    rig.settle(id);
    expectLanded(rig, id, lo, hi, finalHeight, to > from);
}

} // namespace

TEST(MacroCountKnobsRideTheGlide, GrowingTheCountGrowsTheKnobsWithTheCard) {
    RideGuard guard;
    Rig rig;
    const auto id = rig.bank(kTo);
    const int finalHeight = Rig::heightAt(kFrom);
    rig.arrowTo(id, kFrom);
    expectKnobsRide(rig, id, kTo, kFrom, finalHeight);
}

TEST(MacroCountKnobsRideTheGlide, ShrinkingTheCountShrinksTheKnobsWithTheCard) {
    RideGuard guard;
    Rig rig;
    const auto id = rig.bank(kFrom);
    const int finalHeight = Rig::heightAt(kTo);
    rig.arrowTo(id, kTo);
    expectKnobsRide(rig, id, kFrom, kTo, finalHeight);
}

TEST(MacroCountKnobsRideTheGlide, UndoAndRedoRideTheGlideToo) {
    RideGuard guard;
    Rig rig;
    const auto id = rig.bank(kTo);
    const int small = rig.card(id)->getHeight();
    const int tall = Rig::heightAt(kFrom);
    rig.undo.clearUndoHistory();
    rig.arrowTo(id, kFrom);
    rig.settle(id);

    ASSERT_TRUE(rig.undo.undo()); // the new knobs leave with the card
    ASSERT_TRUE(flushMessageQueue());
    expectKnobsRide(rig, id, kFrom, kTo, small);
    rig.settle(id);

    ASSERT_TRUE(rig.undo.redo()); // and come back with it
    ASSERT_TRUE(flushMessageQueue());
    expectKnobsRide(rig, id, kTo, kFrom, tall);
}

// ---- SwapMotion alone ----------------------------------------------------------------------------------------

namespace {

struct SwapParts {
    juce::Component card;
    juce::Component widget;
    juce::Component label;
    SwapParts() {
        card.setSize(300, 300);
        card.addAndMakeVisible(widget);
        card.addAndMakeVisible(label);
        widget.setBounds(10, 40, 60, 60);
        label.setBounds(10, 10, 60, 20);
    }
    synth::SwapMotion::Arrival arrival() {
        return {juce::Component::SafePointer<juce::Component>(&widget),
                juce::Component::SafePointer<juce::Component>(&label), widget.getBounds().getUnion(label.getBounds())};
    }
    static synth::SwapMotion::Departure departure() {
        return {juce::Image(juce::Image::ARGB, 20, 20, true, juce::SoftwareImageType()), {100, 100, 60, 80}};
    }
};

} // namespace

TEST(SwapMotionTiming, NothingLeavingMeansNoShrinkPhaseAndTheArrivalShowsAtOnce) {
    SwapParts parts;
    synth::SwapMotion swap(parts.card, {}, {parts.arrival()});
    swap.holdForTest({});
    EXPECT_EQ(swap.shrinkMs(), 0.0);
    EXPECT_EQ(swap.growMs(), synth::ui::control_motion::kGrowMs);
    swap.setElapsed(20.0);
    EXPECT_TRUE(parts.widget.isVisible());
    EXPECT_GT(parts.widget.getAlpha(), 0.0f) << "no 190 ms wait before the first knob frame";
    EXPECT_GT(parts.label.getAlpha(), 0.0f);
}

TEST(SwapMotionTiming, RidingTheContainerTakesItsDurationAndEase) {
    SwapParts parts;
    synth::SwapMotion swap(parts.card, {}, {parts.arrival()});
    swap.rideContainer(160.0);
    swap.holdForTest({});
    EXPECT_TRUE(swap.ridesContainer());
    EXPECT_EQ(swap.totalMs(), 160.0);
    swap.setElapsed(40.0);
    EXPECT_NEAR(parts.widget.getAlpha(), 1.0f - std::pow(0.75f, 3.0f), 0.01f);
    swap.setElapsed(160.0);
    EXPECT_FALSE(swap.isRunning());
    EXPECT_FLOAT_EQ(parts.widget.getAlpha(), 1.0f);
}

TEST(SwapMotionTiming, AMixedSwapKeepsItsTwoPhasesEvenAskedToRideTheContainer) {
    SwapParts parts;
    synth::SwapMotion swap(parts.card, {SwapParts::departure()}, {parts.arrival()});
    swap.rideContainer(160.0);
    EXPECT_FALSE(swap.ridesContainer());
    EXPECT_EQ(swap.shrinkMs(), synth::ui::control_motion::kSwapShrinkMs);
    EXPECT_EQ(swap.shrinkMs(), 190.0);
    EXPECT_EQ(swap.totalMs(), 190.0 + synth::ui::control_motion::kGrowMs);
}
