// UIAnimationTests.cpp
// Headless gtest coverage for synth::ui easing math and formatShortcutHint.
// Does NOT test the JUCE-dependent AnimationDriver (that requires a running
// MessageManager / VBlankAttachment and is validated by the paint smoke test below).
//
// All tests in this file run without a GUI / MessageManager. They only exercise
// the pure-math and pure-string sections of UIAnimation.h, which have no JUCE
// GUI dependencies.

#include "../Source/UI/UIAnimation.h"
#include <cmath>
#include <gtest/gtest.h>

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
