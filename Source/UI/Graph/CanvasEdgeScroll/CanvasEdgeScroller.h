#pragma once

// CanvasEdgeScroller.h
//
// The gated 30 Hz timer that pans the patch canvas while a card is dragged to the edge of the view (a card moved on
// the canvas, or one dragged in from the module library or sound browser). It runs only for the length of a drag.
// The speed comes from CanvasEdgePanDelta (CanvasEdgeScroll.h); GraphEditor supplies the pointer, the view and the
// pan through Hooks. After each pan it calls the drag's own `onScrolled`, so the dragged card or landing ghost is
// re-placed under the (stationary) pointer. See docs/layout/layout.md "Edge scroll while dragging".

#include <functional>
#include <juce_events/juce_events.h>
#include <juce_graphics/juce_graphics.h>

class CanvasEdgeScroller : private juce::Timer {
public:
    struct Hooks {
        std::function<juce::Rectangle<int>()> view;    // the visible canvas, in the same space as pointer()
        std::function<juce::Point<float>()> pointer;   // the pointer now, in that space
        std::function<bool(juce::Point<float>)> panBy; // pans by this screen delta; true when the view moved
        std::function<bool()> dragStillDown;           // false once the gesture is over (a safety net)
    };

    explicit CanvasEdgeScroller(Hooks hooks)
        : hooks_(std::move(hooks)) {}
    ~CanvasEdgeScroller() override { stopTimer(); }

    /** Arms the scroller for one drag; `onScrolled` runs after every tick that moved the view. Replaces any drag
     *  already armed. */
    void begin(std::function<void()> onScrolled);
    /** Disarms it (mouse up, drop, cancel). Safe to call when nothing is armed. */
    void end();
    bool isActive() const noexcept { return isTimerRunning(); }

    /** One tick; the timer calls it, and tests call it directly. */
    void tick();

private:
    void timerCallback() override { tick(); }

    Hooks hooks_;
    std::function<void()> onScrolled_;
};
