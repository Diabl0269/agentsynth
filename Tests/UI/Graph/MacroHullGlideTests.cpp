// MacroHullGlide: what a gliding macro border draws at the start, middle and end of its glide.

#include "UI/Graph/MacroHullGlide/MacroHullGlide.h"
#include <gtest/gtest.h>

TEST(MacroHullGlide, StartsWhereTheBorderWasAndSettlesOnItsLiveBounds) {
    MacroHullGlide glide;
    const juce::Rectangle<int> was(0, 0, 100, 100), now(0, 0, 200, 100);
    ASSERT_TRUE(glide.arm({{"m", was}}, {{"m", now}}));
    EXPECT_EQ(glide.apply("m", now), was);
    glide.applyTweenAt(0.5f);
    EXPECT_EQ(glide.apply("m", now), juce::Rectangle<int>(0, 0, 150, 100));
    glide.finish();
    EXPECT_FALSE(glide.isLive());
    EXPECT_EQ(glide.apply("m", now), now);
}

TEST(MacroHullGlide, AnUnchangedOrNewMacroIsNotArmed) {
    MacroHullGlide glide;
    const juce::Rectangle<int> r(0, 0, 100, 100);
    EXPECT_FALSE(glide.arm({{"same", r}}, {{"same", r}, {"new", r}}));
    EXPECT_EQ(glide.apply("new", r.translated(5, 5)), r.translated(5, 5));
}

// A Poly toggle that leaves the border where it was must not move it in any frame: nothing arms, so every tween step
// draws the live border untouched.
TEST(MacroHullGlide, NoFrameMovesABorderWhoseBeforeEqualsItsAfter) {
    MacroHullGlide glide;
    const juce::Rectangle<int> r(40, 24, 960, 480);
    EXPECT_FALSE(glide.arm({{"m", r}}, {{"m", r}}));
    for (const float t : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        glide.applyTweenAt(t);
        EXPECT_EQ(glide.apply("m", r), r) << "t=" << t;
    }
    EXPECT_FALSE(glide.isLive());
}

TEST(MacroHullGlide, FollowsALiveBorderThatKeepsMovingDuringTheGlide) {
    MacroHullGlide glide;
    const juce::Rectangle<int> was(0, 0, 100, 100), settled(0, 0, 200, 100);
    ASSERT_TRUE(glide.arm({{"m", was}}, {{"m", settled}}));
    glide.applyTweenAt(1.0f);
    // The member keeps moving: the drawn border lands on the live one, not the one captured at arm time.
    EXPECT_EQ(glide.apply("m", settled.withWidth(260)), settled.withWidth(260));
}
