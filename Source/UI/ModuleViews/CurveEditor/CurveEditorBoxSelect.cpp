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

void CurveEditorComponent::dragBoxTo(juce::Point<float> position) {
    const auto next =
        boxSelect_.dragTo(position.roundToInt(), [this](juce::Rectangle<float> r) { return movableNodesInBox(r); });
    if (!next)
        return;
    std::vector<int> indices = *next;
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    const int last = indices.empty() ? -1 : indices.back();
    setSelection(std::move(indices), last, last);
}

void CurveEditorComponent::endBox() {
    if (boxSelect_.release() == PointBoxGesture::Release::click)
        setSelectedIndex(-1);
}

} // namespace synth::ui
