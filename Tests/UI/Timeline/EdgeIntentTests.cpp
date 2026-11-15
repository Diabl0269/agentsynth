// EdgeIntentTests.cpp -- the edge intent rule (UI/Timeline/EdgeAutoScroll.h): a drag pressed inside an edge zone only
// scrolls toward that edge once the pointer has moved toward it, or has been outside that zone once.
#include "UI/Timeline/EdgeAutoScroll.h"

#include <gtest/gtest.h>

using synth::ui::EdgeIntent;
using synth::ui::EdgeIntentAxis;
using synth::ui::edgeScrollVelocity;
using synth::ui::kEdgeIntentPx;

namespace {
constexpr int kLo = 0, kHi = 400, kZone = 48;
} // namespace

// Regression test for FRO1774: a press inside the edge zone scrolled before the pointer moved.
TEST(EdgeIntent, APressInTheZoneHeldStillDoesNotScroll) {
    EdgeIntentAxis axis;
    axis.press(390);
    for (int frame = 0; frame < 10; ++frame)
        EXPECT_FALSE(axis.armed(390, kLo, kHi, kZone)) << "frame " << frame;
    EXPECT_EQ(edgeScrollVelocity(390, kLo, kHi, kZone, 22.0, axis), 0.0);
}

TEST(EdgeIntent, MovingTheIntentDistanceTowardTheEdgeScrolls) {
    EdgeIntentAxis axis;
    axis.press(380);
    EXPECT_FALSE(axis.armed(380 + kEdgeIntentPx - 1, kLo, kHi, kZone)) << "short of the dead distance";
    EXPECT_TRUE(axis.armed(380 + kEdgeIntentPx, kLo, kHi, kZone)) << "the dead distance toward the edge arms it";
    EXPECT_GT(edgeScrollVelocity(380 + kEdgeIntentPx, kLo, kHi, kZone, 22.0, axis), 0.0);
}

TEST(EdgeIntent, MovingAwayFromTheEdgeInsideTheZoneDoesNotScroll) {
    EdgeIntentAxis axis;
    axis.press(390);
    EXPECT_FALSE(axis.armed(380, kLo, kHi, kZone)) << "away from the edge, still inside its zone";
    EXPECT_FALSE(axis.armed(386, kLo, kHi, kZone)) << "back toward it, but not past the press";
    EXPECT_TRUE(axis.armed(394, kLo, kHi, kZone)) << "past the press by the dead distance";
    EXPECT_FALSE(axis.armed(390, kLo, kHi, kZone)) << "back at the press: stops";
    EXPECT_TRUE(axis.armed(396, kLo, kHi, kZone)) << "and toward the edge again: pans again";
}

TEST(EdgeIntent, LeavingTheZoneAndComingBackScrolls) {
    EdgeIntentAxis axis;
    axis.press(395);
    EXPECT_TRUE(axis.armed(200, kLo, kHi, kZone)) << "the dead middle has nothing to hold";
    EXPECT_TRUE(axis.armed(360, kLo, kHi, kZone)) << "back in the zone, short of the press: it was left once";
}

TEST(EdgeIntent, APressOutsideTheZoneScrollsAsSoonAsThePointerEnters) {
    EdgeIntentAxis axis;
    axis.press(200);
    EXPECT_TRUE(axis.armed(360, kLo, kHi, kZone));
    EdgeIntentAxis justShort;
    justShort.press(kHi - kZone); // on the zone's inner line, which is outside it
    EXPECT_TRUE(justShort.armed(kHi - kZone + 1, kLo, kHi, kZone));
}

TEST(EdgeIntent, EachEdgeCountsOnItsOwn) {
    EdgeIntentAxis axis;
    axis.press(395);
    EXPECT_TRUE(axis.armed(10, kLo, kHi, kZone)) << "the far edge was never pressed in: it scrolls at once";
    EdgeIntentAxis low;
    low.press(5);
    EXPECT_FALSE(low.armed(5, kLo, kHi, kZone));
    EXPECT_TRUE(low.armed(5 - kEdgeIntentPx, kLo, kHi, kZone)) << "toward the low edge is a smaller position";
}

TEST(EdgeIntent, EachAxisCountsOnItsOwn) {
    EdgeIntent intent;
    intent.press(395, 200); // near the right edge, in the vertical middle
    EXPECT_FALSE(intent.x.armed(395, kLo, kHi, kZone)) << "held still: no sideways pan";
    EXPECT_TRUE(intent.y.armed(10, kLo, kHi, kZone)) << "moved to the top: the vertical pan starts at once";
    EXPECT_FALSE(intent.x.armed(395, kLo, kHi, kZone)) << "still no sideways pan";
}

TEST(EdgeIntent, ADragFromOutsideTheViewIsArmed) {
    EdgeIntent intent;
    intent.press(395, 395);
    intent.arm();
    EXPECT_TRUE(intent.x.armed(398, kLo, kHi, kZone));
    EXPECT_TRUE(intent.y.armed(398, kLo, kHi, kZone));
    EdgeIntentAxis neverPressed;
    EXPECT_TRUE(neverPressed.armed(398, kLo, kHi, kZone));
}
