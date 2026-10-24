// CurveEditorComponent -- an undo or redo that moves the nodes of a same-shaped curve: they are drawn gliding from
// where they were (KeyedPointTween). The class is declared in CurveEditorComponent.h.

#include "CurveEditorComponent.h"

namespace synth::ui {

std::vector<juce::Point<float>> CurveEditorComponent::nodePositions() const {
    std::vector<juce::Point<float>> positions;
    const auto geometry = currentGeometry();
    for (int i = 0; i < model_.getNumNodes(); ++i)
        positions.push_back(geometry.nodePosition(i));
    return positions;
}

// A node still gliding from an earlier step continues from where it is drawn, so back-to-back undos stay smooth.
void CurveEditorComponent::glideNodesFrom(const std::vector<juce::Point<float>>& before) {
    const auto after = nodePositions();
    if (before.size() != after.size())
        return;
    for (size_t i = 0; i < after.size(); ++i)
        pointTween_.retarget((KeyedPointTween::Key)i, pointTween_.drawnAt((KeyedPointTween::Key)i, before[i]), after[i],
                             true);
}

} // namespace synth::ui
