#include "UI/Graph/CanvasEdgeScroll/CanvasEdgeScroller.h"

#include "UI/Graph/CanvasEdgeScroll/CanvasEdgeScroll.h"

void CanvasEdgeScroller::begin(std::function<void()> onScrolled, std::function<void()> onStill) {
    onScrolled_ = std::move(onScrolled);
    onStill_ = std::move(onStill);
    startTimerHz(synth::ui::kEdgeScrollHz);
}

void CanvasEdgeScroller::end() {
    stopTimer();
    onScrolled_ = nullptr;
    onStill_ = nullptr;
}

// Every tick re-reads the pointer, so moving back inward (or off the zone) stops the pan on the next tick and a
// release stops the timer itself. `onScrolled_` is moved out while it runs: it may end() the scroller (a rebuild that
// cancels the drag), which must not destroy the callable that is executing.
void CanvasEdgeScroller::tick() {
    if (!isActive())
        return;
    if (hooks_.dragStillDown && !hooks_.dragStillDown()) {
        end();
        return;
    }
    const auto view = hooks_.view();
    const auto pointer = hooks_.pointer().roundToInt();
    const auto delta = synth::ui::canvasEdgePanDelta(pointer, view);
    if (delta.isOrigin() || !hooks_.panBy(delta)) {
        if (auto cb = std::move(onStill_)) { // moved out for the same reason as onScrolled_ below
            cb();
            if (isActive() && !onStill_)
                onStill_ = std::move(cb);
        }
        return;
    }
    if (auto cb = std::move(onScrolled_)) {
        cb();
        if (isActive() && !onScrolled_)
            onScrolled_ = std::move(cb);
    }
}
