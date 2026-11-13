#include "MotionStep.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

// Topic: the two small helpers the last instant changes share (docs/layout/animation.md#fading-things-in-and-out):
// FadeAmount (the fade of something its owner paints) and ChevronTurn (the turn of a fold arrow). Headless: the
// animated path is forced with FadeAnimateGuard and stepped by hand.

using synth::ui::AnimationMode;
using synth::ui::ChevronTurn;
using synth::ui::FadeAmount;

TEST(FadeAmount, FadesInAndOutAndAReversalStartsFromWhereItIs) {
    juce::Component owner;
    FadeAmount fade(owner);
    FadeAnimateGuard guard;
    int settled = 0;
    fade.onSettled = [&] { ++settled; };

    fade.setShown(true);
    EXPECT_TRUE(fade.isShown());
    EXPECT_TRUE(fade.isFading());
    EXPECT_FLOAT_EQ(fade.value(), 0.0f) << "frame 0 is the first fade frame";
    stepMotion(0.5f);
    EXPECT_NEAR(fade.value(), 0.5f, 1.0e-4f);

    fade.setShown(false); // a reversal mid-fade: from 0.5, not from an extreme
    EXPECT_FALSE(fade.isShown());
    EXPECT_NEAR(fade.value(), 0.5f, 1.0e-4f);
    stepMotion(0.5f);
    EXPECT_NEAR(fade.value(), 0.25f, 1.0e-4f);
    stepMotion(1.0f);
    EXPECT_FLOAT_EQ(fade.value(), 0.0f);
    EXPECT_FALSE(fade.isFading());
    EXPECT_EQ(settled, 1);
}

TEST(FadeAmount, ReduceMotionStillFadesAndAnimationsOffAndOffScreenLandAtOnce) {
    juce::Component owner;
    FadeAmount fade(owner);
    {
        FadeAnimateGuard reduced(AnimationMode::reduced);
        fade.setShown(true);
        EXPECT_TRUE(fade.isFading()) << "Reduce Motion is a short fade, not a jump";
        stepMotion(1.0f);
        EXPECT_FLOAT_EQ(fade.value(), 1.0f);
    }
    {
        FadeAnimateGuard off(AnimationMode::off);
        fade.setShown(false);
        EXPECT_FALSE(fade.isFading());
        EXPECT_FLOAT_EQ(fade.value(), 0.0f);
    }
    fade.setShown(true); // not on screen and not forced
    EXPECT_FALSE(fade.isFading());
    EXPECT_FLOAT_EQ(fade.value(), 1.0f);
}

TEST(ChevronTurn, TurnsOverTimeAndAReversalStartsFromWhereTheArrowIs) {
    juce::Component owner;
    ChevronTurn turn(owner, true);
    FadeAnimateGuard guard;

    EXPECT_FLOAT_EQ(turn.openness(), 1.0f);
    turn.setOpen(false);
    EXPECT_TRUE(turn.isTurning());
    EXPECT_FLOAT_EQ(turn.openness(), 1.0f) << "nothing has moved at frame 0";
    stepMotion(0.5f);
    EXPECT_GT(turn.openness(), 0.0f);
    EXPECT_LT(turn.openness(), 1.0f);
    const float mid = turn.openness();

    turn.setOpen(true); // back from the middle
    EXPECT_NEAR(turn.openness(), mid, 1.0e-4f);
    stepMotion(0.5f);
    EXPECT_GT(turn.openness(), mid);
    stepMotion(1.0f);
    EXPECT_FLOAT_EQ(turn.openness(), 1.0f);
    EXPECT_FALSE(turn.isTurning());
}

TEST(ChevronTurn, ReduceMotionAnimationsOffAndOffScreenLandAtOnce) {
    juce::Component owner;
    ChevronTurn turn(owner, true);
    {
        FadeAnimateGuard reduced(AnimationMode::reduced);
        turn.setOpen(false);
        EXPECT_FALSE(turn.isTurning()) << "a turn is movement: Reduce Motion flips it";
        EXPECT_FLOAT_EQ(turn.openness(), 0.0f);
    }
    {
        FadeAnimateGuard off(AnimationMode::off);
        turn.setOpen(true);
        EXPECT_FALSE(turn.isTurning());
        EXPECT_FLOAT_EQ(turn.openness(), 1.0f);
    }
    turn.setOpen(false); // not on screen and not forced
    EXPECT_FALSE(turn.isTurning());
    EXPECT_FLOAT_EQ(turn.openness(), 0.0f);
}
