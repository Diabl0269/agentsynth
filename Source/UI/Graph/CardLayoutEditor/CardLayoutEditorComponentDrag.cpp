// CardLayoutEditorComponentDrag.cpp -- drag-to-reorder on the shared vertical ReorderDragAnimator: the
// row under the pointer is lifted and follows it, the other rows of its group glide aside, and the
// release commits ONCE. Group headers take part as fixed slots, so a row dropped under a header joins
// that group. Esc cancels with nothing committed. docs/layout/animation.md#reorder-drag.
#include "CardLayoutEditorComponent.h"
#include "CardLayoutEditorRow.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
int indexOfKey(const std::vector<juce::String>& keys, const juce::String& key) {
    const auto it = std::find(keys.begin(), keys.end(), key);
    return it == keys.end() ? -1 : static_cast<int>(it - keys.begin());
}

// The rows a drag reorders among: every draggable row, and the group headers between them.
bool joinsDrag(const CardLayoutEditorRow& row) { return row.isDraggable() || row.isHeader(); }
} // namespace

// Slots are the rows' static positions, so a press during an earlier drop's settle still starts from
// the true layout. The grab offset is captured once, in rowsContent_'s coordinates: the handle moves
// with its row while the row is dragged, so the event's own position cannot be reused.
void CardLayoutEditorComponent::beginRowDrag(const juce::String& key, const juce::MouseEvent& e) {
    rowDrag_.discard();
    layOutRows();
    dragKeys_.clear();
    dragSlotStarts_.clear();

    std::vector<ReorderDragAnimator::Slot> slots;
    int pressedKey = -1;
    for (auto* row : rows_) {
        if (!joinsDrag(*row))
            continue;
        if (row->getKey() == key)
            pressedKey = static_cast<int>(slots.size());
        dragKeys_.push_back(row->getKey());
        dragSlotStarts_.push_back(static_cast<float>(row->getY()));
        slots.push_back({static_cast<float>(row->getY()), static_cast<float>(CardLayoutEditorRow::kRowHeight)});
    }
    if (pressedKey < 0)
        return;
    const float pointer = e.getEventRelativeTo(&rowsContent_).position.y;
    rowDrag_.begin(slots, pressedKey, pointer - slots[static_cast<size_t>(pressedKey)].start, pointer);
}

void CardLayoutEditorComponent::updateRowDrag(const juce::MouseEvent& e) {
    if (rowDrag_.dragTo(e.getEventRelativeTo(&rowsContent_).position.y))
        placeDragRows();
}

// The animator is released BEFORE the commit: the commit rebuilds every row, and the settle has to
// survive that (the new rows pick their glide up by key in placeDragRows).
void CardLayoutEditorComponent::endRowDrag() {
    if (rowDrag_.end() != ReorderDragSession::End::Commit)
        return;
    const auto& animator = rowDrag_.animator();
    const int draggedKey = animator.getDraggedKey();
    const int insertion = animator.getInsertionIndex();
    const auto newOrder = animator.getNewOrder();
    std::vector<float> finalStarts(dragSlotStarts_.size(), 0.0f);
    std::vector<juce::String> visibleOrder;
    for (size_t place = 0; place < newOrder.size(); ++place) {
        finalStarts[static_cast<size_t>(newOrder[place])] = dragSlotStarts_[place];
        visibleOrder.push_back(dragKeys_[static_cast<size_t>(newOrder[place])]);
    }
    const auto key = dragKeys_[static_cast<size_t>(draggedKey)];

    rowDrag_.release(finalStarts);
    placeDragRows();
    if (insertion != draggedKey) {
        model_.dropAt(key, visibleOrder);
        commitAndRebuild();
    }
}

// While a drag is live or settling, each row of the dragged group takes its place from the animator;
// every other row stays where the static layout put it. Once nothing is reordering the static layout
// is simply re-applied.
void CardLayoutEditorComponent::placeDragRows() {
    if (!rowDrag_.isReordering()) {
        layOutRows();
        for (auto* row : rows_)
            row->setLift(0.0f);
        return;
    }
    const auto& animator = rowDrag_.animator();
    for (auto* row : rows_) {
        const int key = indexOfKey(dragKeys_, row->getKey());
        if (key < 0 || !joinsDrag(*row))
            continue;
        const bool dragged = key == animator.getDraggedKey();
        const float top = dragged ? animator.getDraggedStart() : animator.getLayoutStart(key);
        row->setBounds(0, static_cast<int>(std::lround(top)), rowsContent_.getWidth(), CardLayoutEditorRow::kRowHeight);
        row->setLift(dragged ? animator.getLift() : 0.0f);
        if (dragged)
            row->toFront(false);
    }
}

} // namespace synth::ui
