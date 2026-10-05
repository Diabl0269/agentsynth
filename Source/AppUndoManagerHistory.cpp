// Concern: AppUndoManager's undo and redo of one history step: the restore bracket and hooks around it, and the
// canvas animation it triggers.
#include "AppUndoManager.h"
#include "UI/Graph/CardGlideAnimator/CardGlideAnimator.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"

// Undo and redo bump the edit serial for the same reason a fresh edit does: after a save, an undo moves the
// document AWAY from what is on disk, so it has to read as modified. A cable the step takes away retracts on
// the canvas rather than vanishing (GraphEditor::retractCablesGoneSince).
bool AppUndoManager::applyHistoryStep(bool redoStep) {
    juce::Component::SafePointer<GraphEditor> ge(graphEditor);
    const auto cablesBefore =
        ge != nullptr ? ge->snapshotCablesForRetract() : std::vector<graph_editor_types::VisibleCable>();
    const auto hullsBefore = ge != nullptr ? ge->snapshotPaintedHulls() : MacroHullGlide::Hulls();
    beginRestore();
    const bool did = redoStep ? undoManager.redo() : undoManager.undo();
    endRestore(did);
    if (did && ge != nullptr) {
        ge->retractCablesGoneSince(cablesBefore);
        ge->glideHullsFrom(hullsBefore); // a macro border a take-out or join moved glides back like it glided out
    }
    return did;
}

bool AppUndoManager::undo() { return applyHistoryStep(false); }

bool AppUndoManager::redo() { return applyHistoryStep(true); }

// Both undo() and redo() land here (docs/layout/animation.md "Undo and redo glide"). isRestoring() lets views glide
// rather than snap while it runs. The glide scope captures the cards' bounds before the restore and arms a slide from
// them afterwards, so the canvas moves back the way the edit moved forward. A card matches by component, or by node id
// when the restore rebuilt the cards (freeing any node tears every card down, as taking a module out of a macro does);
// a card the restore removes shrinks away and one it creates grows back (CardGlideAnimatorGhosts.cpp).
void AppUndoManager::beginRestore() {
    restoring_ = true;
    if (graphEditor != nullptr)
        glideScope_ = std::make_shared<CardGlideAnimator::Scope>(graphEditor->getCardGlide(), /*restore=*/true);
}

void AppUndoManager::endRestore(bool did) {
    glideScope_.reset(); // arms the slide from the captured bounds to the restored ones
    restoring_ = false;
    if (did)
        ++editSerial_;
}

// Distinct from the pre/post-restore lambdas SnapshotAction already carries: those are the
// GraphEditor's component lifecycle (detach before processors are freed, reconcile after), and
// the "pre" half of that pair deliberately fires LAZILY — a parameter-only undo frees nothing,
// so it never runs. These two always fire, in every case, which is what a caller needs for:
//
//  - `beforeRestore` — opening an AutomationRecorder::ScopedProgrammaticApply, so the parameter
//    writes a restore performs are never mistaken for a user's gesture. A parameter-only undo
//    is exactly the case that writes parameters, so hanging this off the lazy hook would miss it.
//  - `afterRestore` — re-running the timeline's binding reconciliation + publish. A graph
//    restore can strand a track/lane binding, and a timeline restore comes back out of
//    TimelineDoc::fromVar with every orphan flag reset to false (it is runtime-derived state),
//    so BOTH domains need the same pass.
void AppUndoManager::setRestoreHooks(std::function<void()> beforeRestore, std::function<void()> afterRestore) {
    beforeRestore_ = std::move(beforeRestore);
    afterRestore_ = std::move(afterRestore);
}
