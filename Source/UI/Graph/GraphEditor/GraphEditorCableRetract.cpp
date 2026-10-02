// GraphEditorCableRetract.cpp -- removed cables retract into their source jack and fade (CableRetractAnimator.h):
// arming from a before/after diff, the driver, and the ghost paint pass. GraphEditor is declared in GraphEditor.h;
// sibling GraphEditor*.cpp files in this directory hold the rest of the class.

#include "GraphEditor.h"

std::vector<GraphEditor::VisibleCable> GraphEditor::snapshotCablesForRetract() { return buildVisibleCables(); }

// Called after a disconnect, an undo or a redo with the cables drawn before it. Every cable no longer drawn is
// kept as a ghost for one short retract, so a removal never just blinks out.
void GraphEditor::retractCablesGoneSince(const std::vector<VisibleCable>& before) {
    if (!cableRetract_.arm(before, buildVisibleCables()))
        return;
    juce::Component::SafePointer<GraphEditor> safeEditor(this);
    cableRetractDriverAnim_.start(
        vblankUpdater, 180.0, [](float t) { return t; },
        [safeEditor](float t) {
            if (safeEditor == nullptr)
                return;
            safeEditor->cableRetract_.applyTweenAt(t);
            safeEditor->content.repaint();
        },
        [safeEditor] {
            if (safeEditor == nullptr)
                return;
            safeEditor->cableRetract_.finish();
            safeEditor->content.repaint();
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
