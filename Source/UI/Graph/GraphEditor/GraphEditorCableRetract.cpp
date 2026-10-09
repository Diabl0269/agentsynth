// GraphEditorCableRetract.cpp -- removed cables retract into their source jack and fade, restored ones grow back
// (CableRetractAnimator.h): arming from a before/after diff and the driver. GraphEditor is declared in GraphEditor.h;
// sibling GraphEditor*.cpp files in this directory hold the rest of the class.

#include "GraphEditor.h"

#include "UI/Layout/ReducedMotion.h"
#include "UI/Layout/UIAnimation.h"

std::vector<GraphEditor::VisibleCable> GraphEditor::snapshotCablesForRetract() { return buildVisibleCables(); }

// Called after a disconnect, an undo or a redo with the cables drawn before it. Every cable no longer drawn is
// kept as a ghost for one short retract, so a removal never just blinks out; with `growAdded` (undo and redo) a cable
// the step brought back grows out of its source jack. Each frame repaints only where the moving wires were and are
// drawn, never the whole canvas. Animations: Off shows the result at once; Reduce Motion is an alpha fade.
void GraphEditor::retractCablesGoneSince(const std::vector<VisibleCable>& before, bool growAdded) {
    if (synth::ui::animationsOff()) {
        cableRetract_.finish();
        return;
    }
    if (!cableRetract_.arm(before, buildVisibleCables(), {growAdded, synth::ui::prefersReducedMotion()}))
        return;
    juce::Component::SafePointer<GraphEditor> safeEditor(this);
    cableRetractDriverAnim_.start(
        vblankUpdater, cableRetract_.durationMs(), [](float t) { return t; },
        [safeEditor](float t) {
            if (safeEditor == nullptr)
                return;
            const auto drawnBefore = safeEditor->cableRetract_.paintArea();
            safeEditor->cableRetract_.applyTweenAt(t);
            safeEditor->content.repaint(drawnBefore.getUnion(safeEditor->cableRetract_.paintArea()));
        },
        [safeEditor] {
            if (safeEditor == nullptr)
                return;
            const auto drawnBefore = safeEditor->cableRetract_.paintArea();
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
