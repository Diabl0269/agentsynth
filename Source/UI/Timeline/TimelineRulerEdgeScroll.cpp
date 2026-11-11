// TimelineRulerComponent -- a drag on the ruler follows the view. A marker, the loop range being drawn or the playhead
// being scrubbed, held in the ruler's left or right edge zone, scrolls the shared timeline view at the clip lanes'
// pace (EdgeScrollTimer, faster nearer the edge), and each tick re-derives the drag from the still pointer against the
// scrolled view, so the flag, the brace or the playhead stays under it. Esc ends a marker drag with the flag back
// where it was and a loop drag with the loop it started from. While the piano roll lends the ruler its own mapping
// (setMappingOverride) the strip shows the roll's beats, which this view does not move, so nothing scrolls then.
// The class is declared in TimelineRulerComponent.h.

#include "TimelineRulerComponent.h"
#include "Transport/TransportService.h"
#include "UI/Layout/ReorderDrag/ReorderCancelKey.h"
#include "UI/Timeline/EdgeScrollTimer.h"

namespace synth::ui {

bool TimelineRulerComponent::dragFollowsView() const noexcept {
    return overrideView_ == nullptr && (draggingMarker_.isValid() || (zoneDragging_ && transport_ != nullptr));
}

// Every drag event lands here: the timer runs only while the drag follows and the pointer sits in an edge zone, and
// Esc listens on the window for the length of the drag (a press on the ruler does not take keyboard focus).
void TimelineRulerComponent::trackDrag(juce::Point<int> pointer) {
    edgeScroll_->track(dragFollowsView(), pointer, getLocalBounds());
    if (!cancelKey_->isArmed())
        cancelKey_->arm(*this, [this] { cancelDrag(); });
}

void TimelineRulerComponent::edgeScrollTick() {
    if (!dragFollowsView()) {
        edgeScroll_->stopTimer();
        return;
    }
    if (edgeScroll_->horizontalPxPerTick() == 0.0) {
        edgeScroll_->stopTimer();
        return;
    }
    if (edgeScroll_->scrollView(viewState_) == 0.0)
        return; // pinned at beat 0: nothing moved, nothing to re-derive
    const double x = (double)edgeScroll_->pointer().x;
    if (draggingMarker_.isValid()) {
        markerDragMoved_ = true;
        previewMarkerAt(x);
    } else if (gestureZone_ == Zone::Playhead) {
        postSeekIfChanged(x);
    } else {
        postLoopIfChanged(x);
    }
    if (onViewScrolledByDrag)
        onViewScrolledByDrag();
    repaint();
}

// Release: the scroll and the Esc listener end with the drag.
void TimelineRulerComponent::endDrag() {
    edgeScroll_->stopTimer();
    cancelKey_->disarm();
}

// Esc mid-drag: a marker drag commits nothing, a loop drag puts the loop back, a scrub stays where it is (the playhead
// has no "before" to go back to, as in every other seek).
void TimelineRulerComponent::cancelDrag() {
    endDrag();
    if (draggingMarker_.isValid()) {
        draggingMarker_ = {};
        markerDragMoved_ = false;
    } else if (zoneDragging_ && gestureZone_ == Zone::Loop && transport_ != nullptr) {
        transport_->setLoop(loopBefore_.start, loopBefore_.end, loopBefore_.looping);
    }
    zoneDragging_ = false;
    repaint();
}

bool TimelineRulerComponent::isEdgeScrollRunningForTest() const noexcept { return edgeScroll_->isTimerRunning(); }

void TimelineRulerComponent::tickEdgeScrollForTest() { edgeScroll_->timerCallback(); }

bool TimelineRulerComponent::sendEscapeForTest() {
    return cancelKey_->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey), this);
}

} // namespace synth::ui
