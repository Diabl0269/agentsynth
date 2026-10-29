// KeyGlideTests.cpp -- the shared key-hold model (distance by time, speed factor, release) and KeyHold, the frame glue
// around it (key up ends it once, distance is the tap plus the glide).

#include "KeyGlideTestHost.h"
#include "UI/Layout/KeyGlide/KeyGlide.h"
#include "UI/Layout/KeyGlide/KeyHold.h"
#include <cmath>
#include <gtest/gtest.h>

using namespace synth::ui;
using key_glide_test::FakeKeys;

namespace {
const KeyGlideConfig kConfig{2.0, 10.0, 100.0, 1000.0};

// The integral of the cubic ease-in from 2 to 10 units per second over a 1 s ramp, then flat at the cap.
double expectedDistance(double glideSeconds) {
    const double ramp = 1.0;
    return glideSeconds <= ramp ? 2.0 * glideSeconds + 8.0 * ramp * std::pow(glideSeconds / ramp, 4.0) / 4.0
                                : 2.0 * ramp + 8.0 * ramp / 4.0 + 10.0 * (glideSeconds - ramp);
}
} // namespace

TEST(KeyGlideModel, ATapMovesNothingAndTheGlideFollowsTheRampIntegral) {
    KeyGlide glide(kConfig);
    ASSERT_TRUE(glide.press(500.0));
    EXPECT_FALSE(glide.isGliding(500.0 + 99.0));
    EXPECT_EQ(glide.advance(500.0 + 99.0), 0.0);
    EXPECT_EQ(glide.velocityAt(500.0), 0.0);

    EXPECT_NEAR(glide.velocityAt(600.0), 2.0, 1e-9);
    EXPECT_NEAR(glide.velocityAt(1600.0), 10.0, 1e-9);
    EXPECT_NEAR(glide.advance(600.0 + 500.0), expectedDistance(0.5), 1e-9);
    EXPECT_NEAR(glide.advance(600.0 + 3000.0), expectedDistance(3.0) - expectedDistance(0.5), 1e-9)
        << "advance returns what is new since the last call, past the cap too";
}

TEST(KeyGlideModel, TheTotalDoesNotDependOnHowOftenItIsAskedAndTheSpeedFactorScalesWhatIsNew) {
    KeyGlide whole(kConfig), split(kConfig), scaled(kConfig);
    whole.press(0.0);
    split.press(0.0);
    scaled.press(0.0);
    double sum = 0.0;
    for (int ms = 100; ms <= 1100; ms += 16)
        sum += split.advance((double)ms);
    sum += split.advance(1100.0);
    EXPECT_NEAR(sum, whole.advance(1100.0), 1e-9);

    scaled.advance(600.0);
    EXPECT_NEAR(scaled.advance(1100.0, 3.0), 3.0 * (expectedDistance(1.0) - expectedDistance(0.5)), 1e-9);
}

TEST(KeyGlideModel, ARepeatKeepsTheRunningHoldAndReleaseEndsIt) {
    KeyGlide glide(kConfig);
    ASSERT_TRUE(glide.press(0.0));
    EXPECT_FALSE(glide.press(50.0)) << "an auto-repeat does not restart the ramp";
    glide.release();
    EXPECT_FALSE(glide.isHeld());
    EXPECT_EQ(glide.advance(5000.0), 0.0);
    EXPECT_TRUE(glide.press(5000.0));
}

TEST(KeyHoldTest, DistanceIsTheTapPlusTheGlideAndKeyUpEndsItOnce) {
    juce::Component owner;
    FakeKeys keys;
    KeyHold hold(owner);
    hold.host = keys.host();
    int frames = 0, ends = 0;
    hold.onFrame = [&] { ++frames; };
    hold.onEnd = [&] { ++ends; };
    keys.down = {juce::KeyPress::rightKey};

    hold.begin(juce::KeyPress::rightKey, kConfig, 0.5);
    ASSERT_TRUE(hold.active());
    EXPECT_NEAR(hold.distance(), 0.5, 1e-12) << "the tap the press already made";
    keys.now += 100.0 + 500.0;
    hold.tick();
    EXPECT_EQ(frames, 1);
    EXPECT_NEAR(hold.distance(), 0.5 + expectedDistance(0.5), 1e-9);

    keys.shift = true;
    hold.speedFactor = [&](synth::ui::KeyAxis) { return keys.shift ? 2.0 : 1.0; };
    keys.now += 500.0;
    hold.tick();
    EXPECT_NEAR(hold.distance(), 0.5 + expectedDistance(0.5) + 2.0 * (expectedDistance(1.0) - expectedDistance(0.5)),
                1e-9);

    keys.down.clear();
    hold.checkKeyUp();
    EXPECT_FALSE(hold.active());
    EXPECT_EQ(ends, 1);
    hold.end();
    EXPECT_EQ(ends, 1) << "ending twice is one end";
}

TEST(KeyHoldTest, AFrameWhoseKeyIsNoLongerDownEndsTheHoldOneTurnLater) {
    juce::Component owner;
    FakeKeys keys;
    KeyHold hold(owner);
    hold.host = keys.host();
    int ends = 0;
    hold.onEnd = [&] { ++ends; };
    keys.down = {juce::KeyPress::upKey};
    hold.begin(juce::KeyPress::upKey, kConfig, 0.0);
    keys.down.clear();
    hold.tick();
    EXPECT_TRUE(hold.active()) << "the pump is not stopped from inside its own frame";
    hold.end();
    EXPECT_EQ(ends, 1);
}
