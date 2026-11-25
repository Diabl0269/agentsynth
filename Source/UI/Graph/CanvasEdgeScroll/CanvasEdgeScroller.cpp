#include "UI/Graph/CanvasEdgeScroll/CanvasEdgeScroller.h"

#include "UI/Graph/CanvasEdgeScroll/CanvasEdgeScroll.h"
#include "UI/Layout/EdgeScrollPump.h"

namespace {
constexpr int kAxisX = 0;
constexpr int kAxisY = 1;
} // namespace

CanvasEdgeScroller::CanvasEdgeScroller(juce::Component& host, Hooks hooks)
    : hooks_(std::move(hooks))
    , pump_(std::make_unique<synth::ui::EdgeScrollPump>(host)) {
    pump_->onFrame = [this] { frame(); };
}

CanvasEdgeScroller::~CanvasEdgeScroller() = default;

void CanvasEdgeScroller::begin(std::function<void()> onScrolled, std::function<void()> onStill,
                               std::optional<juce::Point<int>> press) {
    if (press)
        intent_.press(press->x, press->y);
    else
        intent_.arm();
    onScrolled_ = std::move(onScrolled);
    onStill_ = std::move(onStill);
    pump_->stop(); // a new drag starts from rest
    pump_->start();
}

void CanvasEdgeScroller::end() {
    pump_->stop(); // first: the aim hook reads isActive() to tell a finished drag from a resting pointer
    if (hooks_.onAim)
        hooks_.onAim({});
    onScrolled_ = nullptr;
    onStill_ = nullptr;
}

bool CanvasEdgeScroller::isActive() const noexcept { return pump_->isRunning(); }

void CanvasEdgeScroller::tick() { pump_->frameForTest(); }

// The frames run for the whole drag (the pointer is polled, not pushed), so moving back inward (or off the zone)
// eases the pan to rest and a release stops the frames themselves. The step is whole device pixels, so every frame
// draws the canvas at a pixel the screen can show; the fraction carries to the next frame. `onScrolled_` is moved out
// while it runs: it may end() the scroller (a rebuild that cancels the drag), which must not destroy the callable that
// is executing.
void CanvasEdgeScroller::frame() {
    if (hooks_.dragStillDown && !hooks_.dragStillDown()) {
        end();
        return;
    }
    const auto view = hooks_.view();
    const auto pointer = hooks_.pointer().roundToInt();
    const auto target = synth::ui::canvasEdgePanTarget(pointer, view, intent_);
    if (hooks_.onAim)
        hooks_.onAim(target);
    const double quantum = pump_->devicePixel();
    const auto max = (double)synth::ui::kCanvasEdgeMaxPxPerSec;
    const juce::Point<float> delta((float)pump_->step(kAxisX, target.x, max, quantum),
                                   (float)pump_->step(kAxisY, target.y, max, quantum));
    const bool moved = !delta.isOrigin() && hooks_.panBy(delta);
    if (!moved && !delta.isOrigin()) {
        pump_->halt(kAxisX); // clamped at the canvas's end: a later move starts from rest
        pump_->halt(kAxisY);
    }
    if (!moved) {
        if (!target.isOrigin() && delta.isOrigin() && !pump_->settled())
            return;                          // easing in below a device pixel: not still, just not moved yet
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
