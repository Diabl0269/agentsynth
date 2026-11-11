// ViewportDragScroll.cpp -- the edge-follow step of a drag inside a juce::Viewport (docs/layout/layout.md, "Every drag
// follows the view").
#include "ViewportDragScroll.h"

#include "UI/Timeline/EdgeAutoScroll.h"
#include <algorithm>
#include <cmath>

namespace synth::ui::viewport_drag_scroll {

// A third at most, so a short frame (a mixer section a few rows tall) keeps a dead middle band and its two zones never
// meet: with overlapping zones a pointer near the bottom would also read as near the top.
int zoneFor(int extent, int zonePx) noexcept { return std::min(zonePx, extent / 3); }

// Linear in how deep into the zone the pointer is, held at the full step past the edge (the pointer dragged right off
// the list). Rounding never lands on 0 inside a zone, so the shallowest depth still crawls.
int stepFor(int pos, int extent, int zonePx) noexcept {
    const double velocity = edgeScrollVelocity(pos, 0, extent, zoneFor(extent, zonePx), kMaxStepPx);
    if (velocity == 0.0)
        return 0;
    const int step = (int)std::lround(velocity);
    return step != 0 ? step : (velocity < 0.0 ? -1 : 1);
}

// The event's own position is relative to the component it was sent to, which can be the very row that moves or the
// list that scrolls; the desktop point is the one thing that stays put while both do.
juce::Point<int> pointerOf(const juce::MouseEvent& e) { return e.getScreenPosition(); }

int stepFor(const juce::Viewport& viewport, juce::Point<int> pointer, Axis axis, int zonePx) {
    const auto* content = viewport.getViewedComponent();
    if (content == nullptr)
        return 0;
    const auto local = viewport.getLocalPoint(nullptr, pointer);
    if (axis == Axis::Vertical) {
        const int extent = viewport.getMaximumVisibleHeight();
        return content->getHeight() > extent ? stepFor(local.y, extent, zonePx) : 0;
    }
    const int extent = viewport.getMaximumVisibleWidth();
    return content->getWidth() > extent ? stepFor(local.x, extent, zonePx) : 0;
}

int follow(juce::Viewport& viewport, juce::Point<int> pointer, Axis axis, int zonePx) {
    const int step = stepFor(viewport, pointer, axis, zonePx);
    if (step == 0)
        return 0;
    const auto* content = viewport.getViewedComponent();
    const auto before = viewport.getViewPosition();
    auto target = before;
    if (axis == Axis::Vertical) {
        const int maxY = std::max(0, content->getHeight() - viewport.getMaximumVisibleHeight());
        target.y = std::clamp(before.y + step, 0, maxY);
    } else {
        const int maxX = std::max(0, content->getWidth() - viewport.getMaximumVisibleWidth());
        target.x = std::clamp(before.x + step, 0, maxX);
    }
    if (target == before)
        return 0;
    viewport.setViewPosition(target);
    const auto after = viewport.getViewPosition();
    return axis == Axis::Vertical ? after.y - before.y : after.x - before.x;
}

float along(const juce::Component& component, juce::Point<int> pointer, Axis axis) {
    const auto local = component.getLocalPoint(nullptr, pointer.toFloat());
    return axis == Axis::Vertical ? local.y : local.x;
}

// The nearest enclosing viewport is the one whose edge the pointer meets first; a list nested in an outer scroller
// (a mixer section inside the column) follows its own frame, never the outer one.
float followAndLocate(juce::Component& content, juce::Point<int> pointer, Axis axis, bool dragging, int zonePx) {
    if (dragging)
        if (auto* viewport = content.findParentComponentOfClass<juce::Viewport>())
            follow(*viewport, pointer, axis, zonePx);
    return along(content, pointer, axis);
}

} // namespace synth::ui::viewport_drag_scroll
