// Concern: MixerInsertList's drag-reorder: the press on a row's name, the lifted row and the rows making room, the
// drop and its settle (Layout/ReorderDrag, as MixerSendList does for sends; docs/mixer/mixer.md#reordering-inserts).
#include "AppUndoManager.h"
#include "MixerInsertList.h"
#include "UI/Layout/DragCursor.h"

namespace synth::ui {
namespace {
using Kind = synth::MixerInsertEntry::Kind;
}

// The row being dragged, or settling from a drop, or -1.
int MixerInsertList::liftedRow() const {
    if (!rowDrag_.isReordering() || dragRunFirst_ < 0)
        return -1;
    const int row = dragRunFirst_ + rowDrag_.animator().getDraggedKey();
    return row < (int)visible_.size() ? row : -1;
}

// A row inside the run in motion is drawn where the animator puts it; every other row sits in its static slot.
float MixerInsertList::rowDrawTop(int rowIndex) const {
    if (rowDrag_.isReordering() && dragRunFirst_ >= 0 && rowIndex >= dragRunFirst_ && rowIndex <= dragRunLast_) {
        const auto& animator = rowDrag_.animator();
        const int key = rowIndex - dragRunFirst_;
        return key == animator.getDraggedKey() ? animator.getDraggedStart() : animator.getLayoutStart(key);
    }
    return (float)rowTop(rowIndex);
}

juce::Rectangle<int> MixerInsertList::drawBounds(int rowIndex) const {
    const auto bounds = getRowBounds(rowIndex);
    if (bounds.isEmpty() || isAddRow(rowIndex))
        return bounds;
    return bounds.withY((int)std::lround(rowDrawTop(rowIndex)));
}

std::vector<ReorderDragAnimator::Slot> MixerInsertList::runSlots(int first, int last) const {
    std::vector<ReorderDragAnimator::Slot> slots;
    const float top = (float)rowTop(first);
    for (int row = first; row <= last; ++row)
        slots.push_back({top + (float)((row - first) * kRowHeight), (float)kRowHeight});
    return slots;
}

// The name is the grab handle: everything left of the bypass button on a module that has somewhere to go.
bool MixerInsertList::isNameArea(juce::Point<int> position) const {
    const int row = rowIndexAt(position);
    const auto* entry = entryForRow(row);
    if (entry == nullptr || entry->kind != Kind::Module || position.x >= getWidth() - rowButtonsWidth(*entry))
        return false;
    return moveRunOf(row).first >= 0;
}

void MixerInsertList::updateHoverCursor(juce::Point<int> position) {
    const bool grab = rowDrag_.animator().isDragging() || isNameArea(position);
    setMouseCursor(grab ? dragGrabCursor() : juce::MouseCursor(juce::MouseCursor::NormalCursor));
}

// The pointer is converted into this list's own coordinates on every event; the grab offset is captured once here.
void MixerInsertList::beginRowDrag(int rowIndex, const juce::MouseEvent& event) {
    const auto [first, last] = moveRunOf(rowIndex);
    if (first < 0)
        return;
    dragRunFirst_ = first;
    dragRunLast_ = last;
    const auto slots = runSlots(first, last);
    const float pointer = event.getEventRelativeTo(this).position.y;
    rowDrag_.begin(slots, rowIndex - first, pointer - slots[(size_t)(rowIndex - first)].start, pointer);
}

void MixerInsertList::mouseDrag(const juce::MouseEvent& event) {
    if (pressedRow_ < 0)
        return;
    if (rowDrag_.dragTo(event.getEventRelativeTo(this).position.y)) {
        updateHoverCursor(event.getPosition());
        placeRows();
    }
}

void MixerInsertList::mouseUp(const juce::MouseEvent& event) {
    if (pressedRow_ < 0)
        return;
    const int pressed = pressedRow_;
    pressedRow_ = -1;
    switch (rowDrag_.end()) {
    case ReorderDragSession::End::Commit:
        commitRowDrag(pressed);
        return; // the commit may have rebuilt the mixer, this list included
    case ReorderDragSession::End::Click:
    case ReorderDragSession::End::Cancelled:
    case ReorderDragSession::End::Nothing:
        break;
    }
    updateHoverCursor(event.getPosition());
}

// The rows glide into the order the drop produces, and the move goes out ONCE, last: moveRowTo() ends in
// onMutated(), which normally rebuilds the mixer and destroys this list. The target is the animator's insertion
// index, the very place the preview showed, and the run only holds places the graph accepts.
void MixerInsertList::commitRowDrag(int pressedRow) {
    const auto& animator = rowDrag_.animator();
    const int toRow = dragRunFirst_ + animator.getInsertionIndex();
    const auto newOrder = animator.getNewOrder();
    const float runTop = (float)rowTop(dragRunFirst_);
    std::vector<float> finalStarts(newOrder.size(), 0.0f);
    for (size_t place = 0; place < newOrder.size(); ++place)
        finalStarts[(size_t)newOrder[place]] = runTop + (float)((int)place * kRowHeight);
    const float droppedAt = animator.getDraggedStart();
    rowDrag_.release(finalStarts);
    placeRows();
    if (toRow == pressedRow)
        return;
    if (onSettlePending)
        onSettlePending(toRow, droppedAt);
    if (!moveRowTo(pressedRow, toRow)) {
        if (onSettlePending)
            onSettlePending(-1, 0.0f);
        rowDrag_.discard(); // refused, so nothing rebuilt: the rows go back to the order they have
        placeRows();
    }
}

// The list a move rebuilt: rows already sit in the new order, so only the moved row glides.
void MixerInsertList::startSettleFrom(int rowIndex, float fromY) {
    if (rowIndex < 0 || rowIndex >= (int)visible_.size())
        return;
    int first = rowIndex, last = rowIndex;
    while (first > 0 && inSameRun(first - 1, rowIndex))
        --first;
    while (last + 1 < (int)visible_.size() && inSameRun(last + 1, rowIndex))
        ++last;
    dragRunFirst_ = first;
    dragRunLast_ = last;
    lastSettle_ = {rowIndex, fromY};
    rowDrag_.settleInto(runSlots(first, last), rowIndex - first, fromY);
    placeRows();
}

std::vector<juce::String> MixerInsertList::getDragPreviewNamesForTest() const {
    std::vector<juce::String> names;
    for (int row = 0; row < (int)visible_.size(); ++row)
        names.push_back(entries_[(size_t)visible_[(size_t)row]].name);
    if (!rowDrag_.isReordering() || dragRunFirst_ < 0)
        return names;
    const auto order = rowDrag_.animator().getNewOrder();
    const auto original = names;
    for (size_t place = 0; place < order.size(); ++place)
        names[(size_t)dragRunFirst_ + place] = original[(size_t)(dragRunFirst_ + order[place])];
    return names;
}

} // namespace synth::ui
