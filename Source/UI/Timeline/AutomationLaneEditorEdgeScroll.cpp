// AutomationLaneEditor -- a drag follows the view. A dot, the stretch box or a selection box held in the edge zone
// scrolls the shared timeline view at the clip lanes' pace and keeps the dragged thing under the still pointer (a
// selection box keeps its pressed corner on its beat); a key hold brings the moving dot along, eased with the dot,
// once it passes the margin. The class is declared in AutomationLaneEditor.h.

#include "AutomationLaneEditor.h"
#include "UI/Timeline/AutomationLanes/PointSelection/LaneKeyStep.h"
#include "UI/Timeline/EdgeAutoScroll.h"
#include "UI/Timeline/ScrollIntoView.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

bool AutomationLaneEditor::dragFollowsView() const noexcept {
    const bool box = dragMode_ == DragMode::None && selection_.isBoxActive();
    return (dragMode_ == DragMode::MoveHandle || dragMode_ == DragMode::Stretch || box) && !keyHold_.active();
}

// The preview of a dot move, a stretch or a selection box at a pointer position. A mouse move and an edge-scroll tick
// both land here, so a tick re-derives the preview from the same pointer against the scrolled view.
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
        // The box's pressed corner is pinned to its beat, so a scrolled view carries it along with the content.
        if (const auto* points = lanePoints()) {
            if (selection_.isBoxActive())
                selection_.setBoxAnchor(
                    {(int)std::lround(viewState_.beatToX(boxAnchorBeat_)), selection_.boxAnchor().y});
            selection_.dragTo(pos, *points, pointMapper());
        }
        break;
    case DragMode::TensionScrub:
    case DragMode::Pencil:
    case DragMode::Line:
    case DragMode::Eraser:
    case DragMode::LaneConstant:
        break;
    }
}

// The timer runs only while a dot, the stretch box or a selection box is being dragged with the pointer inside an edge
// zone; it stops the moment either stops holding (release, Escape, a tool change, the pointer back in the middle).
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

// The view follows the grabbed dot of a key move by where it is DRAWN (its rest place plus its eased offset), not by
// where it will rest: once that passes the margin line (kScrollIntoViewMargin in from the edge it is leaving by) the
// view scrolls by exactly the excess, so the drawn dot holds that line while it and the view glide together and
// nothing jumps. The dot's pixel offset is relative to the view, so a scroll of the view carries it along unchanged.
// Only the way the dot is carried counts, so a dot that starts near an edge is not pulled away from it, and the view
// stops at beat 0. A key hold feeds it every frame; after the key goes up the dot's ease does, until it lands.
void AutomationLaneEditor::followKeyDragDot() {
    if (dragMode_ == DragMode::MoveHandle && keyHold_.active()) {
        const double moved = previewBeat_ - dragOriginalBeat_;
        keyFollow_ = {true, previewBeat_, lane_key::tweenKey(dragOriginalBeat_),
                      moved < 0.0   ? -1
                      : moved > 0.0 ? 1
                                    : 0};
    } else if (!keyFollow_.active || dragMode_ != DragMode::None) {
        keyFollow_.active = false;
        return;
    } else if (!pointTween_.isAnimating()) {
        keyFollow_.active = false; // landed: the last frame still pins the dot at the line below
    }
    if (getWidth() <= 0 || viewState_.pixelsPerBeat <= 0.0)
        return;
    const double drawnX = viewState_.beatToX(keyFollow_.restBeat) + (double)pointTween_.offsetFor(keyFollow_.key).x;
    const double rightLine = (1.0 - kScrollIntoViewMargin) * (double)getWidth();
    const double leftLine = kScrollIntoViewMargin * (double)getWidth();
    double scrollPx = 0.0;
    if (keyFollow_.direction > 0 && drawnX > rightLine)
        scrollPx = drawnX - rightLine;
    else if (keyFollow_.direction < 0 && drawnX < leftLine)
        scrollPx = drawnX - leftLine;
    if (scrollPx == 0.0)
        return;
    const double before = viewState_.firstVisibleBeat;
    viewState_.scrollBeats(scrollPx / viewState_.pixelsPerBeat);
    if (viewState_.firstVisibleBeat != before)
        viewScrolledByDrag();
}

// The view is shared: the owner repaints the ruler and the other lanes and turns follow-playhead off, as a manual
// scroll does.
void AutomationLaneEditor::viewScrolledByDrag() {
    if (onViewScrolledByDrag)
        onViewScrolledByDrag();
}

} // namespace synth::ui
