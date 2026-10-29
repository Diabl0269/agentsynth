// CurveEditorClipboard.cpp -- copy, paste and duplicate of the selected points of a
// CurveEditorComponent. The class is declared in CurveEditorComponent.h.
//
// The clipboard is one process-wide shared PointClip (message thread only) so a group copied from one curve can be
// pasted into another. A group is stored as spacing from its first point, so it lands anywhere.

#include "CurveEditorComponent.h"
#include "UI/Layout/PointEditorDuplicate.h"

#include <algorithm>

namespace synth::ui {

namespace {
// Where a pasted group starts after the point it follows, as a fraction of the curve's time range, so
// the two never share an x.
constexpr double kPasteGapFraction = 0.02;
// Two points closer than this in time are the same point, so an insert between them is refused.
constexpr double kMinInsertGap = 1e-9;
} // namespace

CurveEditorComponent::Clip& CurveEditorComponent::clipboard() {
    static Clip clip;
    return clip;
}

// Only a bend between two copied neighbours travels with the group.
CurveEditorComponent::Clip CurveEditorComponent::selectionAsClip() const {
    const auto& selected = selection_.selected();
    std::vector<double> xs, levels;
    std::vector<ClipBend> bends;
    for (const int i : selected) {
        if (i < 0 || i >= model_.getNumNodes())
            continue;
        xs.push_back(model_.getNode(i).x);
        levels.push_back(model_.getNode(i).y);
        ClipBend bend;
        const bool followedByCopied = std::binary_search(selected.begin(), selected.end(), i + 1);
        if (followedByCopied && i < model_.getNumSegments()) {
            bend.bend = model_.getBend(i);
            bend.hasBend = true;
        }
        bends.push_back(bend);
    }
    return makePointClip(xs, levels, bends);
}

bool CurveEditorComponent::copySelection() {
    auto clip = selectionAsClip();
    if (clip.isEmpty())
        return false;
    clipboard() = std::move(clip);
    return true;
}

bool CurveEditorComponent::insertClip(const Clip& clip, double startX) {
    const auto& points = clip.entries;
    const int room = CurveModel::kMaxNodes - model_.getNumNodes();
    if (model_.getMode() != CurveMode::Free || points.empty() || room <= 0)
        return false;
    const auto count = std::min((size_t)room, points.size());
    const double minX = model_.getMinX();
    const double maxX = model_.getMaxX();
    const double span = points[count - 1].offset;
    const double scale = span > maxX - minX && span > 0.0 ? (maxX - minX) / span : 1.0;
    const double start = juce::jlimit(minX, std::max(minX, maxX - span * scale), startX);

    beginGesture();
    std::vector<int> added;
    for (size_t k = 0; k < count; ++k) {
        const int index =
            model_.addPoint(start + points[k].offset * scale, juce::jlimit(0.0f, 1.0f, (float)points[k].value));
        if (index < 0)
            break;
        if (points[k].extra.hasBend && index < model_.getNumSegments())
            model_.setBend(index, points[k].extra.bend);
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
    if (model_.getMode() != CurveMode::Free || clipboard().isEmpty())
        return false;
    const double gap = (model_.getMaxX() - model_.getMinX()) * kPasteGapFraction;
    const bool hasCursor = cursorIndex() >= 0 && cursorIndex() < model_.getNumNodes();
    return insertClip(clipboard(), hasCursor ? model_.getNode(cursorIndex()).x + gap : model_.getMinX());
}

bool CurveEditorComponent::duplicateSelection() {
    if (model_.getMode() != CurveMode::Free || selection_.isEmpty())
        return false;
    const auto clip = selectionAsClip();
    const double range = model_.getMaxX() - model_.getMinX();
    std::vector<double> xs;
    for (const int i : selection_.selected())
        if (i >= 0 && i < model_.getNumNodes())
            xs.push_back(model_.getNode(i).x);
    if (xs.empty())
        return false;
    DuplicateSteps steps;
    steps.singleStep = range * kPasteGapFraction;
    steps.tick = range * tickBeats();
    if (snapToGrid_ && grid_.has_value())
        steps.grid = grid_->xStep > 0.0 ? grid_->xStep : range / (double)juce::jmax(1, grid_->xDivisions);
    return insertClip(clip, duplicateAnchor(duplicatePlacement(), xs, steps));
}

// Why a Ctrl+T press cannot insert, in words for the user; empty when it can.
juce::String CurveEditorComponent::insertRefusal() const {
    if (model_.getMode() != CurveMode::Free)
        return "Points cannot be added to this curve";
    if (cursorIndex() < 0 || cursorIndex() >= model_.getNumNodes())
        return "Pick a point first";
    if (model_.getNumNodes() >= CurveModel::kMaxNodes)
        return "No room: the curve has the most points it can hold";
    const bool last = cursorIndex() + 1 >= model_.getNumNodes();
    const double toX = last ? model_.getMaxX() : model_.getNode(cursorIndex() + 1).x;
    if (toX - model_.getNode(cursorIndex()).x <= kMinInsertGap)
        return "No room after this point";
    return {};
}

// Halfway between the cursor point and the next one (or the end of the range when it is the last), at the level
// halfway between them. A refused press returns before beginGesture(), so it never opens an empty undo step.
bool CurveEditorComponent::insertAfterCursor() {
    if (insertRefusal().isNotEmpty())
        return false;
    const CurveNode& from = model_.getNode(cursorIndex());
    const bool last = cursorIndex() + 1 >= model_.getNumNodes();
    const double toX = last ? model_.getMaxX() : model_.getNode(cursorIndex() + 1).x;
    const float toY = last ? from.y : model_.getNode(cursorIndex() + 1).y;

    beginGesture();
    const int index = model_.addPoint(from.x + (toX - from.x) * 0.5, from.y + (toY - from.y) * 0.5f);
    if (index >= 0)
        setSelection({index}, index, index);
    if (onPointsChanged)
        onPointsChanged();
    endGesture();
    repaint();
    return index >= 0;
}

} // namespace synth::ui
