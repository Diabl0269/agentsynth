// CurveEditorSelection.cpp -- the set of selected points of a CurveEditorComponent and the edits that
// act on all of it at once: extend, toggle, rigid move, remove. The class is declared in
// CurveEditorComponent.h.
//
// The selection is the shared PointSelectionModel keyed by node index (UI/Layout/PointEditing/), with the curve's
// rules (kSelectionRules): a sorted set plus a cursor (the point the keyboard continues from and a screen reader reads)
// and an anchor (where a Shift-extended range started). A fully pinned node is never in it.

#include "CurveEditorComponent.h"
#include "UI/Layout/PointEditing/PointGroupMove.h"

#include <algorithm>
#include <cmath>

namespace synth::ui {

bool CurveEditorComponent::isSelected(int index) const { return selection_.contains(index); }

int CurveEditorComponent::cursorIndex() const noexcept { return selection_.cursor().value_or(-1); }

std::vector<int> CurveEditorComponent::movableIndices() const {
    std::vector<int> movable;
    for (int i = 0; i < model_.getNumNodes(); ++i)
        if (model_.getNode(i).isMovable())
            movable.push_back(i);
    return movable;
}

void CurveEditorComponent::setSelection(std::vector<int> indices, int cursor, int anchor) {
    const auto key = [](int i) { return i >= 0 ? std::optional<int>(i) : std::nullopt; };
    if (!selection_.assign(std::move(indices), key(cursor), key(anchor)))
        return;
    selectionChanged();
}

void CurveEditorComponent::selectionChanged() {
    repaint();
    refreshAccessibilityValue();
}

void CurveEditorComponent::setSelectedIndex(int index) {
    setSelection(index >= 0 ? std::vector<int>{index} : std::vector<int>{}, index, index);
}

void CurveEditorComponent::remapSelectionForMove(int from, int to) {
    selection_.rekey([from, to](int j) {
        if (j == from)
            return to;
        if (from < to && j > from && j <= to)
            return j - 1;
        if (to < from && j >= to && j < from)
            return j + 1;
        return j;
    });
}

// A delta of 0 steps back once a cursor exists (and lands on the first point without one), as callers walking the
// points from the start rely on.
int CurveEditorComponent::selectNodeRelative(int delta) {
    if (selection_.step(delta > 0 ? 1 : -1, movableIndices(), kSelectionRules).has_value())
        selectionChanged();
    return cursorIndex();
}

int CurveEditorComponent::extendSelectionRelative(int delta) {
    if (selection_.extend(delta > 0 ? 1 : -1, movableIndices(), kSelectionRules).has_value())
        selectionChanged();
    return cursorIndex();
}

bool CurveEditorComponent::toggleSelected(int index) {
    if (index < 0 || index >= model_.getNumNodes() || !model_.getNode(index).isMovable())
        return false;
    std::vector<int> next = selection_.selected();
    if (isSelected(index)) {
        next.erase(std::remove(next.begin(), next.end(), index), next.end());
        const int cursor = cursorIndex() == index ? (next.empty() ? -1 : next.back()) : cursorIndex();
        setSelection(next, cursor, cursor);
    } else {
        next.push_back(index);
        setSelection(next, index, index);
    }
    return true;
}

bool CurveEditorComponent::selectAllMovable() {
    const auto before = selection_;
    if (!selection_.selectAll(movableIndices(), kSelectionRules))
        return false;
    if (selection_.selected() != before.selected() || selection_.cursor() != before.cursor() ||
        selection_.anchor() != before.anchor())
        selectionChanged();
    return true;
}

// The time axis stops at the range ends and at the nearest point that is not moving along time (Free mode only: a
// Fixed curve ripples instead); the level axis stops the group at the first point's own limit. Both through the
// shared group-move math.
CurveModel CurveEditorComponent::modelWithSelectionMoved(double dx, double dy, std::vector<int>& changed) const {
    const bool isFree = model_.getMode() == CurveMode::Free;
    std::vector<int> movingX;
    std::vector<int> movingY;
    for (const int i : selection_.selected()) {
        if (i < 0 || i >= model_.getNumNodes())
            continue;
        if (model_.getNode(i).xMovable)
            movingX.push_back(i);
        if (model_.getNode(i).yMovable)
            movingY.push_back(i);
    }

    std::vector<PointRoom> levelRooms;
    for (const int i : movingY) {
        const auto& node = model_.getNode(i);
        levelRooms.push_back(roomWithin(node.y, node.minY, node.maxY));
    }
    dy = clampGroupDelta(levelRooms, dy);

    if (isFree) {
        std::vector<double> xs;
        std::vector<bool> moving;
        for (int j = 0; j < model_.getNumNodes(); ++j) {
            xs.push_back(model_.getNode(j).x);
            moving.push_back(std::binary_search(movingX.begin(), movingX.end(), j));
        }
        dx = clampGroupDelta(roomBetweenNeighbours(xs, moving, model_.getMinX(), model_.getMaxX()), dx);
    }

    CurveModel moved = model_;
    for (const int i : movingY)
        moved.setNodeY(i, model_.getNode(i).y + (float)dy);
    if (isFree) {
        // The far end first, so a point never passes the one it is travelling toward.
        if (dx > 0.0)
            std::reverse(movingX.begin(), movingX.end());
        for (const int i : movingX)
            moved.setNodeX(i, model_.getNode(i).x + dx);
    } else if (!movingX.empty()) {
        // A Fixed curve ripples: moving the earliest point carries every later one along.
        moved.setNodeX(movingX.front(), model_.getNode(movingX.front()).x + dx);
    }

    for (int i = 0; i < moved.getNumNodes(); ++i)
        if (moved.getNode(i).x != model_.getNode(i).x || moved.getNode(i).y != model_.getNode(i).y)
            changed.push_back(i);
    return moved;
}

void CurveEditorComponent::commitSelectionMove(CurveModel moved, const std::vector<int>& changed) {
    model_ = std::move(moved);
    if (onNodeChanged)
        for (const int i : changed)
            onNodeChanged(i);
    repaint();
    refreshAccessibilityValue();
}

void CurveEditorComponent::dragSelectionTo(int index, juce::Point<float> point, bool bypassSnap) {
    if (index < 0 || index >= model_.getNumNodes())
        return;
    const auto geometry = currentGeometry();
    const auto target =
        snapModelPoint(geometry, {geometry.timeForX(point.x), (double)geometry.levelForY(point.y)}, bypassSnap);
    const auto& lead = model_.getNode(index);
    std::vector<int> changed;
    CurveModel moved = modelWithSelectionMoved(target.x - lead.x, target.y - lead.y, changed);
    if (!changed.empty())
        commitSelectionMove(std::move(moved), changed);
}

bool CurveEditorComponent::removeSelectedNodes() {
    if (model_.getMode() != CurveMode::Free || selection_.isEmpty())
        return false;
    CurveModel probe = model_;
    std::vector<int> removed;
    const auto& selected = selection_.selected();
    for (auto it = selected.rbegin(); it != selected.rend(); ++it)
        if (probe.removePoint(*it))
            removed.push_back(*it);
    if (removed.empty())
        return false;

    int cursor = cursorIndex();
    for (const int r : removed)
        if (r < cursorIndex())
            --cursor;
    beginGesture();
    model_ = std::move(probe);
    const int count = model_.getNumNodes();
    int next = -1;
    for (int i = std::max(cursor, 0); i < count && next < 0; ++i)
        if (model_.getNode(i).isMovable())
            next = i;
    for (int i = std::min(cursor, count) - 1; i >= 0 && next < 0; --i)
        if (model_.getNode(i).isMovable())
            next = i;
    setSelectedIndex(next);
    if (onPointsChanged)
        onPointsChanged();
    endGesture();
    repaint();
    return true;
}

} // namespace synth::ui
