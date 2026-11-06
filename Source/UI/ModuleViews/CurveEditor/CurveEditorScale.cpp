// CurveEditorScale.cpp -- Ctrl+Alt+arrows on a curve: the selected points scaled about their middle, in time or level,
// as one gesture (the same bracket as a nudge, so one undo step per press). The math is shared with the automation
// lanes (PointScale.h). The class is declared in CurveEditorComponent.h.

#include "CurveEditorComponent.h"

#include "UI/Layout/PointEditing/PointGroupMove.h"
#include "UI/Layout/PointEditing/PointScale.h"
#include <algorithm>

namespace synth::ui {

bool CurveEditorComponent::scaleSelection(bool time, int side, bool fine) {
    // Only the points that can move along the axis take part; a Fixed curve ripples its times, so it scales levels
    // only.
    std::vector<int> picked;
    for (const int i : selection_.selected())
        if (i >= 0 && i < model_.getNumNodes() && (time ? model_.getNode(i).xMovable : model_.getNode(i).yMovable))
            picked.push_back(i);
    if (picked.size() < 2 || (time && model_.getMode() != CurveMode::Free))
        return false;

    const double factor = pointScaleFactor(side, fine);
    CurveModel moved = model_;
    if (time) {
        const int count = model_.getNumNodes();
        std::vector<double> xs;
        std::vector<bool> moving;
        for (int j = 0; j < count; ++j) {
            xs.push_back(model_.getNode(j).x);
            moving.push_back(std::binary_search(picked.begin(), picked.end(), j));
        }
        const double gap = (model_.getMaxX() - model_.getMinX()) * 1e-4;
        std::vector<double> at;
        std::vector<ScaleLimits> limits;
        const auto rooms = roomBetweenNeighbours(xs, moving, model_.getMinX() - gap, model_.getMaxX() + gap);
        for (std::size_t k = 0; k < picked.size(); ++k) {
            const double x = model_.getNode(picked[k]).x;
            at.push_back(x);
            limits.push_back({x + rooms[k].below + gap, x + rooms[k].above - gap});
        }
        const auto scaled = scalePositionsAbout(at, limits, gap, factor);
        // The points that move right go first from the far end, then those that move left from the near end, so none
        // passes another on the way and the indices stay put.
        for (std::size_t k = picked.size(); k-- > 0;)
            if (scaled[k] > at[k])
                moved.setNodeX(picked[k], scaled[k]);
        for (std::size_t k = 0; k < picked.size(); ++k)
            if (scaled[k] < at[k])
                moved.setNodeX(picked[k], scaled[k]);
    } else {
        std::vector<double> ys;
        std::vector<ScaleLimits> limits;
        for (const int i : picked) {
            ys.push_back(model_.getNode(i).y);
            limits.push_back({model_.getNode(i).minY, model_.getNode(i).maxY});
        }
        const auto scaled = scaleValuesAbout(ys, limits, factor);
        for (std::size_t k = 0; k < picked.size(); ++k)
            moved.setNodeY(picked[k], (float)scaled[k]);
    }

    std::vector<int> changed;
    for (int i = 0; i < moved.getNumNodes(); ++i)
        if (moved.getNode(i).x != model_.getNode(i).x || moved.getNode(i).y != model_.getNode(i).y)
            changed.push_back(i);
    if (changed.empty())
        return true; // consumed, but nothing moved: no gesture, no undo step

    const auto before = selectionPositions();
    const auto selectedBefore = selection_.selected();
    beginGesture();
    commitSelectionMove(std::move(moved), changed);
    endGesture();
    const auto after = selectionPositions();
    if (after.size() == before.size())
        for (std::size_t k = 0; k < after.size(); ++k)
            pointTween_.retarget(selectedBefore[k], pointTween_.drawnAt(selectedBefore[k], before[k]), after[k]);
    return true;
}

} // namespace synth::ui
