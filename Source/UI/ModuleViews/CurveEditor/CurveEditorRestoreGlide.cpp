// CurveEditorComponent -- an undo or redo that moves the nodes of a same-shaped curve: they are drawn gliding from
// where they were (KeyedPointTween). One that changes the number of points (a drawn stretch undone or redone) has no
// node to glide, so the whole line morphs: both lines are sampled at every x either one has and the drawn level eases
// from the old line's to the new one's. The class is declared in CurveEditorComponent.h.

#include "CurveEditorComponent.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

std::vector<juce::Point<float>> CurveEditorComponent::nodePositions() const {
    std::vector<juce::Point<float>> positions;
    const auto geometry = currentGeometry();
    for (int i = 0; i < model_.getNumNodes(); ++i)
        positions.push_back(geometry.nodePosition(i));
    return positions;
}

// A node still gliding from an earlier step continues from where it is drawn, so back-to-back undos stay smooth.
void CurveEditorComponent::glideNodesFrom(const std::vector<juce::Point<float>>& before) {
    const auto after = nodePositions();
    if (before.size() != after.size())
        return;
    for (size_t i = 0; i < after.size(); ++i)
        pointTween_.retarget((KeyedPointTween::Key)i, pointTween_.drawnAt((KeyedPointTween::Key)i, before[i]), after[i],
                             true);
}

float CurveEditorComponent::drawnBend(int seg) const { return model_.getBend(seg) + bendTween_.offsetFor(seg).x; }

std::vector<float> CurveEditorComponent::drawnBends() const {
    std::vector<float> bends;
    for (int seg = 0; seg < model_.getNumSegments(); ++seg)
        bends.push_back(drawnBend(seg));
    return bends;
}

CurveModel CurveEditorComponent::drawnModel() const {
    CurveModel shown = model_;
    if (bendTween_.isAnimating())
        for (int seg = 0; seg < shown.getNumSegments(); ++seg)
            shown.setBend(seg, juce::jlimit(-1.0f, 1.0f, drawnBend(seg)));
    return shown;
}

// A segment's bend glides like the nodes beside it: from the bend drawn now (a bend still gliding from an earlier step
// continues from where it is) to the restored one, so the line between two gliding dots bows into place with them.
void CurveEditorComponent::glideBendsFrom(const std::vector<float>& before) {
    if ((int)before.size() != model_.getNumSegments())
        return;
    for (int seg = 0; seg < model_.getNumSegments(); ++seg) {
        const float now = model_.getBend(seg);
        if (std::abs(before[(size_t)seg] - now) < 1e-6f) {
            bendTween_.erase(seg);
            continue;
        }
        bendTween_.retarget(seg, {before[(size_t)seg], 0.0f}, {now, 0.0f}, true);
    }
}

namespace {

/** The level of the left-to-right line `points` at `x`: linear between its samples, held past its ends. Where the line
 *  steps straight up or down at `x`, the first sample there wins. */
float lineYAt(const std::vector<juce::Point<float>>& points, float x) {
    if (points.empty())
        return 0.0f;
    if (x <= points.front().x)
        return points.front().y;
    if (x >= points.back().x)
        return points.back().y;
    const auto after = std::lower_bound(points.begin(), points.end(), x,
                                        [](const juce::Point<float>& p, float value) { return p.x < value; });
    if (after->x == x || after == points.begin())
        return after->y;
    const auto before = std::prev(after);
    const float span = after->x - before->x;
    return span > 0.0f ? before->y + (after->y - before->y) * (x - before->x) / span : after->y;
}

} // namespace

// The weight runs 1 -> 0 through the shared tween, so the morph takes the app's glide time, eases like every other
// restore and is skipped under Animations Off; Reduce Motion lands the new line and fades it in (alphaFor).
void CurveEditorComponent::morphFrom(std::vector<juce::Point<float>> before) {
    if (before.empty()) {
        morphTween_.clear();
        morphFrom_.clear();
        return;
    }
    morphFrom_ = std::move(before);
    morphTween_.retarget(0, {0.0f, 1.0f}, {0.0f, 0.0f}, true);
}

float CurveEditorComponent::morphWeight() const {
    return morphFrom_.empty() ? 0.0f : juce::jlimit(0.0f, 1.0f, morphTween_.offsetFor(0).y);
}

// Every sample of either line is kept, in x order, so the steps of a drawn stretch and the corners of the old curve
// both show while the two blend; a step's two samples keep their order.
std::vector<juce::Point<float>> CurveEditorComponent::drawnCurvePoints(const CurveEditorGeometry& geometry) const {
    auto now = modelCurvePoints(geometry);
    const float w = morphWeight();
    if (w <= 0.0f || now.empty())
        return now;
    std::vector<juce::Point<float>> out;
    out.reserve(now.size() + morphFrom_.size());
    size_t i = 0, j = 0;
    while (i < morphFrom_.size() || j < now.size()) {
        const bool takeOld = j >= now.size() || (i < morphFrom_.size() && morphFrom_[i].x < now[j].x);
        if (takeOld) {
            const auto& p = morphFrom_[i++];
            out.push_back({p.x, lineYAt(now, p.x) * (1.0f - w) + p.y * w});
        } else {
            const auto& p = now[j++];
            out.push_back({p.x, p.y * (1.0f - w) + lineYAt(morphFrom_, p.x) * w});
        }
    }
    return out;
}

} // namespace synth::ui
