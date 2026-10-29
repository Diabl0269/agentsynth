// AutomationLaneEditor -- where each point is drawn: its preview place while a drag, stretch or key hold moves it,
// plus the eased offset of a step, an undo or a redo. The dot and every marker around it (the keyboard cursor's focus
// ring, the selection box) take their place from here, so they cannot drift apart. The class is declared in
// AutomationLaneEditor.h.

#include "AutomationLaneEditor.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Timeline/AutomationLanes/PointSelection/LaneKeyStep.h"

namespace synth::ui {

std::map<double, LaneBreakpoint> AutomationLaneEditor::movedPointsByRestingBeat() const {
    std::map<double, LaneBreakpoint> moved;
    if (dragMode_ == DragMode::MoveHandle) {
        const auto now = dragMovedPoints();
        for (std::size_t i = 0; i < dragPoints_.size() && i < now.size(); ++i)
            moved[dragPoints_[i].beat] = now[i];
    } else if (dragMode_ == DragMode::Stretch) {
        const auto& result = stretch_.result();
        for (std::size_t i = 0; i < result.moved.size(); ++i)
            moved[stretch_.original()[i].beat] = result.moved[i];
        for (std::size_t i = 0; i < result.pushed.size(); ++i)
            moved[result.pushedFrom[i]] = result.pushed[i];
    }
    return moved;
}

// The eased offset is keyed by the beat the point rests at, the key a step or a restore retargets.
juce::Point<float> AutomationLaneEditor::drawnPointPosition(const LaneBreakpoint& point,
                                                            const std::map<double, LaneBreakpoint>& moved) const {
    const auto it = moved.find(point.beat);
    return pointPosition(it != moved.end() ? it->second : point) +
           pointTween_.offsetFor(lane_key::tweenKey(point.beat));
}

juce::Rectangle<float> AutomationLaneEditor::drawnSelectionBounds() const {
    const auto* points = lanePoints();
    if (points == nullptr)
        return {};
    const auto moved = movedPointsByRestingBeat();
    std::vector<juce::Point<float>> drawn;
    for (const auto& point : *points)
        if (selection_.contains(point.beat))
            drawn.push_back(drawnPointPosition(point, moved));
    if (drawn.empty())
        return {};
    auto box = juce::Rectangle<float>(drawn.front(), drawn.front());
    for (const auto& p : drawn)
        box = juce::Rectangle<float>::leftTopRightBottom(std::min(box.getX(), p.x), std::min(box.getY(), p.y),
                                                         std::max(box.getRight(), p.x), std::max(box.getBottom(), p.y));
    return box;
}

// The ring is around the keyboard cursor's dot where that dot is drawn, or around the whole lane when there is none.
void AutomationLaneEditor::paintKeyboardCursorRing(juce::Graphics& g) {
    if (!showsFocusRing(*this))
        return;
    const auto cursor = selection_.getCursor();
    const auto* points = lanePoints();
    if (cursor.has_value() && points != nullptr) {
        const auto moved = movedPointsByRestingBeat();
        for (const auto& point : *points)
            if (point.beat == *cursor) {
                const auto at = drawnPointPosition(point, moved);
                const auto area = juce::Rectangle<float>(at, at).expanded(kHandleRadiusPx + 2.0f);
                paintFocusRing(g, area, *this, area.getWidth() / 2.0f);
                return;
            }
    }
    paintFocusRing(g, getLocalBounds().toFloat(), *this);
}

} // namespace synth::ui
