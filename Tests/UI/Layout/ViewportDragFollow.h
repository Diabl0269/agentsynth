#pragma once

// ViewportDragFollow.h -- the one checklist behind every "a drag inside a scrolling list follows the view" test
// (DragSurfaceRegistryTests.cpp, kFollow, names them; docs/layout/layout.md, "Every drag follows the view"). A real
// mouse-down and real drag events go through the surface's own handlers. The list scrolls on the shared display-rate
// pump (EdgeScrollPump), which off screen never runs: the surface's `frame` is one display frame, taken through the
// owner's test seam, so `drag(x)` means "one drag event, then one frame" and a held pointer is the same position
// dragged again. The speed eases (EdgeScrollPump::kRampSeconds), so a checklist lets a few frames pass before it
// reads a speed. Header-only; not registered in Tests/CMakeLists.txt.

#include <algorithm>
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
    std::function<void(float)> dragEvent;   ///< one drag event, no frame
    std::function<void()> frame;            ///< one display frame of the follow; empty when the surface has no seam
    std::function<void(float)> drag;        ///< one drag event, then one frame
    std::function<void(float)> release;     ///< mouse-up
    std::function<bool()> escape;           ///< Esc, as the key reaches the gesture
    std::function<float()> draggedPosition; ///< the dragged thing's leading edge, viewport coordinates
    std::function<int()> commits;           ///< undo steps (or commit calls) so far
    float pressAt = 0.0f;                   ///< where the press lands, viewport coordinates
};

using Make = std::function<Surface()>;

/** The edge-intent half of every follow test (EdgeIntent, UI/Timeline/EdgeAutoScroll.h): a drag pressed inside an edge
 *  zone and held still scrolls nothing however long it rests; moved a few pixels toward that edge, it scrolls. Every
 *  follow test named in DragSurfaceRegistryTests.cpp calls this (or expectFollows, which does), and the registry fails
 * a follow test that does not. */
struct PressInZone {
    std::function<void()> press; ///< press with the pointer inside the far edge's zone (and one drag event there)
    std::function<void()> tick;  ///< one drag event and one frame with the pointer where it is
    std::function<void()> moveTowardEdge; ///< the pointer moved a few pixels toward that edge; null when it cannot be
    std::function<double()> viewPosition; ///< the view's scroll position along the axis
    std::function<void()> release;        ///< mouse-up; may be null
};

inline void expectPressInZoneHoldsStill(const PressInZone& r, int ticks = 10) {
    r.press();
    const double start = r.viewPosition();
    for (int tick = 0; tick < ticks; ++tick) {
        r.tick();
        EXPECT_EQ(r.viewPosition(), start) << "tick " << tick << ": a press in the edge zone held still scrolled";
    }
    if (r.moveTowardEdge) {
        r.moveTowardEdge();
        for (int tick = 0; tick < ticks; ++tick)
            r.tick();
        EXPECT_NE(r.viewPosition(), start) << "moving toward the edge from a press in its zone scrolls";
    }
    if (r.release)
        r.release();
}

/** The real-mouse half of every follow test: a real pointer leaves the window. JUCE keeps sending the drag
 *  to the pressed component with positions far outside the view (past the window's edge, or stopped at the screen's
 *  edge), and a quick flick can send the first drag event already out there. The synthetic drags of test mode and of
 *  the older checklists never went past the edge, so a view that stopped panning, or a dragged thing that went off
 *  with the pointer, passed them. Every follow test named in DragSurfaceRegistryTests.cpp calls this (or
 *  expectFollows, which does), and the registry fails a follow test that does not. */
struct PastWindow {
    std::function<void()> press;             ///< press clear of the edge zones (no drag event yet)
    std::function<void(float)> dragPastEdge; ///< one drag event `px` past the far edge (the pointer outside the view)
    std::function<void()> tick;              ///< one frame with the pointer where it is (or the same event again)
    std::function<double()> viewPosition;    ///< the view's scroll position along the axis (any sign convention)
    std::function<bool()> draggedThingIsInside; ///< the dragged thing is drawn inside the visible span; null: nothing
                                                ///< is drawn at the pointer (a box that grows, a stroke that paints)
    std::function<void()> release;              ///< mouse-up where the pointer is; may be null
};

inline void expectPastWindowFollows(const PastWindow& r, int ticks = 12) {
    // Far past the edge, and a flick: the first drag event lands there at once.
    for (const float past : {40.0f, 400.0f}) {
        SCOPED_TRACE(past);
        r.press();
        r.dragPastEdge(past);
        const double start = r.viewPosition();
        double last = start;
        int moving = 0;
        for (int tick = 0; tick < ticks; ++tick) {
            r.tick();
            const double now = r.viewPosition();
            moving += now != last ? 1 : 0;
            last = now;
            if (r.draggedThingIsInside)
                EXPECT_TRUE(r.draggedThingIsInside())
                    << "tick " << tick << ": the dragged thing went past the edge of the view with the pointer";
        }
        EXPECT_NE(last, start) << "a pointer dragged " << past << " px past the edge of the view scrolls it";
        EXPECT_GE(moving, ticks / 2) << "a pointer held past the edge keeps the view scrolling on every frame";
        if (r.release)
            r.release();
    }
}

/** `frames` more drag events, each with its frame, with the pointer held at `along`. */
inline void hold(Surface& s, float along, int frames) {
    for (int i = 0; i < frames; ++i)
        s.drag(along);
}

/** How far the view moves over three more frames with the pointer held at `along`, once the speed has eased to it. */
inline int travelWhileHeld(Surface& s, float along) {
    hold(s, along, 6);
    const int before = s.viewPosition();
    hold(s, along, 3);
    return std::abs(s.viewPosition() - before);
}

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

    // Deeper is faster: the zone's first pixels crawl, the edge itself runs at the full speed.
    // Each from the start of the view, so a short list does not run out under the faster one.
    const auto backToTop = [&] {
        hold(s, 1.0f, 80);
        hold(s, extent * 0.5f, 12);
    };
    backToTop();
    const int shallow = travelWhileHeld(s, extent - (float)std::min(32, (int)extent / 3) + 4.0f);
    backToTop();
    const int deep = travelWhileHeld(s, extent - 1.0f);
    EXPECT_GT(shallow, 0) << "inside the zone the view moves";
    EXPECT_GT(deep, shallow) << "nearer the edge is faster";

    // The near edge scrolls back (the speed eases through rest first, so it takes a few frames).
    const int beforeUp = s.viewPosition();
    hold(s, 1.0f, 12);
    EXPECT_LT(s.viewPosition(), beforeUp) << "a pointer at the near edge scrolls back";

    // Back in the middle the speed eases to rest, and then nothing scrolls, however long it rests.
    hold(s, extent * 0.5f, 12);
    const int resting = s.viewPosition();
    hold(s, extent * 0.5f, 4);
    EXPECT_EQ(s.viewPosition(), resting) << "moving back in stops the scroll";

    // Drop well below where it was picked up (the view has scrolled back toward its start by now): half the view or
    // more, so the drop is a real move for a row of any height.
    const float dropAt = std::min(s.pressAt + std::max(40.0f, extent * 0.5f), extent - 1.0f);
    hold(s, dropAt, 12);
    const int commitsBefore = s.commits();
    s.release(dropAt);
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
        hold(s, edge, 3);
        const int held = s.viewPosition();
        s.release(edge);
        if (s.frame)
            for (int tick = 0; tick < 4; ++tick)
                s.frame(); // the pump's frames after the release
        EXPECT_EQ(s.viewPosition(), held) << "the release scrolls nothing more";
    }
    {
        auto s = make();
        lift(s);
        const float edge = (float)s.extent() - 2.0f;
        hold(s, edge, 3);
        const int commitsBefore = s.commits();
        EXPECT_TRUE(s.escape()) << "Esc reaches the drag";
        const int cancelled = s.viewPosition();
        for (int tick = 0; tick < 4; ++tick)
            s.drag(edge);
        if (s.frame)
            s.frame();
        EXPECT_EQ(s.viewPosition(), cancelled) << "Esc stops the scroll";
        s.release(edge);
        EXPECT_EQ(s.commits(), commitsBefore) << "after Esc the release commits nothing";
    }
}

/** A row pressed inside the far edge zone and held there scrolls nothing: the list follows only once the drag is past
 *  its threshold, which a still pointer never is. */
inline void expectListPressInZoneHoldsStill(const Make& make) {
    auto s = make();
    const float inZone = (float)s.extent() - 4.0f;
    float at = inZone; // where the pointer rests
    // Still lifting the row (the drag is past its threshold) but only moving a few pixels toward the far edge.
    expectPressInZoneHoldsStill({[&] {
                                     s.press(inZone);
                                     s.drag(inZone);
                                 },
                                 [&] { s.drag(at); },
                                 [&] {
                                     at = inZone + 6.0f;
                                     s.drag(at);
                                 },
                                 [&] { return (double)s.viewPosition(); }, [&] { s.release(inZone); }});
}

/** A row dragged past the end of the list's frame (out of the window, as a real mouse can) scrolls the list on every
 *  frame and the row stays at the visible end, never off with the pointer. */
inline void expectListPastWindowFollows(const Make& make) {
    std::shared_ptr<Surface> s;
    float at = 0.0f;
    expectPastWindowFollows({[&] {
                                 s = std::make_shared<Surface>(make());
                                 s->press(s->pressAt);
                             },
                             [&](float past) {
                                 at = (float)s->extent() + past;
                                 s->dragEvent(s->pressAt + 12.0f); // past the lift threshold, then the flick
                                 s->dragEvent(at);
                             },
                             [&] { s->drag(at); }, [&] { return (double)s->viewPosition(); },
                             [&] {
                                 const float p = s->draggedPosition();
                                 return p >= 0.0f && p <= (float)s->extent();
                             },
                             [&] { s->release(at); }});
}

/** Every checklist, the one a follow test runs. */
inline void expectFollows(const Make& make) {
    expectHeldEdgeFollows(make);
    expectListPastWindowFollows(make);
    expectReleaseAndEscapeStopIt(make);
    expectListPressInZoneHoldsStill(make);
}

} // namespace viewport_drag_follow
