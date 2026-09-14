#include "MacroPortConfigDialog.h"
#include "MacroPortConfigDialogInternal.h"

namespace synth::ui {

// Concern: drag-to-reorder (mouse) and keyboard row-focus navigation.

// ---- T152 drag-to-reorder ----------------------------------------------------------------------
// beginRowDrag/updateRowDrag/endRowDrag implement the real mouse path (DragHandle wires straight
// to these); the *ForTest seams below call PortRowComponent::commitDragTo directly instead of
// synthesizing a mouseDown/mouseDrag/mouseUp sequence, the same "drive the real controls, skip the
// mouse plumbing" idiom every other *ForTest seam in this file already uses.

void MacroPortConfigDialog::beginRowDrag(PortRowComponent& row) {
    draggingNodeUuid_ = row.nodeUuid;
    dragDropIndexInGroup_ = -1; // recomputed on the first updateRowDrag; -1 = "no move yet"
}

void MacroPortConfigDialog::updateRowDrag(PortRowComponent& row, juce::Point<int> screenPos) {
    if (row.nodeUuid != draggingNodeUuid_)
        return; // defensive: only the row that started the drag drives it

    const int draggedIndex = rowControls_.indexOf(&row);
    if (draggedIndex < 0)
        return;
    const bool isInput = rows_[(size_t)draggedIndex].isInput;
    const int localY = rowsContent_.getLocalPoint(nullptr, screenPos).y;

    // Count how many OTHER rows in the same direction group sit above the drop point — that count
    // IS the dragged row's new index once it is removed from and reinserted into that group,
    // exactly the index reorderMacroPortToIndex (GraphEditor.cpp) expects. Excluding the dragged
    // row itself (rather than comparing against its own, unmoving on-screen position) is what
    // makes this work with the row staying visually in place during the drag, instead of needing
    // to follow the cursor like a real "lift and carry" drag would.
    int dropIndex = 0;
    int firstOtherInGroup = -1, lastOtherInGroup = -1;
    for (int i = 0; i < rowControls_.size(); ++i) {
        if (i == draggedIndex || rows_[(size_t)i].isInput != isInput)
            continue;
        if (firstOtherInGroup < 0)
            firstOtherInGroup = i;
        lastOtherInGroup = i;
        if (localY > rowControls_[i]->getBounds().getCentreY())
            ++dropIndex;
    }
    dragDropIndexInGroup_ = dropIndex;

    // Visual feedback: the two rows straddling the drop point get an insertion-line indicator;
    // every other row (including the dragged one) clears it. Computed fresh each call rather than
    // diffed against the previous call — setDropIndicator() itself is the repaint-only-on-change
    // guard, so this stays cheap.
    for (int i = 0; i < rowControls_.size(); ++i) {
        if (i == draggedIndex || rows_[(size_t)i].isInput != isInput) {
            rowControls_[i]->setDropIndicator(-1);
            continue;
        }
        int otherRank = 0; // this row's rank among the OTHER rows in its group, top to bottom
        for (int j = firstOtherInGroup; j <= lastOtherInGroup; ++j) {
            if (j == draggedIndex || rows_[(size_t)j].isInput != isInput)
                continue;
            if (j == i)
                break;
            ++otherRank;
        }
        if (otherRank == dropIndex)
            rowControls_[i]->setDropIndicator(0); // the drop lands just above this row
        else if (otherRank == dropIndex - 1)
            rowControls_[i]->setDropIndicator(1); // the drop lands just below this row
        else
            rowControls_[i]->setDropIndicator(-1);
    }
}

void MacroPortConfigDialog::endRowDrag(PortRowComponent& row) {
    clearDragIndicators();
    if (row.nodeUuid == draggingNodeUuid_ && dragDropIndexInGroup_ >= 0)
        row.commitDragTo(dragDropIndexInGroup_);
    draggingNodeUuid_ = {};
    dragDropIndexInGroup_ = -1;
}

void MacroPortConfigDialog::clearDragIndicators() {
    for (auto* rc : rowControls_)
        rc->setDropIndicator(-1);
}

// ---- T153 keyboard row navigation ---------------------------------------------------------------

int MacroPortConfigDialog::arrowNavigationTargetRow(int fromRow, bool moveDown) const {
    const int target = fromRow + (moveDown ? 1 : -1);
    return (target >= 0 && target < rowControls_.size()) ? target : -1;
}

void MacroPortConfigDialog::moveRowFocus(PortRowComponent& from, RowControl target, bool moveDown) {
    const int fromIndex = rowControls_.indexOf(&from);
    if (fromIndex < 0)
        return;
    const int toIndex = arrowNavigationTargetRow(fromIndex, moveDown);
    if (toIndex < 0)
        return; // at either end of the list — no wraparound (see the header's own comment)

    auto* target_ = rowControls_[toIndex];
    switch (target) {
    case RowControl::Colour:
        target_->colourSwatch.grabKeyboardFocus();
        break;
    case RowControl::Delete:
        target_->deleteButton.grabKeyboardFocus();
        break;
    }
}

} // namespace synth::ui
