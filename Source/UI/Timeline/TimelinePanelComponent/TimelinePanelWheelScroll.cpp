// TimelinePanelWheelScroll.cpp
//
// How a wheel or trackpad scroll reaches the track rows and the beat origin: a mouse notch eases through
// ScrollTweenRunner, a trackpad's deltas are paced once per VBlank frame by ScrollFrameSmoother (WheelScrollRouter),
// and every vertical move ends in syncTrackScroll(), the one place the header column and the lanes are brought back in
// step.

#include "TimelinePanelComponent.h"
#include "UI/Timeline/TimelineSectionStrip/TimelineSectionStrip.h"

namespace synth::ui {

// A notch eases in; a trackpad's deltas on either axis go to a per-frame backlog, so the view moves once per frame
// however many events arrived. A host that isn't showing (headless tests) applies at once.
void TimelinePanelComponent::scrollByWheel(int axis, double amount, bool eased) {
    // The router works in pixels (its rest and clamp thresholds are pixel sizes): beats convert at the edge.
    const double unit = axis == 0 ? viewState_.pixelsPerBeat : 1.0;
    const auto read = [this](int a) {
        return a == 0 ? viewState_.firstVisibleBeat * viewState_.pixelsPerBeat : viewState_.trackScrollY;
    };
    const auto scroll = [this](int a, double d) { applyWheelScroll(a, a == 0 ? d / viewState_.pixelsPerBeat : d); };
    if (wheel_.route(*this, axis, amount * unit, eased, read, scroll))
        return;
    applyWheelScroll(axis, amount);
}

// Horizontal scroll moves only the ruler and the lanes region (grid, clips/roll, automation lanes,
// marker stems, playhead) -- never the track headers, which scroll only vertically.
void TimelinePanelComponent::applyWheelScroll(int axis, double amount) {
    if (axis == 1) {
        scrollTrackRows(amount);
        return;
    }
    // Only a USER's sideways scroll reaches here (the wheel, a trackpad swipe, the eased wheel
    // tween); follow's own page-flip goes through pageViewToShowBeat and never this method. Taking
    // the view away from the playhead by hand means "stop following", so the flag, the button and
    // the roll's mirror all switch off together rather than the next poll snapping the view back.
    if (follow_.enabled)
        setFollowPlayheadEnabled(false);
    const double before = viewState_.firstVisibleBeat;
    viewState_.scrollBeats(amount);
    if (viewState_.firstVisibleBeat == before)
        return; // clamped at the start: nothing moved, nothing to repaint
    ruler_.repaint();
    sectionStrip_->repaint();
    repaint(gridLanesBounds_);
}

// Every painter and the header column read trackScrollY through the same whole-logical-pixel rounding, so a move that
// does not change that rounding changes nothing on screen: the value moves, nothing syncs or repaints.
void TimelinePanelComponent::scrollTrackRows(double deltaPx) {
    const double before = viewState_.trackScrollY;
    viewState_.scrollTracksPx(deltaPx, maxTrackScrollPx());
    if (viewState_.trackScrollY == before || (std::llround(viewState_.trackScrollY) == std::llround(before)))
        return;
    syncTrackScroll();
}

void TimelinePanelComponent::syncTrackScroll() {
    // One writer, two readers: the header viewport mirrors trackScrollY, the lanes/roll repaint. The panel's one
    // repaint of the lanes rectangle covers the clip lane area (its child). setViewPosition fires
    // visibleAreaChanged, whose handler sees an unchanged value and stops -- no feedback loop.
    trackHeaderViewport_.setViewPosition(0, (int)std::llround(viewState_.trackScrollY));
    placeLaneBodies();
    repaint(gridLanesBounds_);
    ++wheel_.pad(1).syncCalls;
    wheel_.pad(1).repaintedPx += (double)gridLanesBounds_.getWidth() * gridLanesBounds_.getHeight() +
                                 (double)trackHeaderViewport_.getWidth() * trackHeaderViewport_.getHeight();
}

} // namespace synth::ui
