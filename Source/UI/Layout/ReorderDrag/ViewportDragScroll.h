#pragma once

// ViewportDragScroll.h: how a drag inside a juce::Viewport makes the view follow it. A pointer held in the edge zone at
// either end of the viewport's visible span scrolls the view, faster the nearer the edge, on the shared display-rate
// pump (UI/Layout/EdgeScrollPump.h) and the shared speed curve and intent rule (UI/Timeline/EdgeAutoScroll.h): a press
// that lands inside a zone scrolls only once the pointer moves toward that edge. The pointer rests while the view
// moves, so after each frame the owner re-aims its drag at the still pointer (onScrolled). Message thread only.
//
// Owner flow (a ReorderDragSession does all of it for a list; see its follow()):
//   every drag event, past the threshold or not:
//     follower.track(content, pointerOf(e), axis, dragging, e.getMouseDownScreenPosition());
//     dragTo(follower.locate());
//   onScrolled (a frame moved the view):  dragTo(follower.locate()); place the rows
//   release, Esc, rebuild:                follower.stop();
#include "UI/Timeline/EdgeAutoScroll.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>

namespace synth::ui {
class EdgeScrollPump;
}

namespace synth::ui::viewport_drag_scroll {

enum class Axis { Vertical, Horizontal };

/** Width (px) of the strip at each end of the view that starts a scroll. */
constexpr int kZonePx = 32;

/** The zone used for a view `extent` px long: `zonePx`, but never more than a third of the view. */
int zoneFor(int extent, int zonePx = kZonePx) noexcept;

/** Where `e`'s pointer is in desktop coordinates. Read it before anything scrolls. */
juce::Point<int> pointerOf(const juce::MouseEvent& e);

/** `pointer` (desktop coordinates) along `axis` in `component`'s own coordinates. */
float along(const juce::Component& component, juce::Point<int> pointer, Axis axis);

/** The follow of one drag: runs one frame per display refresh while the pointer sits in an edge zone of the viewport
 *  `content` scrolls in, eases the speed in and out, and scrolls the view by whole pixels (kEdgeAutoScrollMaxPxPerSec
 *  at the edge). A viewport whose content fits along the axis never scrolls. */
class ViewportDragFollower {
public:
    /** Frames come from `host`'s display; `host` must outlive this. */
    explicit ViewportDragFollower(juce::Component& host, int zonePx = kZonePx);
    ~ViewportDragFollower();

    /** A frame moved the view: re-aim the drag at the still pointer (locate()) and place the rows. */
    std::function<void()> onScrolled;
    /** Replaces how a frame scrolls the view, for an owner that keeps other views in step (the timeline's track rows).
     *  Receives the signed step in px; must move the viewport itself. Left empty, the viewport is scrolled directly. */
    std::function<void(int step)> scrollBy;

    /** Every drag event of the gesture, `dragging` once it is past its threshold (never after Esc or the release).
     *  `press` is where the button went down in desktop coordinates; without it the first pointer after stop() counts
     *  as the press. Frames run while `dragging` and the armed pointer is in a zone, and stop on their own once the
     *  speed has eased to rest. */
    void track(juce::Component& content, juce::Point<int> pointer, Axis axis, bool dragging,
               std::optional<juce::Point<int>> press = std::nullopt);
    /** The drag has just been lifted (its first track() came while it was still short of its threshold): from now on
     *  the pointer counts, so a pointer that came to rest in a zone scrolls without another event. */
    void lifted();
    /** The last pointer along the tracked axis in the tracked content's coordinates (0 when nothing is tracked). */
    float locate() const;
    /** Ends the gesture at once: no more frames, the next track() starts a new drag. Call it on release, Esc, cancel.
     */
    void stop();
    bool isRunning() const noexcept;

    /** Test seam: one frame of the default test length, as the display would run it. */
    void frameForTest();

private:
    void frame();
    double target();
    juce::Viewport* viewport() const;

    std::unique_ptr<EdgeScrollPump> pump_;
    EdgeIntentAxis intent_;
    juce::Component::SafePointer<juce::Component> content_;
    Axis axis_ = Axis::Vertical;
    juce::Point<int> pointer_;
    int zonePx_;
    bool following_ = false;
    bool inGesture_ = false;
};

} // namespace synth::ui::viewport_drag_scroll
