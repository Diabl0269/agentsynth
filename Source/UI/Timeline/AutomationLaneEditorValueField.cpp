// AutomationLaneEditor -- the typed value: the glue between a point (double-click, or Return on the keyboard cursor)
// and its PointValueField. The class is declared in AutomationLaneEditor.h.

#include "AutomationLaneEditor.h"
#include <cmath>

namespace synth::ui {

// Opens beside the point at `beat`, its value text selected. The bubble steps aside while the field is open (see
// showBubbleAt). The field is wired lazily so a caller that never types pays nothing at construction.
bool AutomationLaneEditor::openValueField(double beat) {
    const auto* points = lanePoints();
    if (points == nullptr)
        return false;
    for (const auto& p : *points) {
        if (p.beat != beat)
            continue;
        valueFieldBeat_ = beat;
        valueField_.parse = [this](const juce::String& text) { return parseTypedValue(text); };
        valueField_.onCommit = [this](double value) {
            if (valueFieldBeat_.has_value())
                commitTypedValue(*valueFieldBeat_, value);
        };
        bubble_.hide();
        const auto label = laneLabel ? laneLabel() : juce::String();
        valueField_.open({(float)viewState_.beatToX(p.beat), (float)valueToY(p.value)}, valueText(p.value),
                         "Value of " + (label.isNotEmpty() ? label : juce::String("automation")) + " point");
        return true;
    }
    return false;
}

// Return with the keyboard cursor on a point.
bool AutomationLaneEditor::openValueFieldOnCursor() {
    const auto cursor = selection_.getCursor();
    return cursor.has_value() && openValueField(*cursor);
}

// Text that is not a number is refused before any parser sees it: a parameter's own text-to-value turns "abc" into 0.
// The result is clamped, so typing past the range lands on the end of it.
std::optional<double> AutomationLaneEditor::parseTypedValue(const juce::String& text) const {
    if (!text.containsAnyOf("0123456789"))
        return std::nullopt;
    const auto value = textToValue ? textToValue(text) : PointValueField::parseNumber(text);
    if (!value.has_value() || !std::isfinite(*value))
        return std::nullopt;
    return clampValue(*value);
}

// Changes only the value of the point at `beat`; its beat, tension and curve are kept. Typing the value a point
// already has writes nothing.
void AutomationLaneEditor::commitTypedValue(double beat, double value) {
    const auto* points = lanePoints();
    if (points == nullptr)
        return;
    for (const auto& p : *points) {
        if (p.beat != beat)
            continue;
        LaneBreakpoint typed = p;
        typed.value = clampValue(value);
        if (typed.value != p.value)
            commitPointEdit(editTarget(), {beat}, {typed});
        return;
    }
}

// A point that was removed (undo, another editor) leaves nothing to type into.
void AutomationLaneEditor::closeValueFieldIfPointGone() {
    if (!valueField_.isOpen() || !valueFieldBeat_.has_value())
        return;
    const auto* points = lanePoints();
    if (points != nullptr)
        for (const auto& p : *points)
            if (p.beat == *valueFieldBeat_)
                return;
    valueField_.close(false);
}

} // namespace synth::ui
