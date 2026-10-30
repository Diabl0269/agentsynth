// KeyedPointTweenTests.cpp -- keyed drawn offsets that ease to zero: intermediate values across frames, a retarget
// that continues from where the point is drawn, and plain landing under Reduce Motion.

#include "UI/Layout/KeyGlide/KeyedPointTween.h"
#include "UI/Layout/ReducedMotion.h"
#include <gtest/gtest.h>

using synth::ui::KeyedPointTween;

namespace {
struct ReducedMotionGuard {
    explicit ReducedMotionGuard(bool reduced) { synth::ui::setReducedMotionForTest(reduced); }
    ~ReducedMotionGuard() { synth::ui::setReducedMotionForTest(std::nullopt); }
};

struct Rig {
    juce::Component owner;
    KeyedPointTween tween{owner};
    ReducedMotionGuard motion{false};
    Rig() { tween.forceAnimateForTest(true); }
};
} // namespace

TEST(KeyedPointTween, AnOffsetPassesThroughIntermediateValuesAndSettlesAtZero) {
    Rig r;
    r.tween.retarget(7, {100.0f, 50.0f}, {140.0f, 50.0f});
    ASSERT_TRUE(r.tween.isAnimating());
    EXPECT_NEAR(r.tween.offsetFor(7).x, -40.0f, 1e-4f) << "drawn at the old place at first";

    float last = -40.0f;
    for (int frame = 0; frame < 4; ++frame) {
        r.tween.stepForTest(20.0 * (frame + 1));
        const float x = r.tween.offsetFor(7).x;
        EXPECT_GT(x, last) << "closing in on the resting place, frame " << frame;
        EXPECT_LT(x, 0.0f);
        last = x;
    }
    r.tween.stepForTest(300.0);
    EXPECT_EQ(r.tween.offsetFor(7), juce::Point<float>());
    EXPECT_FALSE(r.tween.isAnimating()) << "it stops when settled";
}

TEST(KeyedPointTween, AnEaseOutCubicShapeCoversMostOfTheDistanceEarly) {
    Rig r;
    r.tween.retarget(1, {0.0f, 0.0f}, {100.0f, 0.0f});
    r.tween.stepForTest(KeyedPointTween::kMs / 2.0);
    EXPECT_NEAR(r.tween.offsetFor(1).x, -100.0f * 0.125f, 0.5f) << "easeOutCubic is 87.5 percent done at half time";
}

TEST(KeyedPointTween, ARetargetContinuesFromWhereThePointIsDrawnAndOtherKeysKeepTheirOwnOffset) {
    Rig r;
    r.tween.retarget(1, {0.0f, 0.0f}, {100.0f, 0.0f});
    r.tween.retarget(2, {0.0f, 20.0f}, {0.0f, 80.0f});
    r.tween.stepForTest(40.0);
    const auto drawn = r.tween.drawnAt(1, {100.0f, 0.0f});
    const float otherBefore = r.tween.offsetFor(2).y;
    EXPECT_GT(drawn.x, 0.0f);
    EXPECT_LT(drawn.x, 100.0f);

    r.tween.retarget(1, drawn, {200.0f, 0.0f});
    EXPECT_NEAR(r.tween.drawnAt(1, {200.0f, 0.0f}).x, drawn.x, 1e-3f) << "no jump when it is retargeted";
    EXPECT_NEAR(r.tween.offsetFor(2).y, otherBefore, 1e-3f) << "the other point is not disturbed";
    r.tween.stepForTest(20.0);
    EXPECT_GT(r.tween.drawnAt(1, {200.0f, 0.0f}).x, drawn.x);
    EXPECT_LT(r.tween.drawnAt(1, {200.0f, 0.0f}).x, 200.0f);
}

TEST(KeyedPointTween, EraseAndClearLandPointsAtOnce) {
    Rig r;
    r.tween.retarget(1, {0.0f, 0.0f}, {100.0f, 0.0f});
    r.tween.retarget(2, {0.0f, 0.0f}, {0.0f, 100.0f});
    r.tween.erase(1);
    EXPECT_EQ(r.tween.offsetFor(1), juce::Point<float>());
    EXPECT_TRUE(r.tween.isAnimating());
    r.tween.clear();
    EXPECT_FALSE(r.tween.isAnimating());
}

TEST(KeyedPointTween, ReduceMotionAndAnimationsOffLandPlainly) {
    juce::Component owner;
    KeyedPointTween tween(owner);
    tween.forceAnimateForTest(true);
    {
        ReducedMotionGuard reduced(true);
        tween.retarget(1, {0.0f, 0.0f}, {100.0f, 0.0f});
        EXPECT_FALSE(tween.isAnimating());
        EXPECT_EQ(tween.offsetFor(1), juce::Point<float>());
    }
    synth::ui::setAnimationMode(synth::ui::AnimationMode::off);
    tween.retarget(1, {0.0f, 0.0f}, {100.0f, 0.0f});
    EXPECT_FALSE(tween.isAnimating());
    synth::ui::setAnimationMode(synth::ui::AnimationMode::followSystem);
}

TEST(KeyedPointTween, AnOwnerThatIsNotShowingDoesNotAnimate) {
    juce::Component owner;
    KeyedPointTween tween(owner);
    ReducedMotionGuard motion(false);
    tween.retarget(1, {0.0f, 0.0f}, {100.0f, 0.0f});
    EXPECT_FALSE(tween.isAnimating());
}

TEST(KeyedPointTween, UnderReduceMotionARestoreLandsAtOnceAndFadesInThere) {
    juce::Component owner;
    KeyedPointTween tween(owner);
    tween.forceAnimateForTest(true);
    ReducedMotionGuard reduced(true);
    tween.retarget(1, {0.0f, 0.0f}, {100.0f, 0.0f}, true);
    EXPECT_EQ(tween.offsetFor(1), juce::Point<float>()) << "already at the new place";
    EXPECT_LT(tween.alphaFor(1), 0.5f);
    EXPECT_TRUE(tween.isFading());
    tween.stepForTest(40.0);
    EXPECT_GT(tween.alphaFor(1), 0.5f);
    EXPECT_LT(tween.alphaFor(1), 1.0f);
    tween.stepForTest(500.0);
    EXPECT_FLOAT_EQ(tween.alphaFor(1), 1.0f);
    EXPECT_FALSE(tween.isAnimating());
}

TEST(KeyedPointTween, AnimationsOffLandsARestoreWithoutAFade) {
    juce::Component owner;
    KeyedPointTween tween(owner);
    tween.forceAnimateForTest(true);
    synth::ui::setAnimationMode(synth::ui::AnimationMode::off);
    tween.retarget(1, {0.0f, 0.0f}, {100.0f, 0.0f}, true);
    synth::ui::setAnimationMode(synth::ui::AnimationMode::followSystem);
    EXPECT_FALSE(tween.isAnimating());
    EXPECT_FLOAT_EQ(tween.alphaFor(1), 1.0f);
}

namespace {
struct DirtyRecorder {
    DirtyRecorder() {
        synth::ui::setKeyedTweenDirtyProbeForTest(
            [this](juce::Component&, juce::Rectangle<int> a) { areas.push_back(a); });
    }
    ~DirtyRecorder() { synth::ui::setKeyedTweenDirtyProbeForTest({}); }
    std::vector<juce::Rectangle<int>> areas;
};
} // namespace

// Regression test for FRO1197: a box-only repaint left stale pixels for owners that paint more than the moving dot.
TEST(KeyedPointTween, EveryRepaintItAsksForIsTheWholeOwnerByDefault) {
    Rig r;
    r.owner.setSize(300, 200);
    DirtyRecorder dirty;
    r.tween.retarget(1, {10.0f, 10.0f}, {20.0f, 20.0f});
    r.tween.stepForTest(40.0);
    r.tween.retarget(2, {250.0f, 150.0f}, {260.0f, 160.0f});
    r.tween.clear();
    ASSERT_FALSE(dirty.areas.empty());
    for (const auto& a : dirty.areas)
        EXPECT_EQ(a, r.owner.getLocalBounds());
}

TEST(KeyedPointTween, AnOwnerThatOptsInRepaintsOnlyTheMovingBoxPlusPadding) {
    Rig r;
    r.owner.setSize(300, 200);
    r.tween.repaintOnlyAround(5);
    DirtyRecorder dirty;
    r.tween.retarget(1, {10.0f, 10.0f}, {20.0f, 30.0f});
    ASSERT_FALSE(dirty.areas.empty());
    EXPECT_EQ(dirty.areas.front(), juce::Rectangle<int>(5, 5, 20, 30));
}
