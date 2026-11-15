#pragma once

// CanvasEdgeScroller.h
//
// Pans the patch canvas (or the controller surface) while a drag is held at the edge of the view: a card moved on
// the canvas, a cable, a selection box, or one dragged in from the module library or sound browser. It runs one frame
// per display refresh on the shared EdgeScrollPump, only for the length of a drag; the speed comes from
// canvasEdgePanTarget (CanvasEdgeScroll.h), eased in and out and stepped in whole device pixels. The owner supplies
// the pointer, the view and the pan through Hooks. After each pan it calls the drag's own `onScrolled`, so the dragged
// card or landing ghost is re-placed under the (stationary) pointer. See docs/layout/layout.md "Edge scroll while
// dragging".

#include "UI/Timeline/EdgeAutoScroll.h"

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>

namespace synth::ui {
class EdgeScrollPump;
}

class CanvasEdgeScroller {
public:
    struct Hooks {
        std::function<juce::Rectangle<int>()> view;    // the visible canvas, in the same space as pointer()
        std::function<juce::Point<float>()> pointer;   // the pointer now, in that space
        std::function<bool(juce::Point<float>)> panBy; // pans by this screen delta; true when the view moved
        std::function<bool()> dragStillDown;           // false once the gesture is over (a safety net)
    };

    /** Frames come from `host`'s display (the view that pans); `host` must outlive this. */
    CanvasEdgeScroller(juce::Component& host, Hooks hooks);
    ~CanvasEdgeScroller();

    /** Arms the scroller for one drag; `onScrolled` runs after every frame that moved the view, `onStill` after a
     *  frame where the pointer is out of the zones or the view could not move. `press` is where the drag was pressed
     *  (the pointer's space): a press inside an edge zone pans only once the pointer moves toward that edge
     *  (EdgeIntent). None (a drag that came in from outside) pans at once. Replaces any drag already armed. */
    void begin(std::function<void()> onScrolled, std::function<void()> onStill = nullptr,
               std::optional<juce::Point<int>> press = std::nullopt);
    /** Disarms it (mouse up, drop, cancel). Safe to call when nothing is armed. */
    void end();
    bool isActive() const noexcept;

    /** Test seam: one frame of the pump's test length, as the display would run it. */
    void tick();

private:
    void frame();

    Hooks hooks_;
    std::unique_ptr<synth::ui::EdgeScrollPump> pump_;
    synth::ui::EdgeIntent intent_;
    std::function<void()> onScrolled_;
    std::function<void()> onStill_;
};
