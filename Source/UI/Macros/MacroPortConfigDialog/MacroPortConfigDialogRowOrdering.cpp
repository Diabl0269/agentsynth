#include "MacroPortConfigDialog.h"
#include "MacroPortConfigDialogInternal.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

// Concern: drag-to-reorder (mouse, on the shared vertical ReorderDragAnimator) and keyboard
// row-focus navigation.

// ---- Drag-to-reorder ---------------------------------------------------------------------------
// The row under the pointer is lifted and follows it, the other rows of its direction group glide
// aside (an input never crosses into the outputs), and the release commits ONCE through
// PortRowComponent::commitDragTo -> onReorderPortTo. Esc cancels with nothing committed.
// beginRowDrag/updateRowDrag/endRowDrag are the real mouse path (DragHandle wires straight to
// them); dragRowToIndexInGroupForTest calls commitDragTo directly instead, the same "drive the
// real controls, skip the mouse plumbing" idiom every other *ForTest seam in this file uses.

float MacroPortConfigDialog::rowDragPointerY(const juce::MouseEvent& e) {
    return e.getEventRelativeTo(&rowsContent_).position.y;
}

// Slots are the rows' static positions (one row height plus the gap each), so a press during an
// earlier drop's settle still starts from the true layout. The grab offset is captured once, in
// rowsContent_'s coordinates; the handle moves with its row, so the event's own position cannot be
// reused.
void MacroPortConfigDialog::beginRowDrag(PortRowComponent& row, const juce::MouseEvent& e) {
    rowDrag_.discard();
    layOutOrMeasureRows(/*apply=*/true, rowsContent_.getWidth());
    dragGroupUuids_.clear();
    dragSlotStarts_.clear();

    const int draggedIndex = rowControls_.indexOf(&row);
    if (draggedIndex < 0)
        return;
    const bool isInput = rows_[(size_t)draggedIndex].isInput;
    std::vector<ReorderDragAnimator::Slot> slots;
    int pressedKey = -1;
    for (int i = 0; i < rowControls_.size(); ++i) {
        if (rows_[(size_t)i].isInput != isInput)
            continue;
        if (i == draggedIndex)
            pressedKey = (int)slots.size();
        dragGroupUuids_.push_back(rowControls_[i]->nodeUuid);
        dragSlotStarts_.push_back((float)rowControls_[i]->getY());
        slots.push_back({(float)rowControls_[i]->getY(), (float)(kRowHeight + kRowGap)});
    }
    const float pointer = rowDragPointerY(e);
    rowDrag_.begin(slots, pressedKey, pointer - slots[(size_t)pressedKey].start, pointer);
}

void MacroPortConfigDialog::updateRowDrag(const juce::MouseEvent& e) {
    if (rowDrag_.dragTo(rowDragPointerY(e)))
        placeDragRows();
}

void MacroPortConfigDialog::endRowDrag(PortRowComponent& row) {
    if (rowDrag_.end() == ReorderDragSession::End::Commit)
        commitRowDrag(row);
}

// The animator is released BEFORE the commit goes out: the commit's refreshPorts() replaces every
// row component (and, in the app, arrives asynchronously), and the settle has to survive that. The
// final starts are the group's slots in the order the drop produces, so the rows land exactly where
// the rebuilt list will put them.
void MacroPortConfigDialog::commitRowDrag(PortRowComponent& row) {
    const auto& animator = rowDrag_.animator();
    const int draggedKey = animator.getDraggedKey();
    const int insertion = animator.getInsertionIndex();
    const auto newOrder = animator.getNewOrder();
    std::vector<float> finalStarts(dragSlotStarts_.size(), 0.0f);
    for (size_t place = 0; place < newOrder.size(); ++place)
        finalStarts[(size_t)newOrder[place]] = dragSlotStarts_[place];

    rowDrag_.release(finalStarts);
    placeDragRows();
    if (insertion != draggedKey)
        row.commitDragTo(insertion);
}

// While a drag is live or settling, each row of the dragged group takes its place from the animator
// (matched by uuid, so the rows a commit rebuilds keep their glide); every other row stays where the
// static layout put it. Once nothing is reordering, the static layout is simply re-applied.
void MacroPortConfigDialog::placeDragRows() {
    if (!rowDrag_.isReordering()) {
        layOutOrMeasureRows(/*apply=*/true, rowsContent_.getWidth());
        for (auto* rc : rowControls_)
            rc->setLift(0.0f);
        return;
    }
    const auto& animator = rowDrag_.animator();
    for (auto* rc : rowControls_) {
        const auto at = std::find(dragGroupUuids_.begin(), dragGroupUuids_.end(), rc->nodeUuid);
        if (at == dragGroupUuids_.end())
            continue;
        const int key = (int)(at - dragGroupUuids_.begin());
        const bool dragged = key == animator.getDraggedKey();
        const float top = dragged ? animator.getDraggedStart() : animator.getLayoutStart(key);
        rc->setBounds(0, (int)std::lround(top), rowsContent_.getWidth(), kRowHeight);
        rc->setLift(dragged ? animator.getLift() : 0.0f);
        if (dragged)
            rc->toFront(false);
    }
}

// A live drag owns Escape: it cancels the drag (the ReorderCancelKey on the window would only see the
// key after this dialog had already closed itself).
void MacroPortConfigDialog::escapePressed() {
    if (rowDrag_.animator().isDragging())
        rowDrag_.abort();
    else
        requestClose();
}

// ---- Keyboard row navigation --------------------------------------------------------------------

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
