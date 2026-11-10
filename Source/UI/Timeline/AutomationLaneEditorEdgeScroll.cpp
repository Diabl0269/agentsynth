// AutomationLaneEditor -- a drag follows the view. A dot or the stretch box held in the edge zone scrolls the shared
// timeline view at the clip lanes' pace and keeps the dragged thing under the still pointer; a key hold brings the
// moving dot back into view when a step would carry it out. The class is declared in AutomationLaneEditor.h.

#include "AutomationLaneEditor.h"
#include "UI/Timeline/EdgeAutoScroll.h"
#include "UI/Timeline/ScrollIntoView.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
// How much of the view either side of a dot's centre must be on screen for the dot to count as in view: its drawn
// radius and a little air.
constexpr double kDotInViewPadPx = 8.0;
} // namespace

bool AutomationLaneEditor::dragFollowsView() const noexcept {
    return (dragMode_ == DragMode::MoveHandle || dragMode_ == DragMode::Stretch) && !keyHold_.active();
}

// The preview of a dot move or a stretch at a pointer position. A mouse move and an edge-scroll tick both land here,
// so a tick re-derives the preview from the same pointer against the scrolled view.
void AutomationLaneEditor::previewDragAt(juce::Point<int> pos) {
    switch (dragMode_) {
    case DragMode::MoveHandle: {
        // The whole selection moves by the grabbed point's delta; the block stops at beat 0 as a unit and never lands
        // on another point.
        const double target = std::max(0.0, snappedBeatAt(viewState_.xToBeat((double)pos.x)));
        const double delta = settledBeatDelta(dragPoints_, target - dragOriginalBeat_);
        previewBeat_ = delta == target - dragOriginalBeat_ ? target : dragOriginalBeat_ + delta;
        previewValue_ = clampValue(yToValue(pos.y));
        showBubbleAt(previewBeat_, previewValue_);
        refreshStretchBox();
        break;
    }
    case DragMode::Stretch:
        dragStretch(pos);
        refreshStretchBox();
        break;
    case DragMode::None:
    case DragMode::TensionScrub:
    case DragMode::Pencil:
    case DragMode::Line:
    case DragMode::Eraser:
    case DragMode::LaneConstant:
        break;
    }
}

// The timer runs only while a dot or the stretch box is being dragged with the pointer inside an edge zone; it stops
// the moment either stops holding (release, Escape, a tool change, the pointer back in the middle).
void AutomationLaneEditor::updateEdgeScrollArming() {
    const bool inZone =
        dragFollowsView() && edgeScrollVelocity(lastDragPointer_.x, 0, getWidth(), kEdgeZonePx, 1.0) != 0.0;
    if (inZone && !edgeScroll_.isTimerRunning())
        edgeScroll_.startTimerHz(kEdgeScrollHz);
    else if (!inZone && edgeScroll_.isTimerRunning())
        edgeScroll_.stopTimer();
}

// One tick scrolls the view by the edge speed at the last pointer (faster nearer the edge), then moves the preview to
// where that pointer now points, so the dot follows the scroll by the same amount. The view stops at beat 0.
void AutomationLaneEditor::edgeScrollTick() {
    if (!dragFollowsView()) {
        stopEdgeScroll();
        return;
    }
    const double pxPerTick =
        edgeScrollVelocity(lastDragPointer_.x, 0, getWidth(), kEdgeZonePx, kEdgeAutoScrollMaxPxPerTick);
    if (pxPerTick == 0.0) {
        stopEdgeScroll();
        return;
    }
    if (viewState_.pixelsPerBeat <= 0.0)
        return;
    const double before = viewState_.firstVisibleBeat;
    viewState_.scrollBeats(pxPerTick / viewState_.pixelsPerBeat);
    if (viewState_.firstVisibleBeat == before)
        return;
    previewDragAt(lastDragPointer_);
    viewScrolledByDrag();
    repaint();
}

void AutomationLaneEditor::stopEdgeScroll() { edgeScroll_.stopTimer(); }

// After a key step: when the moving dot would be out of view, the view jumps so it lands a margin in from the edge it
// was leaving by (firstBeatShowing), in one move per step, so the scroll never fights the dot's own ease.
void AutomationLaneEditor::keepKeyDragInView() {
    if (dragMode_ != DragMode::MoveHandle || getWidth() <= 0 || viewState_.pixelsPerBeat <= 0.0)
        return;
    const double visibleBeats = (double)getWidth() / viewState_.pixelsPerBeat;
    const double pad = kDotInViewPadPx / viewState_.pixelsPerBeat;
    const double first = viewState_.firstVisibleBeat;
    const double newFirst = firstBeatShowing(first, visibleBeats, previewBeat_ - pad, previewBeat_ + pad);
    if (std::abs(newFirst - first) < 1e-9)
        return;
    viewState_.firstVisibleBeat = newFirst;
    viewScrolledByDrag();
}

// The view is shared: the owner repaints the ruler and the other lanes and turns follow-playhead off, as a manual
// scroll does.
void AutomationLaneEditor::viewScrolledByDrag() {
    if (onViewScrolledByDrag)
        onViewScrolledByDrag();
}

} // namespace synth::ui
