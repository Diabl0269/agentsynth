// TimelinePanelWheelScroll.cpp
//
// How a wheel or trackpad scroll reaches the track rows and the beat origin: a mouse notch eases through
// ScrollTweenRunner, a trackpad's vertical deltas are paced once per VBlank frame by ScrollFrameSmoother, and every
// vertical move ends in syncTrackScroll(), the one place the header column and the lanes are brought back in step.

#include "TimelinePanelComponent.h"
#include "UI/Timeline/TimelineSectionStrip/TimelineSectionStrip.h"

namespace synth::ui {

// A notch eases in via ScrollTweenRunner. A trackpad's vertical deltas go to the per-frame backlog, so the view moves
// once per frame however many events arrived. Anything else, and a host that isn't showing (headless tests), applies
// at once after ending any tween and settling the backlog.
void TimelinePanelComponent::scrollByWheel(int axis, double amount, bool eased) {
    const auto read = [this](int a) { return a == 0 ? viewState_.firstVisibleBeat : viewState_.trackScrollY; };
    const auto scroll = [this](int a, double d) { applyWheelScroll(a, d); };
    if (axis == 1) {
        if (eased)
            trackpadScroll_.flush(); // a notch after a swipe starts from where the swipe's backlog ends
        else {
            wheelTween_.stop();
            const auto step = [this](double d) {
                const double before = viewState_.trackScrollY;
                scrollTrackRows(d);
                return viewState_.trackScrollY - before;
            };
            if (trackpadScroll_.push(*this, amount, step))
                return;
        }
    }
    if (eased && wheelTween_.push(*this, axis, amount, read, scroll))
        return;
    wheelTween_.stop();
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
    viewState_.scrollBeats(amount);
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
    ++trackpadScroll_.syncCalls;
    trackpadScroll_.repaintedPx += (double)gridLanesBounds_.getWidth() * gridLanesBounds_.getHeight() +
                                   (double)trackHeaderViewport_.getWidth() * trackHeaderViewport_.getHeight();
}

} // namespace synth::ui
