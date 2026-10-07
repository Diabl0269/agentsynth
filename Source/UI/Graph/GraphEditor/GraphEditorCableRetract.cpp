// GraphEditorCableRetract.cpp -- removed cables retract into their source jack and fade (CableRetractAnimator.h):
// arming from a before/after diff, the driver, and the ghost paint pass. GraphEditor is declared in GraphEditor.h;
// sibling GraphEditor*.cpp files in this directory hold the rest of the class.

#include "GraphEditor.h"

#include "UI/Layout/CableCurve.h"

std::vector<GraphEditor::VisibleCable> GraphEditor::snapshotCablesForRetract() { return buildVisibleCables(); }

namespace {
// Where the retracting ghosts are drawn now, in canvas coordinates.
juce::Rectangle<int> ghostArea(const CableRetractAnimator& retract) {
    juce::Rectangle<float> area;
    for (const auto& ghost : retract.ghosts())
        area = area.getUnion(synth::ui::cablePaintBounds(ghost.p1, ghost.p2));
    return area.getSmallestIntegerContainer();
}
} // namespace

// Called after a disconnect, an undo or a redo with the cables drawn before it. Every cable no longer drawn is
// kept as a ghost for one short retract, so a removal never just blinks out. Each frame repaints only where the ghosts
// were and are drawn, never the whole canvas.
void GraphEditor::retractCablesGoneSince(const std::vector<VisibleCable>& before) {
    if (!cableRetract_.arm(before, buildVisibleCables()))
        return;
    juce::Component::SafePointer<GraphEditor> safeEditor(this);
    cableRetractDriverAnim_.start(
        vblankUpdater, 180.0, [](float t) { return t; },
        [safeEditor](float t) {
            if (safeEditor == nullptr)
                return;
            const auto drawnBefore = ghostArea(safeEditor->cableRetract_);
            safeEditor->cableRetract_.applyTweenAt(t);
            safeEditor->content.repaint(drawnBefore.getUnion(ghostArea(safeEditor->cableRetract_)));
        },
        [safeEditor] {
            if (safeEditor == nullptr)
                return;
            const auto drawnBefore = ghostArea(safeEditor->cableRetract_);
            safeEditor->cableRetract_.finish();
            safeEditor->content.repaint(drawnBefore);
        });
    content.repaint();
}

void GraphEditor::advanceCableRetractForTest(float t) {
    cableRetract_.applyTweenAt(t);
    content.repaint();
}

void GraphEditor::finishCableRetractForTest() {
    cableRetract_.finish();
    content.repaint();
}
