// ScrollTweenTests.cpp -- headless coverage of the wheel-notch tween's pure logic and of the
// runner's "not showing -> apply immediately" gate.

#include "UI/Layout/ScrollTween.h"
#include <gtest/gtest.h>

namespace {
// A view with optional clamp range; apply returns what actually moved.
struct FakeView {
    double pos[2] = {0.0, 0.0};
    double maxPos = 1.0e9;
    double apply(int axis, double delta) {
        const double before = pos[axis];
        pos[axis] = std::clamp(pos[axis] + delta, 0.0, maxPos);
        return pos[axis] - before;
    }
};
} // namespace

TEST(ScrollTween, FinishesExactlyAtTargetAndMovesPartwayFirst) {
    synth::ui::ScrollTween tween;
    FakeView view;
    tween.retarget(0, 100.0);
    EXPECT_TRUE(tween.step(0.5, [&](int a, double d) { return view.apply(a, d); }));
    EXPECT_NEAR(view.pos[0], 50.0, 1e-9);
    EXPECT_FALSE(tween.step(1.0, [&](int a, double d) { return view.apply(a, d); }));
    EXPECT_DOUBLE_EQ(view.pos[0], 100.0);
    EXPECT_FALSE(tween.isActive());
}

TEST(ScrollTween, RetargetAccumulatesIntoTheRemainingDistance) {
    synth::ui::ScrollTween tween;
    FakeView view;
    const auto apply = [&](int a, double d) { return view.apply(a, d); };
    tween.retarget(0, 100.0);
    tween.step(0.4, apply); // 40 travelled, 60 remaining
    tween.retarget(0, 100.0);
    tween.step(0.5, apply); // rebased from 40, target 200 -> halfway = 120
    EXPECT_NEAR(view.pos[0], 120.0, 1e-9);
    EXPECT_FALSE(tween.step(1.0, apply));
    EXPECT_DOUBLE_EQ(view.pos[0], 200.0); // no distance dropped, none double-counted
}

TEST(ScrollTween, AxesAreIndependentAndShareTheClock) {
    synth::ui::ScrollTween tween;
    FakeView view;
    const auto apply = [&](int a, double d) { return view.apply(a, d); };
    tween.retarget(0, 10.0);
    tween.retarget(1, 40.0);
    tween.step(1.0, apply);
    EXPECT_DOUBLE_EQ(view.pos[0], 10.0);
    EXPECT_DOUBLE_EQ(view.pos[1], 40.0);
}

TEST(ScrollTween, ClampEndsTheTween) {
    synth::ui::ScrollTween tween;
    FakeView view;
    view.maxPos = 30.0;
    const auto apply = [&](int a, double d) { return view.apply(a, d); };
    tween.retarget(0, 100.0);
    tween.step(0.2, apply);               // 20, still inside the range
    EXPECT_FALSE(tween.step(0.6, apply)); // asks for 60, clamped at 30 -> done, not 1.0 yet
    EXPECT_DOUBLE_EQ(view.pos[0], 30.0);
    EXPECT_FALSE(tween.isActive());
}

TEST(ScrollTween, StepWithoutRetargetDoesNothing) {
    synth::ui::ScrollTween tween;
    bool called = false;
    EXPECT_FALSE(tween.step(1.0, [&](int, double) {
        called = true;
        return 0.0;
    }));
    EXPECT_FALSE(called);
}

TEST(ScrollTweenRunner, HostThatIsNotShowingIsNeverTweened) {
    juce::Component host; // no peer: isShowing() == false, the headless path every test takes
    synth::ui::ScrollTweenRunner runner;
    bool scrolled = false;
    EXPECT_FALSE(runner.push(host, 0, 5.0, [](int) { return 0.0; }, [&](int, double) { scrolled = true; }));
    EXPECT_FALSE(scrolled);
    EXPECT_FALSE(runner.isActive());
}
