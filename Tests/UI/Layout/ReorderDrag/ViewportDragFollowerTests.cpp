// ViewportDragFollowerTests.cpp: the edge follow every list reorder shares. A press inside an edge zone scrolls only
// once the pointer moves toward that edge, the speed eases on the display clock (a frame is the test seam's), and the
// gesture's end stops it.
#include "UI/Layout/ReorderDrag/ViewportDragScroll.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace {

using synth::ui::viewport_drag_scroll::Axis;
using synth::ui::viewport_drag_scroll::ViewportDragFollower;

constexpr int kViewHeight = 300;

// A 200 x 300 viewport over `contentHeight` of content, with a follower on the host.
struct FollowerRig {
    juce::Component host;
    juce::Viewport viewport;
    juce::Component content;
    ViewportDragFollower follower{host};
    int scrolled = 0;

    explicit FollowerRig(int contentHeight) {
        host.setSize(200, kViewHeight);
        host.addAndMakeVisible(viewport);
        viewport.setBounds(0, 0, 200, kViewHeight);
        content.setSize(200, contentHeight);
        viewport.setViewedComponent(&content, false);
        viewport.setScrollBarsShown(false, false);
        follower.onScrolled = [this] { ++scrolled; };
    }

    // The desktop point `y` px down the viewport.
    juce::Point<int> at(int y) { return viewport.localPointToGlobal(juce::Point<int>(10, y)); }
    void track(int y, std::optional<int> pressY = std::nullopt) {
        follower.track(content, at(y), Axis::Vertical, true,
                       pressY ? std::optional<juce::Point<int>>(at(*pressY)) : std::nullopt);
    }
    void frames(int count) {
        for (int i = 0; i < count; ++i)
            follower.frameForTest();
    }
    int viewY() const { return viewport.getViewPositionY(); }
};

} // namespace

TEST(ViewportDragFollower, APressInTheZoneHeldStillNeverScrollsAndAFewPixelsTowardTheEdgeDoes) {
    FollowerRig rig(2000);
    rig.track(kViewHeight - 4, kViewHeight - 4);
    rig.frames(60);
    EXPECT_EQ(rig.viewY(), 0) << "a press in the zone, held still, scrolls nothing however long it rests";
    EXPECT_EQ(rig.scrolled, 0);

    rig.track(kViewHeight - 4 + 2); // under the intent's threshold
    rig.frames(10);
    EXPECT_EQ(rig.viewY(), 0) << "two pixels is not yet a move toward the edge";

    rig.track(kViewHeight - 4 + 6);
    rig.frames(10);
    EXPECT_GT(rig.viewY(), 0) << "moving toward the edge scrolls";
    EXPECT_GT(rig.scrolled, 0) << "and every scroll frame tells the owner to re-aim";
}

TEST(ViewportDragFollower, ALiftedPointerHeldAtTheEdgeKeepsScrollingWithoutMoreEventsAndEasesIn) {
    FollowerRig rig(2000);
    rig.track(150, 150);
    rig.track(kViewHeight - 2);
    rig.frames(1);
    const int first = rig.viewY();
    EXPECT_GT(first, 0);
    rig.frames(8);
    const int later = rig.viewY() - first;
    EXPECT_GT(later, first * 4) << "the speed eased up from the first frame, with no event in between";
}

TEST(ViewportDragFollower, LeavingTheZoneOnceRearmsTheIntent) {
    FollowerRig rig(2000);
    rig.track(kViewHeight - 4, kViewHeight - 4);
    rig.frames(5);
    ASSERT_EQ(rig.viewY(), 0);
    rig.track(150); // out of the zone
    rig.frames(5);
    rig.track(kViewHeight - 4); // back in, without a press: the pointer got here by moving
    rig.frames(10);
    EXPECT_GT(rig.viewY(), 0) << "a pointer that came into the zone scrolls";
}

TEST(ViewportDragFollower, ContentThatFitsNeverScrolls) {
    FollowerRig rig(kViewHeight - 10);
    rig.track(150, 150);
    rig.track(kViewHeight - 2);
    rig.frames(20);
    EXPECT_EQ(rig.viewY(), 0);
    EXPECT_EQ(rig.scrolled, 0);
    EXPECT_FALSE(rig.follower.isRunning());
}

TEST(ViewportDragFollower, StopEndsTheFramesAtOnceAndTheNextGestureStartsFresh) {
    FollowerRig rig(2000);
    rig.track(150, 150);
    rig.track(kViewHeight - 2);
    rig.frames(4);
    ASSERT_GT(rig.viewY(), 0);
    ASSERT_TRUE(rig.follower.isRunning());
    rig.follower.stop();
    EXPECT_FALSE(rig.follower.isRunning());
    const int stopped = rig.viewY();
    rig.frames(10);
    EXPECT_EQ(rig.viewY(), stopped) << "no frame runs after the gesture ended";

    rig.track(kViewHeight - 4, kViewHeight - 4); // a new press in the zone
    rig.frames(10);
    EXPECT_EQ(rig.viewY(), stopped) << "the new gesture's press is in the zone: it holds still";
}

TEST(ViewportDragFollower, ScrollingAtTheContentEndHaltsAndTheFollowSettlesWhenThePointerLeaves) {
    FollowerRig rig(kViewHeight + 30);
    rig.track(150, 150);
    rig.track(kViewHeight - 2);
    rig.frames(30);
    EXPECT_EQ(rig.viewY(), 30) << "the view stops at the end of the content";
    const int scrolls = rig.scrolled;
    rig.frames(10);
    EXPECT_EQ(rig.scrolled, scrolls) << "a scroll that goes nowhere re-aims nothing";

    rig.track(150);
    rig.frames(10);
    EXPECT_FALSE(rig.follower.isRunning()) << "the pointer left the zone: the speed is at rest and the frames stop";
}
