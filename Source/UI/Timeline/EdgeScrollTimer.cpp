// EdgeScrollTimer: where the pointer sits against the edges of a drag's view, and the scroll one tick makes from it.
// The speeds come from edgeScrollVelocity, so every timeline drag scrolls at the clip lanes' pace.

#include "UI/Timeline/EdgeScrollTimer.h"

#include "UI/Timeline/EdgeAutoScroll.h"
#include "UI/Timeline/TimelineViewState.h"

namespace synth::ui {

void EdgeScrollTimer::track(bool following, juce::Point<int> pointer, juce::Rectangle<int> area, bool vertical) {
    pointer_ = pointer;
    area_ = area;
    vertical_ = vertical;
    const bool inZone = following && (horizontalPxPerTick() != 0.0 || verticalPerTick(1.0) != 0.0);
    if (inZone && !isTimerRunning())
        startTimerHz(kEdgeScrollHz);
    else if (!inZone && isTimerRunning())
        stopTimer();
}

double EdgeScrollTimer::horizontalPxPerTick() const noexcept {
    return edgeScrollVelocity(pointer_.x, area_.getX(), area_.getRight(), kEdgeZonePx, kEdgeAutoScrollMaxPxPerTick);
}

double EdgeScrollTimer::verticalPerTick(double maxPerTick) const noexcept {
    if (!vertical_)
        return 0.0;
    return edgeScrollVelocity(pointer_.y, area_.getY(), area_.getBottom(), kEdgeZonePx, maxPerTick);
}

double EdgeScrollTimer::scrollView(TimelineViewState& view) const noexcept {
    const double px = horizontalPxPerTick();
    if (px == 0.0 || view.pixelsPerBeat <= 0.0)
        return 0.0;
    const double before = view.firstVisibleBeat;
    view.scrollBeats(px / view.pixelsPerBeat);
    return (view.firstVisibleBeat - before) * view.pixelsPerBeat;
}

} // namespace synth::ui
