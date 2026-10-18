// CurveEditorClipboard.cpp -- copy, paste and duplicate of the selected points of a
// CurveEditorComponent. The class is declared in CurveEditorComponent.h.
//
// The clipboard is one process-wide list (message thread only) so a group copied from one curve can be
// pasted into another. A group is stored as spacing from its first point, so it lands anywhere.

#include "CurveEditorComponent.h"

#include <algorithm>

namespace synth::ui {

namespace {
// Where a pasted group starts after the point it follows, as a fraction of the curve's time range, so
// the two never share an x.
constexpr double kPasteGapFraction = 0.02;
} // namespace

std::vector<CurveEditorComponent::ClipPoint>& CurveEditorComponent::clipboard() {
    static std::vector<ClipPoint> points;
    return points;
}

std::vector<CurveEditorComponent::ClipPoint> CurveEditorComponent::selectionAsClip() const {
    std::vector<ClipPoint> clip;
    for (const int i : selection_) {
        if (i < 0 || i >= model_.getNumNodes())
            continue;
        ClipPoint point;
        point.dx = model_.getNode(i).x - model_.getNode(selection_.front()).x;
        point.y = model_.getNode(i).y;
        // Only a bend between two copied neighbours travels with the group.
        const bool followedByCopied = std::binary_search(selection_.begin(), selection_.end(), i + 1);
        if (followedByCopied && i < model_.getNumSegments()) {
            point.bend = model_.getBend(i);
            point.hasBend = true;
        }
        clip.push_back(point);
    }
    return clip;
}

bool CurveEditorComponent::copySelection() {
    auto clip = selectionAsClip();
    if (clip.empty())
        return false;
    clipboard() = std::move(clip);
    return true;
}

bool CurveEditorComponent::insertClip(const std::vector<ClipPoint>& points, double startX) {
    const int room = CurveModel::kMaxNodes - model_.getNumNodes();
    if (model_.getMode() != CurveMode::Free || points.empty() || room <= 0)
        return false;
    const auto count = std::min((size_t)room, points.size());
    const double minX = model_.getMinX();
    const double maxX = model_.getMaxX();
    const double span = points[count - 1].dx;
    const double scale = span > maxX - minX && span > 0.0 ? (maxX - minX) / span : 1.0;
    const double start = juce::jlimit(minX, std::max(minX, maxX - span * scale), startX);

    beginGesture();
    std::vector<int> added;
    for (size_t k = 0; k < count; ++k) {
        const int index = model_.addPoint(start + points[k].dx * scale, juce::jlimit(0.0f, 1.0f, points[k].y));
        if (index < 0)
            break;
        if (points[k].hasBend && index < model_.getNumSegments())
            model_.setBend(index, points[k].bend);
        added.push_back(index);
    }
    if (!added.empty()) {
        const int first = added.front();
        const int last = added.back();
        setSelection(std::move(added), last, first);
    }
    if (onPointsChanged)
        onPointsChanged();
    endGesture();
    repaint();
    return true;
}

bool CurveEditorComponent::pasteAtCursor() {
    if (model_.getMode() != CurveMode::Free || clipboard().empty())
        return false;
    const double gap = (model_.getMaxX() - model_.getMinX()) * kPasteGapFraction;
    const bool hasCursor = selectedIndex_ >= 0 && selectedIndex_ < model_.getNumNodes();
    return insertClip(clipboard(), hasCursor ? model_.getNode(selectedIndex_).x + gap : model_.getMinX());
}

bool CurveEditorComponent::duplicateSelection() {
    if (model_.getMode() != CurveMode::Free || selection_.empty())
        return false;
    const auto clip = selectionAsClip();
    const double gap = (model_.getMaxX() - model_.getMinX()) * kPasteGapFraction;
    return insertClip(clip, model_.getNode(selection_.back()).x + gap);
}

} // namespace synth::ui
