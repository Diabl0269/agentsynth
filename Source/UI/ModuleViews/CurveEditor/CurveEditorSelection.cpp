// CurveEditorSelection.cpp -- the set of selected points of a CurveEditorComponent and the edits that
// act on all of it at once: extend, toggle, rigid move, remove. The class is declared in
// CurveEditorComponent.h.
//
// The selection is a sorted set of node indices plus a cursor (the point the keyboard continues from
// and a screen reader reads) and an anchor (where a Shift-extended range started). A fully pinned node
// is never in it.

#include "CurveEditorComponent.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace synth::ui {

bool CurveEditorComponent::isSelected(int index) const {
    return std::binary_search(selection_.begin(), selection_.end(), index);
}

void CurveEditorComponent::setSelection(std::vector<int> indices, int cursor, int anchor) {
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    if (indices == selection_ && cursor == selectedIndex_ && anchor == anchorIndex_)
        return;
    selection_ = std::move(indices);
    selectedIndex_ = cursor;
    anchorIndex_ = anchor;
    repaint();
    refreshAccessibilityValue();
}

void CurveEditorComponent::setSelectedIndex(int index) {
    setSelection(index >= 0 ? std::vector<int>{index} : std::vector<int>{}, index, index);
}

void CurveEditorComponent::remapSelectionForMove(int from, int to) {
    const auto remap = [from, to](int j) {
        if (j == from)
            return to;
        if (from < to && j > from && j <= to)
            return j - 1;
        if (to < from && j >= to && j < from)
            return j + 1;
        return j;
    };
    std::vector<int> moved;
    for (const int j : selection_)
        moved.push_back(remap(j));
    selectedIndex_ = selectedIndex_ >= 0 ? remap(selectedIndex_) : -1;
    anchorIndex_ = anchorIndex_ >= 0 ? remap(anchorIndex_) : -1;
    std::sort(moved.begin(), moved.end());
    selection_ = std::move(moved);
}

int CurveEditorComponent::selectNodeRelative(int delta) {
    const int count = model_.getNumNodes();
    const bool fromStart = selectedIndex_ < 0;
    const int step = (fromStart || delta > 0) ? 1 : -1;
    for (int candidate = fromStart ? 0 : selectedIndex_ + step; candidate >= 0 && candidate < count;
         candidate += step) {
        if (model_.getNode(candidate).isMovable()) {
            setSelectedIndex(candidate);
            repaint();
            return candidate;
        }
    }
    return selectedIndex_;
}

int CurveEditorComponent::extendSelectionRelative(int delta) {
    if (selectedIndex_ < 0)
        return selectNodeRelative(delta);
    const int count = model_.getNumNodes();
    const int step = delta > 0 ? 1 : -1;
    for (int candidate = selectedIndex_ + step; candidate >= 0 && candidate < count; candidate += step) {
        if (!model_.getNode(candidate).isMovable())
            continue;
        const int anchor = anchorIndex_ >= 0 ? anchorIndex_ : selectedIndex_;
        std::vector<int> range;
        for (int i = std::min(anchor, candidate); i <= std::max(anchor, candidate); ++i)
            if (model_.getNode(i).isMovable())
                range.push_back(i);
        setSelection(std::move(range), candidate, anchor);
        return candidate;
    }
    return selectedIndex_;
}

bool CurveEditorComponent::toggleSelected(int index) {
    if (index < 0 || index >= model_.getNumNodes() || !model_.getNode(index).isMovable())
        return false;
    std::vector<int> next = selection_;
    if (isSelected(index)) {
        next.erase(std::remove(next.begin(), next.end(), index), next.end());
        const int cursor = selectedIndex_ == index ? (next.empty() ? -1 : next.back()) : selectedIndex_;
        setSelection(next, cursor, cursor);
    } else {
        next.push_back(index);
        setSelection(next, index, index);
    }
    return true;
}

bool CurveEditorComponent::selectAllMovable() {
    std::vector<int> all;
    for (int i = 0; i < model_.getNumNodes(); ++i)
        if (model_.getNode(i).isMovable())
            all.push_back(i);
    if (all.empty())
        return false;
    const int cursor = isSelected(selectedIndex_) ? selectedIndex_ : all.back();
    setSelection(all, cursor, all.front());
    return true;
}

CurveModel CurveEditorComponent::modelWithSelectionMoved(double dx, double dy, std::vector<int>& changed) const {
    const bool isFree = model_.getMode() == CurveMode::Free;
    std::vector<int> movingX;
    std::vector<int> movingY;
    for (const int i : selection_) {
        if (i < 0 || i >= model_.getNumNodes())
            continue;
        if (model_.getNode(i).xMovable)
            movingX.push_back(i);
        if (model_.getNode(i).yMovable)
            movingY.push_back(i);
    }
    const auto isMovingX = [&movingX](int j) { return std::binary_search(movingX.begin(), movingX.end(), j); };

    double dyLo = -std::numeric_limits<double>::infinity();
    double dyHi = std::numeric_limits<double>::infinity();
    for (const int i : movingY) {
        const auto& node = model_.getNode(i);
        dyLo = std::max(dyLo, (double)node.minY - node.y);
        dyHi = std::min(dyHi, (double)node.maxY - node.y);
    }
    dy = juce::jlimit(std::min(dyLo, 0.0), std::max(dyHi, 0.0), dy);

    if (isFree) {
        // Rigid: stop at the range ends and at the nearest point that is not moving along time.
        double dxLo = -std::numeric_limits<double>::infinity();
        double dxHi = std::numeric_limits<double>::infinity();
        for (const int i : movingX) {
            double lower = model_.getMinX();
            for (int j = i - 1; j >= 0; --j)
                if (!isMovingX(j)) {
                    lower = model_.getNode(j).x;
                    break;
                }
            double upper = model_.getMaxX();
            for (int j = i + 1; j < model_.getNumNodes(); ++j)
                if (!isMovingX(j)) {
                    upper = model_.getNode(j).x;
                    break;
                }
            dxLo = std::max(dxLo, lower - model_.getNode(i).x);
            dxHi = std::min(dxHi, upper - model_.getNode(i).x);
        }
        dx = juce::jlimit(std::min(dxLo, 0.0), std::max(dxHi, 0.0), dx);
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
    if (model_.getMode() != CurveMode::Free || selection_.empty())
        return false;
    CurveModel probe = model_;
    std::vector<int> removed;
    for (auto it = selection_.rbegin(); it != selection_.rend(); ++it)
        if (probe.removePoint(*it))
            removed.push_back(*it);
    if (removed.empty())
        return false;

    int cursor = selectedIndex_;
    for (const int r : removed)
        if (r < selectedIndex_)
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
