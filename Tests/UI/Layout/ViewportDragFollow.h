#pragma once

// ViewportDragFollow.h -- the one checklist behind every "a drag inside a scrolling list follows the view" test
// (DragSurfaceRegistryTests.cpp, kFollow, names them; docs/layout/layout.md, "Every drag follows the view"). A real
// mouse-down and real drag events go through the surface's own handlers. Off screen JUCE sends no drag auto-repeat,
// so a "tick" is one more drag event with the pointer where it already is, which is what the auto-repeat sends while
// the pointer rests. Header-only; not registered in Tests/CMakeLists.txt.

#include <cmath>
#include <functional>
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace viewport_drag_follow {

/** One fresh surface, ready to be pressed. Positions are along the axis the list scrolls, in the viewport's own
 *  coordinates (0 = the top or left edge of what is visible); the surface turns them into its own events, rebuilt for
 *  every call because the pressed row moves under the pointer and the list scrolls under both. */
struct Surface {
    std::shared_ptr<void> keepAlive;        ///< whatever owns the components below
    std::function<int()> extent;            ///< the visible length of the view
    std::function<int()> viewPosition;      ///< the view's scroll position along the axis
    std::function<void(float)> press;       ///< mouse-down on the thing that is dragged
    std::function<void(float)> drag;        ///< one drag event
    std::function<void(float)> release;     ///< mouse-up
    std::function<bool()> escape;           ///< Esc, as the key reaches the gesture
    std::function<float()> draggedPosition; ///< the dragged thing's leading edge, viewport coordinates
    std::function<int()> commits;           ///< undo steps (or commit calls) so far
    float pressAt = 0.0f;                   ///< where the press lands, viewport coordinates
};

using Make = std::function<Surface()>;

/** Presses, then drags far enough that the gesture lifts, still clear of the edge zones. */
inline void lift(Surface& s) {
    s.press(s.pressAt);
    s.drag(s.pressAt + 6.0f);
    s.drag(s.pressAt + 12.0f);
}

/** The dragged thing is where the user can see it: its position is inside the view, whatever the pointer does. */
inline void expectInside(const Surface& s, const char* when) {
    const float extent = (float)s.extent();
    EXPECT_GE(s.draggedPosition(), 0.0f) << when << ": the dragged thing is before the start of the view";
    EXPECT_LE(s.draggedPosition(), extent) << when << ": the dragged thing is past the end of the view";
}

/** The whole checklist: a pointer held at the far edge scrolls on every tick with the dragged thing staying under it,
 *  deeper into the zone is faster, the near edge scrolls back, moving back in stops it, and the drop is one step. */
inline void expectHeldEdgeFollows(const Make& make) {
    auto s = make();
    const auto extent = (float)s.extent();
    ASSERT_GT(extent, 100.0f) << "the rig's view is too short to have a dead middle";
    lift(s);
    const int start = s.viewPosition();
    const float edge = extent - 2.0f;
    s.drag(edge);
    const float underPointer = s.draggedPosition();
    for (int tick = 0; tick < 6; ++tick) {
        const int before = s.viewPosition();
        s.drag(edge);
        EXPECT_GT(s.viewPosition(), before) << "tick " << tick << ": a pointer held at the edge scrolls the view";
        EXPECT_NEAR(s.draggedPosition(), underPointer, 1.0f)
            << "tick " << tick << ": the dragged thing left the pointer";
        expectInside(s, "a held tick");
    }
    EXPECT_GT(s.viewPosition(), start);

    // Deeper is faster: the zone's first pixels crawl, the edge itself runs at the full step.
    const int beforeShallow = s.viewPosition();
    s.drag(extent - (float)std::min(32, (int)extent / 3) + 4.0f);
    const int shallow = s.viewPosition() - beforeShallow;
    const int beforeDeep = s.viewPosition();
    s.drag(extent - 1.0f);
    const int deep = s.viewPosition() - beforeDeep;
    EXPECT_GT(shallow, 0) << "inside the zone the view moves";
    EXPECT_GT(deep, shallow) << "nearer the edge is faster";

    // The near edge scrolls back.
    const int beforeUp = s.viewPosition();
    s.drag(1.0f);
    EXPECT_LT(s.viewPosition(), beforeUp) << "a pointer at the near edge scrolls back";

    // Back in the middle nothing scrolls, however long it rests.
    s.drag(extent * 0.5f);
    const int resting = s.viewPosition();
    for (int tick = 0; tick < 4; ++tick)
        s.drag(extent * 0.5f);
    EXPECT_EQ(s.viewPosition(), resting) << "moving back in stops the scroll";

    const int commitsBefore = s.commits();
    s.release(extent * 0.5f);
    EXPECT_EQ(s.commits() - commitsBefore, 1) << "the whole drag, scroll included, is one step";
    // Not asserted after the release: the list surfaces rebuild their rows on the drop, so draggedPosition then names
    // whatever row now sits at the pressed index, not the dropped one.
}

/** The release ends the scroll with the gesture (no timer runs on, and the button's up stops the drag events), and
 *  Esc ends it at once: the auto-repeat's next events, which keep coming while the button is held, scroll nothing,
 *  and the release after Esc commits nothing. */
inline void expectReleaseAndEscapeStopIt(const Make& make) {
    {
        auto s = make();
        lift(s);
        const float edge = (float)s.extent() - 2.0f;
        s.drag(edge);
        s.drag(edge);
        const int held = s.viewPosition();
        s.release(edge);
        EXPECT_EQ(s.viewPosition(), held) << "the release scrolls nothing more";
    }
    {
        auto s = make();
        lift(s);
        const float edge = (float)s.extent() - 2.0f;
        s.drag(edge);
        s.drag(edge);
        const int commitsBefore = s.commits();
        EXPECT_TRUE(s.escape()) << "Esc reaches the drag";
        const int cancelled = s.viewPosition();
        for (int tick = 0; tick < 4; ++tick)
            s.drag(edge);
        EXPECT_EQ(s.viewPosition(), cancelled) << "Esc stops the scroll";
        s.release(edge);
        EXPECT_EQ(s.commits(), commitsBefore) << "after Esc the release commits nothing";
    }
}

/** Both checklists, the one a follow test runs. */
inline void expectFollows(const Make& make) {
    expectHeldEdgeFollows(make);
    expectReleaseAndEscapeStopIt(make);
}

} // namespace viewport_drag_follow
