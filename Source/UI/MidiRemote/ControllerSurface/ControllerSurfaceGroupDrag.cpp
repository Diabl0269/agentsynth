// Concern: FRO270 (docs/control/midi-remote-ui.md#surface-centre) -- turning a cell's own
// cell-to-cell drag delta (ControllerSurfaceCell::onDraggedByCells/onDragEnded, unchanged since
// FRO131) into a MOVE OF THE WHOLE SELECTION when the dragged cell is part of one, clamped as a
// single block against the grid's top-left bound and refused outright if it would land any moved
// control on a cell an unselected control already occupies. A lone selection (or a drag on a cell
// that isn't part of the current selection) is just this same code with a group of one -- there is
// exactly one drag-to-move implementation, not a single-cell path plus a separate group path.
//
// UNIFICATION NOTE (FRO270): before this ticket, a single-cell drag never checked for a target
// already occupied by another control -- it could park two controls on the same cell. That was
// never intentional (docs/control/midi-remote-ui.md never describes it, and no test asserted it);
// it was simply never worth guarding for one control at a time. A GROUP move makes the same gap
// much more visible and easy to trigger by accident, and there is no reasonable single shared rule
// ("refuse the group but allow a single cell to land on another") that wouldn't be its own source
// of confusion, so this refusal now applies uniformly to every drag, one and many alike.

#include "ControllerSurfaceComponent.h"

#include <algorithm>
#include <juce_core/juce_core.h>

namespace synth::ui {

ControllerSurfaceCell* ControllerSurfaceComponent::findMutableCell(const juce::String& controlId) {
    for (auto* cell : cells_)
        if (cell->getControlId() == controlId)
            return cell;
    return nullptr;
}

// The set of controls this drag moves together: the whole selection, if `controlId` (the cell the
// mouse actually pressed) is part of a multi-selection -- otherwise just `controlId` alone, so a
// drag on an unselected (or solely-selected) cell behaves exactly as a single-control drag always
// has.
std::vector<juce::String> ControllerSurfaceComponent::dragGroupFor(const juce::String& controlId) const {
    if (selectedIds_.size() > 1 && std::find(selectedIds_.begin(), selectedIds_.end(), controlId) != selectedIds_.end())
        return selectedIds_;
    return {controlId};
}

void ControllerSurfaceComponent::moveCellToLayout(ControllerSurfaceCell& cell, int col, int row) {
    cell.setBounds(kCellMargin + col * (kCellSize + kCellMargin), kCellMargin + row * (kCellSize + kCellMargin),
                   kCellSize, kCellSize);
}

// Fired many times per drag (once per cell boundary crossed) from the cell that has mouse capture.
// `deltaCol`/`deltaRow` are relative to THAT cell's own pre-drag position -- every group member's
// pre-drag position is still readable off its own cell (getControl().layout is never mutated
// mid-drag, only bounds are, so it stays the drag's "start" for the whole gesture). Clamping is a
// single min-over-the-group computation so the WHOLE block is held back together, never one member
// alone (which would break the relative layout the group started with).
void ControllerSurfaceComponent::handleCellDragged(const juce::String& controlId, int deltaCol, int deltaRow) {
    liveDragOriginId_ = controlId;
    liveDragDeltaCol_ = deltaCol;
    liveDragDeltaRow_ = deltaRow;

    const auto group = dragGroupFor(controlId);
    int minStartCol = 0;
    int minStartRow = 0;
    bool first = true;
    for (const auto& id : group) {
        const auto* cell = findMutableCell(id);
        if (cell == nullptr)
            continue;
        const auto& layout = cell->getControl().layout;
        if (first || layout.col < minStartCol)
            minStartCol = layout.col;
        if (first || layout.row < minStartRow)
            minStartRow = layout.row;
        first = false;
    }
    const int clampedDeltaCol = juce::jmax(deltaCol, -minStartCol);
    const int clampedDeltaRow = juce::jmax(deltaRow, -minStartRow);

    for (const auto& id : group) {
        auto* cell = findMutableCell(id);
        if (cell == nullptr)
            continue;
        const auto& layout = cell->getControl().layout;
        moveCellToLayout(*cell, layout.col + clampedDeltaCol, layout.row + clampedDeltaRow);
    }
}

// Fired once, when the gesture ends with at least one cell boundary having been crossed. Recomputes
// the same clamped delta handleCellDragged() last applied (from the state it left behind, since a
// cell's own onDragEnded carries no arguments), then either commits it (onControlsMoved) or -- an
// unselected control already sits on a target cell -- snaps every moved cell back to its start and
// fires nothing, per this file's own top comment.
void ControllerSurfaceComponent::handleCellDragEnded(const juce::String& controlId) {
    const auto group = dragGroupFor(controlId);
    const int deltaCol = liveDragOriginId_ == controlId ? liveDragDeltaCol_ : 0;
    const int deltaRow = liveDragOriginId_ == controlId ? liveDragDeltaRow_ : 0;
    liveDragOriginId_.clear();
    liveDragDeltaCol_ = 0;
    liveDragDeltaRow_ = 0;

    int minStartCol = 0;
    int minStartRow = 0;
    bool first = true;
    std::vector<ControllerSurfaceCell*> groupCells;
    for (const auto& id : group) {
        auto* cell = findMutableCell(id);
        if (cell == nullptr)
            continue;
        groupCells.push_back(cell);
        const auto& layout = cell->getControl().layout;
        if (first || layout.col < minStartCol)
            minStartCol = layout.col;
        if (first || layout.row < minStartRow)
            minStartRow = layout.row;
        first = false;
    }
    if (groupCells.empty())
        return;

    const int clampedDeltaCol = juce::jmax(deltaCol, -minStartCol);
    const int clampedDeltaRow = juce::jmax(deltaRow, -minStartRow);

    std::vector<MovedCell> moves;
    for (auto* cell : groupCells) {
        const auto& layout = cell->getControl().layout;
        moves.push_back({cell->getControlId(), layout.col + clampedDeltaCol, layout.row + clampedDeltaRow});
    }

    bool overlapsUnselected = false;
    for (auto* other : cells_) {
        if (std::find(group.begin(), group.end(), other->getControlId()) != group.end())
            continue; // a group member -- checked against the OTHER group members implicitly:
                      // the block move preserves relative offsets, so two members that didn't
                      // overlap before this drag can't overlap each other after it.
        const auto& otherLayout = other->getControl().layout;
        for (const auto& move : moves) {
            if (move.col == otherLayout.col && move.row == otherLayout.row) {
                overlapsUnselected = true;
                break;
            }
        }
        if (overlapsUnselected)
            break;
    }

    if (overlapsUnselected) {
        for (auto* cell : groupCells)
            moveCellToLayout(*cell, cell->getControl().layout.col, cell->getControl().layout.row);
        return;
    }

    if (onControlsMoved)
        onControlsMoved(moves);
}

} // namespace synth::ui
