// PluginKnobPickerComponentDrag.cpp -- drag-to-reorder of the checked rows, on the shared vertical
// ReorderDragAnimator: the row under the pointer is lifted and follows it, the other checked rows
// glide aside, and the release commits ONCE (commitReorder -> one undo step for the whole move). Esc
// cancels with nothing committed. See docs/control/plugin-card-layout.md#choosing-knobs.
#include "PluginKnobPickerComponent.h"
#include "PluginKnobPickerRow.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
int indexOfId(const std::vector<juce::String>& ids, const juce::String& id) {
    const auto it = std::find(ids.begin(), ids.end(), id);
    return it == ids.end() ? -1 : static_cast<int>(it - ids.begin());
}
} // namespace

// Slots are the checked rows' static positions, so a press during an earlier drop's settle still
// starts from the true layout. The grab offset is captured once, in rowsContent_'s coordinates: the
// handle moves with its row while the row is dragged, so the event's own position cannot be reused.
void PluginKnobPickerComponent::beginRowDrag(const juce::String& paramId, const juce::MouseEvent& e) {
    rowDrag_.discard();
    layOutRows();
    dragParamIds_.clear();
    dragSlotStarts_.clear();

    std::vector<ReorderDragAnimator::Slot> slots;
    int pressedKey = -1;
    for (auto* row : rows_) {
        if (!row->isChecked())
            continue;
        if (row->getParamId() == paramId)
            pressedKey = static_cast<int>(slots.size());
        dragParamIds_.push_back(row->getParamId());
        dragSlotStarts_.push_back(static_cast<float>(row->getY()));
        slots.push_back({static_cast<float>(row->getY()), static_cast<float>(PluginKnobPickerRow::kRowHeight)});
    }
    if (pressedKey < 0)
        return;
    const float pointer = e.getEventRelativeTo(&rowsContent_).position.y;
    rowDrag_.begin(slots, pressedKey, pointer - slots[static_cast<size_t>(pressedKey)].start, pointer);
}

void PluginKnobPickerComponent::updateRowDrag(const juce::MouseEvent& e) {
    if (rowDrag_.dragTo(e.getEventRelativeTo(&rowsContent_).position.y))
        placeDragRows();
}

// The animator is released BEFORE the commit: commitReorder rebuilds every row, and the settle has to
// survive that (the new rows pick their glide up by parameter id in placeDragRows).
void PluginKnobPickerComponent::endRowDrag() {
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
        visibleOrder.push_back(dragParamIds_[static_cast<size_t>(newOrder[place])]);
    }
    const auto paramId = dragParamIds_[static_cast<size_t>(draggedKey)];

    rowDrag_.release(finalStarts);
    placeDragRows();
    if (insertion != draggedKey)
        commitReorder(paramId, workingIndexForDrop(visibleOrder, paramId));
}

// The dragged row goes before the row that follows it in the drop's order, or after the one that
// precedes it when it lands last; both are looked up among ALL the working slots, since a search can
// hide some checked rows.
int PluginKnobPickerComponent::workingIndexForDrop(const std::vector<juce::String>& visibleOrder,
                                                   const juce::String& paramId) const {
    std::vector<juce::String> others;
    for (const auto& slot : workingSlots_)
        if (slot.paramId != paramId)
            others.push_back(slot.paramId);
    const int place = indexOfId(visibleOrder, paramId);
    if (place >= 0 && place + 1 < static_cast<int>(visibleOrder.size()))
        return std::max(0, indexOfId(others, visibleOrder[static_cast<size_t>(place) + 1]));
    if (place > 0)
        return indexOfId(others, visibleOrder[static_cast<size_t>(place) - 1]) + 1;
    return 0;
}

// While a drag is live or settling, each dragged-group row takes its place from the animator; every
// other row stays where the static layout put it. Once nothing is reordering the static layout is
// simply re-applied.
void PluginKnobPickerComponent::placeDragRows() {
    if (!rowDrag_.isReordering()) {
        layOutRows();
        for (auto* row : rows_)
            row->setLift(0.0f);
        return;
    }
    const auto& animator = rowDrag_.animator();
    for (auto* row : rows_) {
        const int key = indexOfId(dragParamIds_, row->getParamId());
        if (key < 0 || !row->isChecked())
            continue;
        const bool dragged = key == animator.getDraggedKey();
        const float top = dragged ? animator.getDraggedStart() : animator.getLayoutStart(key);
        row->setBounds(0, static_cast<int>(std::lround(top)), rowsContent_.getWidth(), PluginKnobPickerRow::kRowHeight);
        row->setLift(dragged ? animator.getLift() : 0.0f);
        if (dragged)
            row->toFront(false);
    }
}

} // namespace synth::ui
