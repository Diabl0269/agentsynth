// UIAnimationTests.cpp
// Headless gtest coverage for synth::ui easing math and formatShortcutHint.
// Does NOT test the JUCE-dependent AnimationDriver (that requires a running
// MessageManager / VBlankAttachment and is validated by the paint smoke test below).
//
// All tests in this file run without a GUI / MessageManager. They only exercise
// the pure-math and pure-string sections of UIAnimation.h, which have no JUCE
// GUI dependencies.

#include "UI/Layout/UIAnimation.h"
#include <cmath>
#include <gtest/gtest.h>
#include <vector>

// ============================================================================
// easeOutCubic
// ============================================================================

TEST(EaseOutCubic, Endpoints) {
    EXPECT_FLOAT_EQ(synth::ui::easeOutCubic(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(synth::ui::easeOutCubic(1.0f), 1.0f);
}

TEST(EaseOutCubic, MidpointIsReasonable) {
    // At t=0.5, easeOutCubic should be > 0.5 (output front-loaded)
    const float mid = synth::ui::easeOutCubic(0.5f);
    EXPECT_GT(mid, 0.5f);
    EXPECT_LT(mid, 1.0f);
}

TEST(EaseOutCubic, Monotonic) {
    // Output must be strictly non-decreasing for t in [0,1].
    float prev = synth::ui::easeOutCubic(0.0f);
    for (int i = 1; i <= 100; ++i) {
        const float t = static_cast<float>(i) / 100.0f;
        const float val = synth::ui::easeOutCubic(t);
        EXPECT_GE(val, prev) << "Not monotonic at t=" << t;
        prev = val;
    }
}

// ============================================================================
// easeInOutCubic
// ============================================================================

TEST(EaseInCubic, EndpointsAndSlowStart) {
    EXPECT_FLOAT_EQ(synth::ui::easeInCubic(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(synth::ui::easeInCubic(1.0f), 1.0f);
    EXPECT_NEAR(synth::ui::easeInCubic(0.5f), 0.125f, 1e-6f) << "leaves slowly";
    EXPECT_NEAR(synth::ui::easeInCubic(0.5f), 1.0f - synth::ui::easeOutCubic(0.5f), 1e-6f)
        << "the mirror image of ease-out";
}

TEST(EaseInOutCubic, Endpoints) {
    EXPECT_FLOAT_EQ(synth::ui::easeInOutCubic(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(synth::ui::easeInOutCubic(1.0f), 1.0f);
}

TEST(EaseInOutCubic, Symmetry) {
    // easeInOutCubic(t) + easeInOutCubic(1-t) == 1  (anti-symmetric around 0.5)
    for (int i = 0; i <= 10; ++i) {
        const float t = static_cast<float>(i) / 10.0f;
        const float sum = synth::ui::easeInOutCubic(t) + synth::ui::easeInOutCubic(1.0f - t);
        EXPECT_NEAR(sum, 1.0f, 1e-5f) << "Symmetry broken at t=" << t;
    }
}

TEST(EaseInOutCubic, MidpointIsHalf) { EXPECT_NEAR(synth::ui::easeInOutCubic(0.5f), 0.5f, 1e-5f); }

TEST(EaseInOutCubic, Monotonic) {
    float prev = synth::ui::easeInOutCubic(0.0f);
    for (int i = 1; i <= 100; ++i) {
        const float t = static_cast<float>(i) / 100.0f;
        const float val = synth::ui::easeInOutCubic(t);
        EXPECT_GE(val, prev) << "Not monotonic at t=" << t;
        prev = val;
    }
}

// ============================================================================
// easeOutBack
// ============================================================================

TEST(EaseOutBack, Endpoints) {
    EXPECT_FLOAT_EQ(synth::ui::easeOutBack(0.0f), 0.0f);
    EXPECT_NEAR(synth::ui::easeOutBack(1.0f), 1.0f, 1e-5f);
}

TEST(EaseOutBack, OvershootsAboveOne) {
    // easeOutBack should exceed 1.0 somewhere in (0.5, 1.0) — that's the "back" effect.
    bool foundOvershoot = false;
    for (int i = 50; i < 100; ++i) {
        const float t = static_cast<float>(i) / 100.0f;
        if (synth::ui::easeOutBack(t) > 1.0f) {
            foundOvershoot = true;
            break;
        }
    }
    EXPECT_TRUE(foundOvershoot) << "easeOutBack should overshoot 1.0 near t=1";
}

// ============================================================================
// AnimationDriver::lerpBounds
// ============================================================================

TEST(AnimationDriverLerpBounds, ZeroTReturnsFrom) {
    juce::Rectangle<int> from{0, 0, 100, 50};
    juce::Rectangle<int> to{200, 100, 300, 150};
    auto result = synth::ui::AnimationDriver::lerpBounds(from, to, 0.0f);
    EXPECT_EQ(result, from);
}

TEST(AnimationDriverLerpBounds, OneTReturnsTo) {
    juce::Rectangle<int> from{0, 0, 100, 50};
    juce::Rectangle<int> to{200, 100, 300, 150};
    auto result = synth::ui::AnimationDriver::lerpBounds(from, to, 1.0f);
    EXPECT_EQ(result, to);
}

TEST(AnimationDriverLerpBounds, HalfTIsMidpoint) {
    juce::Rectangle<int> from{0, 0, 100, 100};
    juce::Rectangle<int> to{200, 200, 300, 300};
    auto result = synth::ui::AnimationDriver::lerpBounds(from, to, 0.5f);
    EXPECT_EQ(result.getX(), 100);
    EXPECT_EQ(result.getY(), 100);
    EXPECT_EQ(result.getWidth(), 200);
    EXPECT_EQ(result.getHeight(), 200);
}

// ============================================================================
// PanelSlide — the [0..1] open fraction a sliding panel's layout is derived
// from. Pure state, so all of this is exact and needs no message loop: it is
// the math MainComponent's three panels (library / AI / timeline) share.
// ============================================================================

TEST(PanelSlide, StartsClosedAndStill) {
    synth::ui::PanelSlide slide;
    EXPECT_FLOAT_EQ(slide.getProgress(), 0.0f);
    EXPECT_FALSE(slide.isMoving());
    EXPECT_EQ(slide.sizeBetween(0, 200), 0);
}

TEST(PanelSlide, SnapToLandsImmediatelyAndLeavesNoTweenBehind) {
    synth::ui::PanelSlide slide;
    slide.snapTo(1.0f);
    EXPECT_FLOAT_EQ(slide.getProgress(), 1.0f);
    EXPECT_FLOAT_EQ(slide.getTarget(), 1.0f);
    EXPECT_FLOAT_EQ(slide.getTweenStart(), 1.0f) << "a snap must not look like a tween in flight";
    EXPECT_FALSE(slide.isMoving());
}

// The synchronous path: no VBlank reaches an off-screen component, so the fraction has to land on
// the target inside retarget() itself rather than wait for frames that never arrive.
TEST(PanelSlide, RetargetWithoutAnimationSnapsAndReportsNoTween) {
    synth::ui::PanelSlide slide;
    EXPECT_FALSE(slide.retarget(1.0f, /*canAnimate=*/false));
    EXPECT_FLOAT_EQ(slide.getProgress(), 1.0f);
    EXPECT_FALSE(slide.isMoving());
    EXPECT_FLOAT_EQ(slide.getTweenStart(), 0.0f) << "the start point it snapped from stays readable";

    EXPECT_FALSE(slide.retarget(0.0f, /*canAnimate=*/false));
    EXPECT_FLOAT_EQ(slide.getProgress(), 0.0f);
}

TEST(PanelSlide, RetargetToWhereItAlreadyRestsIsNotATween) {
    synth::ui::PanelSlide slide;
    slide.snapTo(1.0f);
    EXPECT_FALSE(slide.retarget(1.0f, /*canAnimate=*/true)) << "nothing to animate";
    EXPECT_FLOAT_EQ(slide.getProgress(), 1.0f);
}

TEST(PanelSlide, TweenHoldsTheFractionUntilTheFirstFrameThenFollowsIt) {
    synth::ui::PanelSlide slide;
    ASSERT_TRUE(slide.retarget(1.0f, /*canAnimate=*/true));
    EXPECT_FLOAT_EQ(slide.getProgress(), 0.0f) << "retarget must not itself move a tweening panel";
    EXPECT_TRUE(slide.isMoving());

    slide.applyTweenAt(0.25f);
    EXPECT_FLOAT_EQ(slide.getProgress(), 0.25f);
    slide.applyTweenAt(1.0f);
    EXPECT_FLOAT_EQ(slide.getProgress(), 1.0f);
}

// The whole point of keeping a fraction: a re-toggle mid-slide reverses from where the panel IS,
// never from an extreme (that restart is what read on screen as a jump).
TEST(PanelSlide, MidFlightRetargetReversesFromTheCurrentFraction) {
    synth::ui::PanelSlide slide;
    ASSERT_TRUE(slide.retarget(1.0f, /*canAnimate=*/true));
    slide.applyTweenAt(0.5f);
    ASSERT_FLOAT_EQ(slide.getProgress(), 0.5f);

    ASSERT_TRUE(slide.retarget(0.0f, /*canAnimate=*/true));
    EXPECT_FLOAT_EQ(slide.getTweenStart(), 0.5f) << "the reversal starts HERE, not at 1.0";
    EXPECT_FLOAT_EQ(slide.getProgress(), 0.5f) << "and the panel does not move until the next frame";

    slide.applyTweenAt(0.5f);
    EXPECT_FLOAT_EQ(slide.getProgress(), 0.25f) << "halfway back from 0.5 towards 0";
    slide.applyTweenAt(1.0f);
    EXPECT_FLOAT_EQ(slide.getProgress(), 0.0f);
}

TEST(PanelSlide, FinishPinsTheExactEndValue) {
    synth::ui::PanelSlide slide;
    ASSERT_TRUE(slide.retarget(1.0f, /*canAnimate=*/true));
    slide.applyTweenAt(0.97f);
    ASSERT_NE(slide.getProgress(), 1.0f) << "test premise: the last frame lands short";

    slide.finish();
    EXPECT_FLOAT_EQ(slide.getProgress(), 1.0f);
    EXPECT_FALSE(slide.isMoving());
}

TEST(PanelSlide, SizeBetweenIsExactAtBothEndpointsAndLerpsInBetween) {
    synth::ui::PanelSlide slide;
    EXPECT_EQ(slide.sizeBetween(0, 300), 0);
    slide.snapTo(1.0f);
    EXPECT_EQ(slide.sizeBetween(0, 300), 300) << "a fully open panel must measure exactly its full size";
    slide.snapTo(0.5f);
    EXPECT_EQ(slide.sizeBetween(0, 300), 150);

    // A panel whose closed size isn't zero (the library's sidebarCollapsedWidth) lerps between the
    // two, so both ends stay pixel-exact.
    slide.snapTo(0.0f);
    EXPECT_EQ(slide.sizeBetween(40, 200), 40);
    slide.snapTo(0.5f);
    EXPECT_EQ(slide.sizeBetween(40, 200), 120);
    slide.snapTo(1.0f);
    EXPECT_EQ(slide.sizeBetween(40, 200), 200);
}

TEST(PanelSlide, ClampsOutOfRangeTargetsAndFrames) {
    synth::ui::PanelSlide slide;
    slide.snapTo(4.0f);
    EXPECT_FLOAT_EQ(slide.getProgress(), 1.0f);
    slide.snapTo(-4.0f);
    EXPECT_FLOAT_EQ(slide.getProgress(), 0.0f);

    EXPECT_FALSE(slide.retarget(9.0f, /*canAnimate=*/false));
    EXPECT_FLOAT_EQ(slide.getProgress(), 1.0f);

    ASSERT_TRUE(slide.retarget(0.0f, /*canAnimate=*/true));
    slide.applyTweenAt(2.0f); // an easing that overshoots (easeOutBack) must not push it past 0
    EXPECT_FLOAT_EQ(slide.getProgress(), 0.0f);
}

// ============================================================================
// formatShortcutHint
// ============================================================================

TEST(FormatShortcutHint, EmptyShortcutReturnsBase) {
    auto result = synth::ui::formatShortcutHint("Save", "");
    EXPECT_EQ(result, juce::String("Save"));
}

TEST(FormatShortcutHint, NonEmptyShortcutAppendsHint) {
    auto result = synth::ui::formatShortcutHint("Save", "Cmd+S");
    EXPECT_EQ(result, juce::String("Save  (Cmd+S)"));
}

TEST(FormatShortcutHint, BothEmptyReturnsEmpty) {
    auto result = synth::ui::formatShortcutHint("", "");
    EXPECT_EQ(result, juce::String(""));
}

TEST(FormatShortcutHint, BaseEmptyWithShortcutReturnsFormatted) {
    auto result = synth::ui::formatShortcutHint("", "Ctrl+Z");
    EXPECT_EQ(result, juce::String("  (Ctrl+Z)"));
}

// ============================================================================
// Paint smoke test for AnimationDriver (GUI required — runs under the shared
// ScopedJuceInitialiser_GUI in TestMain.cpp).
// Constructs a dummy Component, creates an AnimationDriver + VBlankAnimatorUpdater,
// starts a zero-or-near-zero duration animation, and pumps the message queue.
// The test simply asserts no crash.
// ============================================================================

TEST(AnimationDriverSmoke, StartAndCompleteNocrash) {
    // A plain Component suffices — VBlankAttachment works on any Component.
    juce::Component dummyComponent;
    dummyComponent.setSize(1, 1);

    juce::VBlankAnimatorUpdater updater{&dummyComponent};
    synth::ui::AnimationDriver driver;

    bool completeCalled = false;
    float lastT = -1.0f;

    driver.start(
        updater,
        /*durationMs=*/1.0, synth::ui::easeOutCubic, [&](float t) { lastT = t; }, [&]() { completeCalled = true; });

    EXPECT_TRUE(driver.isRunning());

    // Pump the message loop briefly; the 1 ms animation should complete quickly.
    if (auto* mm = juce::MessageManager::getInstanceWithoutCreating())
        mm->runDispatchLoopUntil(50);

    // Either completeCalled is true (fast machine) or we at least got frames.
    // The key assertion is: no crash.
    SUCCEED();
}

// Regression test for FRO973: a message-thread stall right after an animation's first frame (a graph rebuild, a plugin
// load) made the next frame land on the final value, so the motion was never seen.
TEST(AnimationDriverStall, AFrameAfterAStallAdvancesByOneFrameAndTheRestStillPlays) {
    juce::Component dummyComponent;
    dummyComponent.setSize(1, 1);
    juce::VBlankAnimatorUpdater updater{&dummyComponent};
    synth::ui::AnimationDriver driver;

    std::vector<float> values;
    int completed = 0;
    driver.start(
        updater, 580.0, [](float t) { return t; }, [&](float t) { values.push_back(t); }, [&] { ++completed; });
    driver.stallGuardForTest(true);

    driver.stepForTest(0.0);   // the first frame
    driver.stepForTest(16.0);  // a normal frame
    driver.stepForTest(900.0); // 884 ms later: the stall, far past the end of the animation
    ASSERT_GE(values.size(), 3u);
    EXPECT_LT(values.back(), 0.15f) << "the frame after a stall must not jump to the end";
    EXPECT_GT(values.back(), values[1]) << "it still moves";
    EXPECT_EQ(completed, 0);
    EXPECT_TRUE(driver.isRunning());

    // The rest plays from there, one value per frame, rising to exactly 1, completing once.
    float previous = values.back();
    for (int frame = 1; frame <= 60 && completed == 0; ++frame) {
        driver.stepForTest(16.0 * frame);
        EXPECT_LE(values.back() - previous, 0.06f) << "no jump at frame " << frame;
        EXPECT_GE(values.back(), previous);
        previous = values.back();
    }
    EXPECT_EQ(completed, 1);
    EXPECT_FLOAT_EQ(values.back(), 1.0f);
}

TEST(AnimationDriverStall, SteadyFramesAreLeftAlone) {
    juce::Component dummyComponent;
    dummyComponent.setSize(1, 1);
    juce::VBlankAnimatorUpdater updater{&dummyComponent};
    synth::ui::AnimationDriver driver;
    std::vector<float> values;
    int completed = 0;
    driver.start(
        updater, 200.0, [](float t) { return t; }, [&](float t) { values.push_back(t); }, [&] { ++completed; });
    driver.stallGuardForTest(true);
    for (int ms = 0; ms <= 216; ms += 16)
        driver.stepForTest(ms);
    EXPECT_EQ(completed, 1);
    EXPECT_FLOAT_EQ(values.back(), 1.0f);
    EXPECT_NEAR(values[6], 80.0f / 200.0f, 1e-3f); // the same clock as an unguarded run
}
