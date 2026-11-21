// CurveEditorBoxSelect.cpp -- box select on empty space of a CurveEditorComponent: the shared PointBoxSelect gesture
// (UI/Layout/PointEditing/) with this editor's answer to "which dots are inside". The class is declared in
// CurveEditorComponent.h.

#include "CurveEditorComponent.h"

namespace synth::ui {

// The drawn centre, so a dot gliding after a move is boxed where it is seen. A node that cannot move (the pinned ends
// of an envelope) is never in the box, as it is never in any other selection.
std::vector<int> CurveEditorComponent::movableNodesInBox(juce::Rectangle<float> box) const {
    const auto geometry = currentGeometry();
    std::vector<int> inside;
    for (int i = 0; i < model_.getNumNodes(); ++i)
        if (model_.getNode(i).isMovable() && box.contains(geometry.nodePosition(i) + pointTween_.offsetFor(i)))
            inside.push_back(i);
    return inside;
}

// The cursor is the dot the box took in last, whichever side it grew from (PointBoxSelect::lastEntered), so a group
// never reads as one hovered dot at the far end; a box that took in nothing new keeps the cursor where it was.
void CurveEditorComponent::dragBoxTo(juce::Point<float> position) {
    const auto geometry = currentGeometry();
    const auto next = boxSelect_.dragTo(
        position.roundToInt(), [this](juce::Rectangle<float> r) { return movableNodesInBox(r); },
        [&](const int& i) { return geometry.nodePosition(i) + pointTween_.offsetFor(i); });
    if (!next)
        return;
    std::vector<int> indices = *next;
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    const auto has = [&](int i) { return std::find(indices.begin(), indices.end(), i) != indices.end(); };
    int cursor = boxSelect_.lastEntered().value_or(-1);
    if (cursor < 0 || !has(cursor))
        cursor = has(cursorIndex()) ? cursorIndex() : (indices.empty() ? -1 : indices.back());
    setSelection(std::move(indices), cursor, cursor);
}

void CurveEditorComponent::endBox() {
    if (boxSelect_.release() == PointBoxGesture::Release::click)
        setSelectedIndex(-1);
}

} // namespace synth::ui
