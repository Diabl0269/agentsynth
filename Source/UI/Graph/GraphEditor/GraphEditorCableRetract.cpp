// GraphEditorCableRetract.cpp -- removed cables retract into their source jack and fade, restored ones grow back
// (CableRetractAnimator.h): arming from a before/after diff and the driver. GraphEditor is declared in GraphEditor.h;
// sibling GraphEditor*.cpp files in this directory hold the rest of the class.

#include "GraphEditor.h"

#include "UI/Layout/ReducedMotion.h"
#include "UI/Layout/UIAnimation.h"

std::vector<graph_editor_types::VisibleCable> GraphEditor::snapshotCablesForRetract() { return buildVisibleCables(); }

// Called after a disconnect, an undo or a redo with the cables drawn before it. Every cable no longer drawn is
// kept as a ghost for one short retract, so a removal never just blinks out; with `growAdded` (undo and redo) a cable
// the step brought back grows out of its source jack. Each frame repaints only where the moving wires were and are
// drawn, never the whole canvas. Animations: Off shows the result at once; Reduce Motion is an alpha fade.
namespace {
// Arms `retract` from the cables drawn before and the cables drawn now, and runs its driver; each frame repaints only
// where the moving wires were and are drawn.
void armCableMotion(CableRetractAnimator& retract, synth::ui::AnimationDriver& driver,
                    juce::VBlankAnimatorUpdater& updater, juce::Component& canvas,
                    const std::vector<graph_editor_types::VisibleCable>& before,
                    const std::vector<graph_editor_types::VisibleCable>& after, CableRetractAnimator::Options options) {
    if (synth::ui::animationsOff()) {
        retract.finish();
        return;
    }
    if (!retract.arm(before, after, options))
        return;
    juce::Component::SafePointer<juce::Component> safeCanvas(&canvas);
    driver.start(
        updater, retract.durationMs(), [](float t) { return t; },
        [&retract, safeCanvas](float t) {
            if (safeCanvas == nullptr)
                return;
            const auto drawnBefore = retract.paintArea();
            retract.applyTweenAt(t);
            safeCanvas->repaint(drawnBefore.getUnion(retract.paintArea()));
        },
        [&retract, safeCanvas] {
            if (safeCanvas == nullptr)
                return;
            const auto drawnBefore = retract.paintArea();
            retract.finish();
            safeCanvas->repaint(drawnBefore);
        });
    canvas.repaint();
}
} // namespace

void GraphEditor::retractCablesGoneSince(const std::vector<VisibleCable>& before, bool growAdded) {
    armCableMotion(cableRetract_, cableRetractDriverAnim_, vblankUpdater, content, before, buildVisibleCables(),
                   {growAdded, synth::ui::prefersReducedMotion(), /*morphChains=*/true, /*chainsOnly=*/false});
}

// Called after a module was inserted on a cable or deleted out of a chain, with the cables drawn before: the one cable
// splits in two running to the new card, the two halves of the deleted one run together. Every other cable change keeps
// its behaviour. Like the cards' glide it only runs while the canvas is showing (a headless run lands on the final
// cables at once; tests force it with CableRetractAnimator::setForceMotionForTest).
void GraphEditor::morphCablesSince(const std::vector<VisibleCable>& before) {
    if (!isShowing() && !cableRetract_.forcesMotionForTest()) {
        cableRetract_.finish();
        return;
    }
    repaintCanvas(); // drops the cable memo: the diff must see the cables as they are now
    armCableMotion(cableRetract_, cableRetractDriverAnim_, vblankUpdater, content, before, buildVisibleCables(),
                   {/*growAdded=*/false, synth::ui::prefersReducedMotion(), /*morphChains=*/true, /*chainsOnly=*/true});
}

void GraphEditor::advanceCableRetractForTest(float t) {
    cableRetract_.applyTweenAt(t);
    content.repaint();
}

void GraphEditor::finishCableRetractForTest() {
    cableRetract_.finish();
    content.repaint();
}
