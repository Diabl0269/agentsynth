// ViewportDragScroll.cpp -- the edge-follow of a drag inside a juce::Viewport (docs/layout/layout.md, "Every drag
// follows the view").
#include "ViewportDragScroll.h"

#include "UI/Layout/EdgeScrollPump.h"
#include "UI/Timeline/EdgeAutoScroll.h"
#include <algorithm>

namespace synth::ui::viewport_drag_scroll {

namespace {
constexpr int kAxisIndex = 0; // the pump's first axis carries the one scroll axis of a list
}

// A third at most, so a short frame (a mixer section a few rows tall) keeps a dead middle band and its two zones never
// meet: with overlapping zones a pointer near the bottom would also read as near the top.
int zoneFor(int extent, int zonePx) noexcept { return std::min(zonePx, extent / 3); }

// The event's own position is relative to the component it was sent to, which can be the very row that moves or the
// list that scrolls; the desktop point is the one thing that stays put while both do.
juce::Point<int> pointerOf(const juce::MouseEvent& e) { return e.getScreenPosition(); }

float along(const juce::Component& component, juce::Point<int> pointer, Axis axis) {
    const auto local = component.getLocalPoint(nullptr, pointer.toFloat());
    return axis == Axis::Vertical ? local.y : local.x;
}

ViewportDragFollower::ViewportDragFollower(juce::Component& host, int zonePx)
    : pump_(std::make_unique<EdgeScrollPump>(host))
    , intent_(std::make_unique<EdgeIntentAxis>())
    , zonePx_(zonePx) {
    pump_->onFrame = [this] { frame(); };
}

ViewportDragFollower::~ViewportDragFollower() = default;

// The nearest enclosing viewport is the one whose edge the pointer meets first; a list nested in an outer scroller
// (a mixer section inside the column) follows its own frame, never the outer one.
juce::Viewport* ViewportDragFollower::viewport() const {
    auto* content = content_.getComponent();
    return content != nullptr ? content->findParentComponentOfClass<juce::Viewport>() : nullptr;
}

// The intent is asked on every drag event (the pointer only moves then) and again every frame, which is harmless: the
// pointer is where the last event left it.
double ViewportDragFollower::target() {
    auto* vp = viewport();
    if (!following_ || vp == nullptr || vp->getViewedComponent() == nullptr)
        return 0.0;
    const bool vertical = axis_ == Axis::Vertical;
    const int extent = vertical ? vp->getMaximumVisibleHeight() : vp->getMaximumVisibleWidth();
    const int contentExtent = vertical ? vp->getViewedComponent()->getHeight() : vp->getViewedComponent()->getWidth();
    if (contentExtent <= extent)
        return 0.0;
    const auto local = vp->getLocalPoint(nullptr, pointer_);
    return edgeScrollVelocity(vertical ? local.y : local.x, 0, extent, zoneFor(extent, zonePx_), 1.0, *intent_);
}

void ViewportDragFollower::track(juce::Component& content, juce::Point<int> pointer, Axis axis, bool dragging,
                                 std::optional<juce::Point<int>> press) {
    content_ = &content;
    axis_ = axis;
    pointer_ = pointer;
    following_ = dragging;
    if (!inGesture_) {
        inGesture_ = true;
        if (auto* vp = viewport()) {
            const auto at = vp->getLocalPoint(nullptr, press.value_or(pointer));
            intent_->press(axis == Axis::Vertical ? at.y : at.x);
        }
    }
    if (!dragging) {
        pump_->stop();
        return;
    }
    if (target() != 0.0 && !pump_->isRunning())
        pump_->start();
}

void ViewportDragFollower::lifted() {
    if (!inGesture_ || content_.getComponent() == nullptr)
        return;
    following_ = true;
    if (target() != 0.0 && !pump_->isRunning())
        pump_->start();
}

float ViewportDragFollower::locate() const {
    auto* content = content_.getComponent();
    return content != nullptr ? along(*content, pointer_, axis_) : 0.0f;
}

void ViewportDragFollower::stop() {
    pump_->stop();
    following_ = false;
    inGesture_ = false;
    content_ = nullptr;
}

bool ViewportDragFollower::isRunning() const noexcept { return pump_->isRunning(); }

// Leaving the zone does not stop the frames: the speed eases to rest and the pump stops itself then. The view moves
// in whole pixels (the viewport's own unit); a scroll that goes nowhere (the content's end) drops the speed.
void ViewportDragFollower::frame() {
    const double px = pump_->step(kAxisIndex, target(), kEdgeAutoScrollMaxPxPerSec, 1.0);
    if (pump_->settled()) {
        pump_->stop();
        return;
    }
    auto* vp = viewport();
    if (px == 0.0 || vp == nullptr || vp->getViewedComponent() == nullptr)
        return;
    const bool vertical = axis_ == Axis::Vertical;
    const auto before = vp->getViewPosition();
    if (scrollBy) {
        scrollBy((int)px);
    } else {
        const auto* content = vp->getViewedComponent();
        auto next = before;
        if (vertical)
            next.y =
                std::clamp(before.y + (int)px, 0, std::max(0, content->getHeight() - vp->getMaximumVisibleHeight()));
        else
            next.x = std::clamp(before.x + (int)px, 0, std::max(0, content->getWidth() - vp->getMaximumVisibleWidth()));
        if (next != before)
            vp->setViewPosition(next);
    }
    if (vp->getViewPosition() == before) {
        pump_->halt(kAxisIndex);
        return;
    }
    if (onScrolled)
        onScrolled();
}

void ViewportDragFollower::frameForTest() { pump_->frameForTest(); }

} // namespace synth::ui::viewport_drag_scroll
