// TimelineSectionStripEdgeScroll.cpp
//
// A drag on the Sections strip follows the view: a block being moved (or placed again), or the outline of a section
// being made, held in the strip's left or right edge zone scrolls the shared timeline view at the clip lanes' pace
// (EdgeAutoScroll.h: faster nearer the edge), and each tick re-derives the preview from the still pointer against the
// scrolled view, so the drop line and the ghost keep following. The class is declared in TimelineSectionStrip.h.

#include "UI/Timeline/EdgeAutoScroll.h"
#include "UI/Timeline/TimelineSectionStrip/TimelineSectionStrip.h"

namespace synth::ui {

namespace {
bool insideEdgeZone(float x, int width) { return edgeScrollVelocity((int)x, 0, width, kEdgeZonePx, 1.0) != 0.0; }
} // namespace

// The timer runs only while a drag is live (a block drag not cancelled, or a make-drag) with the pointer in an edge
// zone; it stops the moment either stops holding (release, Esc, a new doc, the pointer back in the middle).
void TimelineSectionStrip::updateEdgeScrollArming() {
    const bool dragging = (blockDragging_ && !dragCancelled_) || makeDragging_;
    const bool inZone = dragging && insideEdgeZone(lastDragPointer_.x, getWidth());
    if (inZone && !edgeScroll_.isTimerRunning())
        edgeScroll_.startTimerHz(kEdgeScrollHz);
    else if (!inZone && edgeScroll_.isTimerRunning())
        edgeScroll_.stopTimer();
}

void TimelineSectionStrip::edgeScrollTick() {
    const bool dragging = (blockDragging_ && !dragCancelled_) || makeDragging_;
    if (!dragging) {
        stopEdgeScroll();
        return;
    }
    const double pxPerTick =
        edgeScrollVelocity((int)lastDragPointer_.x, 0, getWidth(), kEdgeZonePx, kEdgeAutoScrollMaxPxPerTick);
    if (pxPerTick == 0.0) {
        stopEdgeScroll();
        return;
    }
    if (viewState_.pixelsPerBeat <= 0.0)
        return;
    const double before = viewState_.firstVisibleBeat;
    viewState_.scrollBeats(pxPerTick / viewState_.pixelsPerBeat);
    if (viewState_.firstVisibleBeat == before)
        return; // pinned at beat 0: nothing moved, nothing to re-derive
    if (makeDragging_)
        updateMakeDragAt(lastDragPointer_);
    else
        updateBlockDragAt(lastDragPointer_, lastDragShift_);
    if (onViewScrolledByDrag)
        onViewScrolledByDrag();
    repaint();
}

void TimelineSectionStrip::stopEdgeScroll() { edgeScroll_.stopTimer(); }

} // namespace synth::ui
