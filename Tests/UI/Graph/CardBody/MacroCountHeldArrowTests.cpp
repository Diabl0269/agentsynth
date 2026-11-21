// MacroCountHeldArrowTests.cpp
//
// Regression test for FRO2145: a held Controllers arrow made the knobs pop. Each step of the hold re-read the card's
// conditions, and that landed the knob the step before had only started to grow. Now a step that comes while the
// knobs move adds to the motion: a knob still growing keeps its progress, one still shrinking stays drawn, and only
// the knobs the new step adds or takes away start their own motion. The steps come faster than the 160 ms glide
// (6 to 30 a second), driven through the same parameter gesture the arrows make, with the swap stepped by hand on
// one running clock and the card glide stepped from each step (it re-aims from where it is).

#include "MacroCountRideRig.h"

#include <array>
#include <gtest/gtest.h>

using namespace macro_ride_test;

namespace {

constexpr int kStart = 4;
// Milliseconds between the steps of the hold (shorter than the 160 ms glide a knob rides; a window opacity is 8 bits,
// so a knob 150 ms in already reads as full).
constexpr std::array<double, 6> kGaps = {100.0, 33.0, 110.0, 60.0, 120.0, 80.0};

juce::Component* widgetOf(Rig& rig, NodeID id, int knob) {
    const auto parts = synth::cardbody_test::controlParts(*rig.card(id), "macro" + juce::String(knob));
    return parts ? parts->widget : nullptr;
}

} // namespace

TEST(MacroCountHeldArrow, EachStepGrowsItsKnobWhileTheOneBeforeIsStillPartGrown) {
    // Regression test for FRO2145: the next step landed the previous knob at full size at once.
    RideGuard guard;
    Rig rig;
    const auto id = rig.bank(kStart);
    double now = 0.0;
    for (size_t step = 0; step < kGaps.size() + 1; ++step) {
        if (step > 0) {
            now += kGaps[step - 1];
            synth::cardbody_test::stepSwap(*rig.card(id), now);
        }
        const int count = kStart + 1 + (int)step;
        rig.arrowTo(id, count);
        ASSERT_TRUE(synth::cardbody_test::isSwapRunning(*rig.card(id))) << "step " << step;
        if (step > 0) {
            auto* previous = widgetOf(rig, id, count - 1);
            ASSERT_NE(previous, nullptr);
            EXPECT_TRUE(previous->isVisible()) << "step " << step;
            EXPECT_GT(previous->getAlpha(), 0.0f) << "the knob before is not hidden at step " << step;
            EXPECT_LT(previous->getAlpha(), 1.0f) << "the knob before has not landed at step " << step;
            EXPECT_GT(scaleOf(*previous), 0.0) << "step " << step;
            EXPECT_LT(scaleOf(*previous), 1.0) << "the knob before has not jumped to full size at step " << step;
        }
        rig.card(id)->heightGlideForTest().stepForTest(0.0);
    }
    synth::cardbody_test::stepSwap(*rig.card(id), now + 1000.0);
    rig.card(id)->heightGlideForTest().stepForTest(1000.0);
    rig.editor.finishCardGlideForTest();
    EXPECT_FALSE(synth::cardbody_test::isSwapRunning(*rig.card(id)));
    for (const auto& knob : knobsBetween(rig, id, kStart, kStart + (int)kGaps.size() + 1))
        for (auto* part : {knob.widget, knob.label}) {
            EXPECT_TRUE(part->isVisible()) << knob.id;
            EXPECT_FLOAT_EQ(part->getAlpha(), 1.0f) << knob.id;
            EXPECT_TRUE(part->getTransform().isIdentity()) << knob.id;
        }
}

TEST(MacroCountHeldArrow, AKnobGrowsOnItsOwnClockAfterAStepInterruptedTheOneBefore) {
    RideGuard guard;
    Rig rig;
    const auto id = rig.bank(kStart);
    rig.arrowTo(id, kStart + 1);
    synth::cardbody_test::stepSwap(*rig.card(id), 100.0);
    auto* first = widgetOf(rig, id, kStart + 1);
    const double firstAt100 = scaleOf(*first);
    rig.arrowTo(id, kStart + 2);
    auto* second = widgetOf(rig, id, kStart + 2);
    EXPECT_NEAR(scaleOf(*first), firstAt100, 1e-6) << "the interrupt does not move the first knob";
    synth::cardbody_test::stepSwap(*rig.card(id), 140.0);
    EXPECT_GT(scaleOf(*first), firstAt100) << "the first knob keeps growing";
    EXPECT_GT(scaleOf(*second), 0.0) << "the second starts from the step, not from the first's clock";
    EXPECT_LT(scaleOf(*second), scaleOf(*first));
}

TEST(MacroCountHeldArrow, HeldTheOtherWayTheKnobsOfEarlierStepsKeepShrinkingInsteadOfPopping) {
    // Regression test for FRO2145: the next step cleared the ghosts of the knob the step before took away.
    RideGuard guard;
    Rig rig;
    const int top = kStart + (int)kGaps.size() + 1;
    const auto id = rig.bank(top);
    double now = 0.0;
    for (size_t step = 0; step < kGaps.size() + 1; ++step) {
        if (step > 0) {
            now += kGaps[step - 1];
            synth::cardbody_test::stepSwap(*rig.card(id), now);
        }
        rig.arrowTo(id, top - 1 - (int)step);
        const auto ghosts = synth::cardbody_test::swapGhostRects(*rig.card(id));
        if (step > 0) {
            ASSERT_GE(ghosts.size(), 2u) << "the knob before is still drawn at step " << step;
            const auto& before = ghosts[ghosts.size() - 2];
            EXPECT_GT(before.getHeight(), 0.0f) << "step " << step;
            EXPECT_LT(before.getHeight(), ghosts.back().getHeight()) << "part-shrunk, not full again, at step " << step;
        }
    }
    synth::cardbody_test::stepSwap(*rig.card(id), now + 1000.0);
    EXPECT_TRUE(synth::cardbody_test::swapGhostRects(*rig.card(id)).empty());
    EXPECT_FALSE(synth::cardbody_test::isSwapRunning(*rig.card(id)));
}

TEST(MacroCountHeldArrow, ASingleClickStillGrowsOneKnobOnTheGlide) {
    RideGuard guard;
    Rig rig;
    const auto id = rig.bank(kStart);
    rig.arrowTo(id, kStart + 1);
    rig.stepTo(id, glideMs() * 0.25);
    auto* knob = widgetOf(rig, id, kStart + 1);
    EXPECT_GT(scaleOf(*knob), 0.0);
    EXPECT_LT(scaleOf(*knob), 1.0);
    rig.stepTo(id, glideMs());
    EXPECT_FALSE(synth::cardbody_test::isSwapRunning(*rig.card(id)));
    EXPECT_TRUE(knob->getTransform().isIdentity());
}
