// Concern: the Draw tool's shape and the one lane range, fanned out to the lane editors that act on them.
#include "UI/Timeline/AutomationLanes/TimelineAutomationLanes/TimelineAutomationLanes.h"

namespace synth::ui {

void TimelineAutomationLanes::setDrawShape(DrawShape shape) {
    drawShape_ = shape;
    for (auto& [id, editor] : editors_)
        editor->setDrawShape(shape);
    for (auto& [id, entry] : modulators_)
        for (auto& band : entry.bands)
            band->setDrawShape(shape);
}

// Every editor repaints, not just the range's: the band may have just left another lane.
void TimelineAutomationLanes::laneRangeChanged() {
    repaintEditors();
    for (auto& [id, header] : headers_)
        header->getControlStrip().refreshStates(); // Create custom LFO follows the range
    if (onLaneRangeChanged)
        onLaneRangeChanged();
}

// The editor the range was begun on, while it is still one of ours: a lane editor, or the editor of a modulator band.
// A lane id cannot find it (a band's private doc reuses the real docs' ids), so it is resolved through the owner.
// Null once the lane is folded away or deleted, or the band is rebuilt: a range on it can no longer be acted on.
AutomationLaneEditor* TimelineAutomationLanes::laneRangeEditor() const {
    auto* owner = laneRange_.getOwner();
    if (owner == nullptr)
        return nullptr;
    for (const auto& [id, editor] : editors_)
        if (editor.get() == owner)
            return editor.get();
    for (const auto& [id, entry] : modulators_)
        for (const auto& band : entry.bands)
            if (band->getEditor() == owner)
                return band->getEditor();
    return nullptr;
}

bool TimelineAutomationLanes::hasLaneRange() const { return laneRange_.hasWidth() && laneRangeEditor() != nullptr; }

bool TimelineAutomationLanes::laneRangeIsOnLane(synth::LaneId lane) const {
    const auto* editor = editorFor(lane);
    return editor != nullptr && laneRange_.isActive() && laneRange_.getOwner() == editor;
}

bool TimelineAutomationLanes::stampShapeOnLaneRange(DrawShape shape) {
    auto* editor = hasLaneRange() ? laneRangeEditor() : nullptr;
    return editor != nullptr && editor->getShapeGesture().stampOverRange(shape);
}

bool TimelineAutomationLanes::deleteLaneRangePoints() {
    auto* editor = hasLaneRange() ? laneRangeEditor() : nullptr;
    return editor != nullptr && editor->getShapeGesture().deleteRangePoints();
}

} // namespace synth::ui
