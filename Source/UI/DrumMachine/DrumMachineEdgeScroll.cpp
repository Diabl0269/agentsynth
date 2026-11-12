// DrumMachineComponent: a drag follows the view. A paint stroke or a drum's end edge held in the grid's left or right
// edge zone, or a ramp across a lane under the grid held at that lane's edge, scrolls the grid's own view sideways at
// the clip lanes' pace (EdgeScrollTimer, faster nearer the edge); a stroke held at the top or bottom of the grid also
// scrolls the rows. Each tick re-derives the drag from the still pointer against the scrolled view: the stroke and the
// ramp reach the cells and hits that scrolled in under the pointer, and the edge stays under it. The loudness drag on
// one hit only changes a value and never scrolls. Release and Esc stop it (endGesture, DrumHitLane::endGesture).
// docs/timeline/drum-machine.md.

#include "DrumMachineComponent.h"

#include "UI/Timeline/EdgeAutoScroll.h"
#include <cmath>

namespace synth::ui {

bool DrumMachineComponent::gridDragFollows() const noexcept {
    return gesture_.kind == Gesture::Kind::EdgeDrag || gesture_.kind == Gesture::Kind::Stroke;
}

// The raw pointer, not the stroke's (which is clamped inside the grid), so a pointer past the edge scrolls at full
// speed.
void DrumMachineComponent::trackGridDrag(juce::Point<int> pointer) {
    edgeLane_ = nullptr;
    edgeScroll_.track(gridDragFollows(), pointer, gridRegion(), gesture_.kind == Gesture::Kind::Stroke);
}

void DrumMachineComponent::trackLaneDrag(DrumHitLane& lane, juce::Point<int> pointer) {
    edgeLane_ = &lane;
    const auto bars = lane.getBounds().withTrimmedLeft(kLabelColumnWidth);
    edgeScroll_.track(lane.isDragging(), pointer + lane.getPosition(), bars);
}

void DrumMachineComponent::edgeScrollTick() {
    const bool laneDrag = edgeLane_ != nullptr && edgeLane_->isDragging();
    if (!laneDrag && !gridDragFollows()) {
        stopEdgeScroll();
        return;
    }
    const double rowsPx = laneDrag ? 0.0 : edgeScroll_.verticalPerTick(kEdgeAutoScrollMaxPxPerTick);
    if (edgeScroll_.horizontalPxPerTick() == 0.0 && rowsPx == 0.0) {
        stopEdgeScroll();
        return;
    }
    const double movedX = edgeScroll_.scrollView(view_);
    const double scrollYBefore = scrollY_;
    if (rowsPx != 0.0)
        setScrollY(scrollY_ + rowsPx);
    const double movedY = scrollY_ - scrollYBefore;
    if (movedX == 0.0 && movedY == 0.0)
        return; // pinned at beat 0 and at the top or bottom row: nothing moved, nothing to re-derive
    if (movedX != 0.0)
        notifyViewChanged();
    const auto pointer = edgeScroll_.pointer();
    if (laneDrag) {
        edgeLane_->followScroll(static_cast<int>(std::lround(movedX)), pointer - edgeLane_->getPosition());
    } else if (gesture_.kind == Gesture::Kind::EdgeDrag) {
        dragEdgeTo(static_cast<double>(pointer.x));
    } else {
        gesture_.lastPos -= juce::Point<float>(static_cast<float>(movedX), static_cast<float>(movedY));
        extendStroke(pointer.toFloat());
    }
    repaint();
}

void DrumMachineComponent::stopEdgeScroll() {
    edgeScroll_.stopTimer();
    edgeLane_ = nullptr;
}

} // namespace synth::ui
