// Concern: AppUndoManager's undo and redo of one history step, and the canvas animation they trigger.
#include "AppUndoManager.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"

// Undo and redo bump the edit serial for the same reason a fresh edit does: after a save, an undo moves the
// document AWAY from what is on disk, so it has to read as modified. A cable the step takes away retracts on
// the canvas rather than vanishing (GraphEditor::retractCablesGoneSince).
bool AppUndoManager::applyHistoryStep(bool redoStep) {
    juce::Component::SafePointer<GraphEditor> ge(graphEditor);
    const auto cablesBefore =
        ge != nullptr ? ge->snapshotCablesForRetract() : std::vector<graph_editor_types::VisibleCable>();
    beginRestore();
    const bool did = redoStep ? undoManager.redo() : undoManager.undo();
    endRestore(did);
    if (did && ge != nullptr)
        ge->retractCablesGoneSince(cablesBefore);
    return did;
}

bool AppUndoManager::undo() { return applyHistoryStep(false); }

bool AppUndoManager::redo() { return applyHistoryStep(true); }
