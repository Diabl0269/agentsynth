// TimelineSectionStripEdgeScroll.cpp
//
// A drag on the Sections strip follows the view: a block being moved (or placed again), or the outline of a section
// being made, held in the strip's left or right edge zone scrolls the shared timeline view at the clip lanes' pace
// (EdgeScrollTimer: faster nearer the edge), and each tick re-derives the preview from the still pointer against the
// scrolled view, so the drop line and the ghost keep following. The class is declared in TimelineSectionStrip.h.

#include "UI/Timeline/TimelineSectionStrip/TimelineSectionStrip.h"
#include "UI/Timeline/TimelineViewState.h"

namespace synth::ui {

namespace {
bool following(bool blockDragging, bool cancelled, bool makeDragging) {
    return (blockDragging && !cancelled) || makeDragging;
}
} // namespace

// The timer runs only while a drag is live (a block drag not cancelled, or a make-drag) with the pointer in an edge
// zone; it stops the moment either stops holding (release, Esc, a new doc, the pointer back in the middle).
void TimelineSectionStrip::updateEdgeScrollArming() {
    edgeScroll_.track(following(blockDragging_, dragCancelled_, makeDragging_), lastDragPointer_.toInt(),
                      getLocalBounds());
}

void TimelineSectionStrip::edgeScrollTick() {
    if (!following(blockDragging_, dragCancelled_, makeDragging_) || edgeScroll_.horizontalPxPerTick() == 0.0) {
        stopEdgeScroll();
        return;
    }
    if (edgeScroll_.scrollView(viewState_) == 0.0)
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
