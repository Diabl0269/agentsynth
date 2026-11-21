// CurveEditorComponent -- an undo or redo that moves the nodes of a same-shaped curve: they are drawn gliding from
// where they were (KeyedPointTween). One that changes the number of points (a drawn stretch undone or redone) has no
// node to glide, so the whole line morphs: both lines are sampled at every x either one has and the drawn level eases
// from the old line's to the new one's. The class is declared in CurveEditorComponent.h.

#include "CurveEditorComponent.h"
#include <algorithm>
#include <cmath>
#include <iterator>

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

using Line = std::vector<juce::Point<float>>;

/** True when two neighbouring samples sit at the same x (within half a pixel): the line steps straight up or down, so
 *  one x no longer names one level and a blend by x would collapse the step. */
bool hasVerticalStep(const Line& points) {
    for (size_t i = 1; i < points.size(); ++i)
        if (std::abs(points[i].x - points[i - 1].x) < 0.5f)
            return true;
    return false;
}

/** Each sample's distance along the line as a fraction of its whole length; the index fraction when the line has no
 *  length (all samples on one spot). */
std::vector<float> lengthFractions(const Line& points) {
    std::vector<float> fraction(points.size(), 0.0f);
    float total = 0.0f;
    for (size_t i = 1; i < points.size(); ++i) {
        total += points[i].getDistanceFrom(points[i - 1]);
        fraction[i] = total;
    }
    for (size_t i = 0; i < points.size(); ++i)
        fraction[i] =
            total > 0.0f ? fraction[i] / total : (points.size() > 1 ? (float)i / (float)(points.size() - 1) : 0.0f);
    return fraction;
}

/** The point a fraction `t` of the way along the line whose samples sit at `fraction`. */
juce::Point<float> pointAtFraction(const Line& points, const std::vector<float>& fraction, float t) {
    const auto after = std::lower_bound(fraction.begin(), fraction.end(), t);
    if (after == fraction.begin())
        return points.front();
    if (after == fraction.end())
        return points.back();
    const size_t hi = (size_t)(after - fraction.begin());
    const float span = fraction[hi] - fraction[hi - 1];
    const float along = span > 0.0f ? (t - fraction[hi - 1]) / span : 1.0f;
    return points[hi - 1] + (points[hi] - points[hi - 1]) * along;
}

/** Both lines sampled at every fraction either has, each pair blended by `w` (1 = the old line). */
Line blendByLength(const Line& before, const Line& now, float w) {
    const auto fromFraction = lengthFractions(before);
    const auto nowFraction = lengthFractions(now);
    std::vector<float> breaks;
    breaks.reserve(fromFraction.size() + nowFraction.size());
    std::merge(fromFraction.begin(), fromFraction.end(), nowFraction.begin(), nowFraction.end(),
               std::back_inserter(breaks));
    breaks.erase(std::unique(breaks.begin(), breaks.end(), [](float a, float b) { return b - a < 1e-6f; }),
                 breaks.end());
    Line out;
    out.reserve(breaks.size());
    for (const float t : breaks) {
        const auto was = pointAtFraction(before, fromFraction, t);
        const auto is = pointAtFraction(now, nowFraction, t);
        out.push_back(is * (1.0f - w) + was * w);
    }
    return out;
}

} // namespace

// The weight runs 1 -> 0 through the shared tween, so the morph takes the app's glide time, eases like every other
// restore and is skipped under Animations Off; Reduce Motion lands the new line and fades it in (alphaFor).
void CurveEditorComponent::morphFrom(std::vector<juce::Point<float>> before) {
    if (before.empty()) {
        morphTween_.clear();
        morphFrom_.clear();
        editMorph_ = {};
        return;
    }
    morphFrom_ = std::move(before);
    morphTween_.retarget(0, {0.0f, 1.0f}, {0.0f, 0.0f}, true);
}

std::vector<juce::Point<float>> CurveEditorComponent::lineBeforeEdit() const {
    return drawnCurvePoints(currentGeometry());
}

// The one path for a changed number of points: an undo or redo (setModel), a delete and an add. Dots that glided a key
// move or a restore belong to the old shape, so they stop; the line eases from what was drawn into the new shape.
void CurveEditorComponent::glideLineFrom(std::vector<juce::Point<float>> before) {
    glideLineFrom(std::move(before), EditMorph{});
}

void CurveEditorComponent::glideLineFrom(std::vector<juce::Point<float>> before, EditMorph edit) {
    pointTween_.clear();
    bendTween_.clear();
    morphFrom(std::move(before));
    editMorph_ = edit;
}

// An undo or redo that changes the point count hides every dot and bend handle while the line morphs (the old dots are
// not the new ones). An add or delete keeps them: the dots that exist on both sides stay put and drawn while the line
// eases, so only the dot that was added, or the bend handle of a segment that merged or split, fades in. Reduce
// Motion's fade-in of the landed line (morphTween_.alphaFor) applies to what is new, never to what was already there.
float CurveEditorComponent::dotMorphFade(int node) const {
    if (editMorph_.dotsStay && node != editMorph_.newNode)
        return 1.0f;
    return (1.0f - morphWeight()) * morphTween_.alphaFor(0);
}

float CurveEditorComponent::dotMorphScale(int node) const {
    if (!editMorph_.dotsStay || node != editMorph_.newNode)
        return 1.0f;
    return juce::jlimit(0.0f, 1.0f, 1.0f - morphWeight());
}

float CurveEditorComponent::bendMorphFade(int seg) const {
    if (editMorph_.dotsStay && (seg < editMorph_.firstFadeSeg || seg > editMorph_.lastFadeSeg))
        return 1.0f;
    return (1.0f - morphWeight()) * morphTween_.alphaFor(0);
}

float CurveEditorComponent::morphWeight() const {
    return morphFrom_.empty() ? 0.0f : juce::jlimit(0.0f, 1.0f, morphTween_.offsetFor(0).y);
}

// Every sample of either line is kept, in x order, so the steps of a drawn stretch and the corners of the old curve
// both show while the two blend; a step's two samples keep their order. A line with a vertical step (dots stacked at
// one x) cannot be blended by x, so both lines blend by the fraction of their length instead.
std::vector<juce::Point<float>> CurveEditorComponent::drawnCurvePoints(const CurveEditorGeometry& geometry) const {
    auto now = modelCurvePoints(geometry);
    const float w = morphWeight();
    if (w <= 0.0f || now.empty())
        return now;
    if (hasVerticalStep(morphFrom_) || hasVerticalStep(now))
        return blendByLength(morphFrom_, now, w);
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
