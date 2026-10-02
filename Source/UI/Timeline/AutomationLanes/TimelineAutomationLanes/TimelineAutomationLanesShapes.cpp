// Concern: the Draw tool's shape and the one lane range, fanned out to the lane editors that act on them.
#include "UI/Timeline/AutomationLanes/TimelineAutomationLanes/TimelineAutomationLanes.h"

namespace synth::ui {

void TimelineAutomationLanes::setDrawShape(DrawShape shape) {
    drawShape_ = shape;
    for (auto& [id, editor] : editors_)
        editor->setDrawShape(shape);
}

// Every editor repaints, not just the range's: the band may have just left another lane.
void TimelineAutomationLanes::laneRangeChanged() {
    repaintEditors();
    if (onLaneRangeChanged)
        onLaneRangeChanged();
}

// A lane folded away or deleted takes its editor with it; a range on it can no longer be acted on.
bool TimelineAutomationLanes::hasLaneRange() const {
    return laneRange_.hasWidth() && editorFor(laneRange_.getLane()) != nullptr;
}

bool TimelineAutomationLanes::stampShapeOnLaneRange(DrawShape shape) {
    auto* editor = hasLaneRange() ? editorFor(laneRange_.getLane()) : nullptr;
    return editor != nullptr && editor->getShapeGesture().stampOverRange(shape);
}

bool TimelineAutomationLanes::deleteLaneRangePoints() {
    auto* editor = hasLaneRange() ? editorFor(laneRange_.getLane()) : nullptr;
    return editor != nullptr && editor->getShapeGesture().deleteRangePoints();
}

} // namespace synth::ui
