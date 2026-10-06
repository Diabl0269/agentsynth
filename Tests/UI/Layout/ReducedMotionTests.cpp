// ReducedMotionTests.cpp (docs/layout/animation.md#reduced-motion): the Animations mode decides what
// prefersReducedMotion() answers and how long motions run; Off makes the shared driver and the popup durations
// instant.
#include "UI/Layout/PopupMotion.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Layout/UIAnimation.h"
#include <gtest/gtest.h>

namespace {

using synth::ui::AnimationMode;

// Puts the mode and the test override back whatever a test did to them.
struct ModeGuard {
    ~ModeGuard() {
        synth::ui::setAnimationMode(AnimationMode::followSystem);
        synth::ui::setReducedMotionForTest(std::nullopt);
    }
};

TEST(ReducedMotionTests, DefaultModeFollowsTheSystem) {
    ModeGuard guard;
    EXPECT_EQ(synth::ui::animationMode(), AnimationMode::followSystem);
}

TEST(ReducedMotionTests, FollowSystemAsksTheOs) {
    ModeGuard guard;
    synth::ui::setAnimationMode(AnimationMode::followSystem);
    synth::ui::setReducedMotionForTest(true);
    EXPECT_TRUE(synth::ui::prefersReducedMotion());
    synth::ui::setReducedMotionForTest(false);
    EXPECT_FALSE(synth::ui::prefersReducedMotion());
    EXPECT_FALSE(synth::ui::animationsOff());
}

TEST(ReducedMotionTests, FullIgnoresTheOsAndReducedAndOffImplyReducedMotion) {
    ModeGuard guard;
    synth::ui::setReducedMotionForTest(true);
    synth::ui::setAnimationMode(AnimationMode::full);
    EXPECT_FALSE(synth::ui::prefersReducedMotion());

    synth::ui::setReducedMotionForTest(false);
    synth::ui::setAnimationMode(AnimationMode::reduced);
    EXPECT_TRUE(synth::ui::prefersReducedMotion());
    EXPECT_FALSE(synth::ui::animationsOff());

    synth::ui::setAnimationMode(AnimationMode::off);
    EXPECT_TRUE(synth::ui::prefersReducedMotion());
    EXPECT_TRUE(synth::ui::animationsOff());
}

TEST(ReducedMotionTests, MotionMsPicksFullReducedOrZeroByMode) {
    ModeGuard guard;
    synth::ui::setAnimationMode(AnimationMode::full);
    EXPECT_DOUBLE_EQ(synth::ui::motionMs(160.0, 80.0), 160.0);
    synth::ui::setAnimationMode(AnimationMode::reduced);
    EXPECT_DOUBLE_EQ(synth::ui::motionMs(160.0, 80.0), 80.0);
    synth::ui::setAnimationMode(AnimationMode::off);
    EXPECT_DOUBLE_EQ(synth::ui::motionMs(160.0, 80.0), 0.0);
}

TEST(ReducedMotionTests, ModeStringsRoundTripAndUnknownFollowsTheSystem) {
    for (auto mode : {AnimationMode::followSystem, AnimationMode::full, AnimationMode::reduced, AnimationMode::off})
        EXPECT_EQ(synth::ui::animationModeFromString(synth::ui::animationModeToString(mode)), mode);
    EXPECT_EQ(synth::ui::animationModeToString(AnimationMode::followSystem), "follow");
    EXPECT_EQ(synth::ui::animationModeFromString("nonsense"), AnimationMode::followSystem);
    EXPECT_EQ(synth::ui::animationModeFromString(""), AnimationMode::followSystem);
}

TEST(ReducedMotionTests, AnimationDriverLandsAtOnceUnderOff) {
    ModeGuard guard;
    juce::Component host;
    juce::VBlankAnimatorUpdater updater{&host};
    synth::ui::AnimationDriver driver;

    float last = -1.0f;
    bool completed = false;
    synth::ui::setAnimationMode(AnimationMode::off);
    driver.start(updater, 250.0, synth::ui::easeOutCubic, [&](float t) { last = t; }, [&] { completed = true; });
    EXPECT_FLOAT_EQ(last, 1.0f);
    EXPECT_TRUE(completed);
    EXPECT_FALSE(driver.isRunning());

    // Any other mode still runs the animation over time.
    last = -1.0f;
    completed = false;
    synth::ui::setAnimationMode(AnimationMode::full);
    driver.start(updater, 250.0, synth::ui::easeOutCubic, [&](float t) { last = t; }, [&] { completed = true; });
    EXPECT_FALSE(completed);
    EXPECT_TRUE(driver.isRunning());
    driver.stop(updater);
}

TEST(ReducedMotionTests, PopupDurationsAreZeroUnderOff) {
    using synth::ui::popup_motion::durationMs;
    using synth::ui::popup_motion::Phase;
    ModeGuard guard;
    synth::ui::setAnimationMode(AnimationMode::off);
    for (bool reduce : {false, true}) {
        EXPECT_DOUBLE_EQ(durationMs(Phase::In, reduce), 0.0);
        EXPECT_DOUBLE_EQ(durationMs(Phase::Out, reduce), 0.0);
    }
    synth::ui::setAnimationMode(AnimationMode::full);
    EXPECT_DOUBLE_EQ(durationMs(Phase::In, false), synth::ui::popup_motion::kInMs);
    EXPECT_DOUBLE_EQ(durationMs(Phase::Out, true), synth::ui::popup_motion::kReducedMs);
}

} // namespace
