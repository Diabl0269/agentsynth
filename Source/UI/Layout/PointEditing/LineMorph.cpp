#include "LineMorph.h"
#include <algorithm>
#include <cmath>
#include <iterator>

namespace synth::ui::line_morph {

float lineYAt(const Line& points, float x) {
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

// One x no longer names one level where the line steps straight up or down, so a blend by x would collapse the step.
bool hasVerticalStep(const Line& points) {
    for (size_t i = 1; i < points.size(); ++i)
        if (std::abs(points[i].x - points[i - 1].x) < 0.5f)
            return true;
    return false;
}

namespace {

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

/** The level arriving at `x` (from the left) and the level leaving it (to the right): equal except at a step, where
 *  they are the first and the last sample at that x. */
std::pair<float, float> levelsAt(const Line& points, float x) {
    if (points.empty())
        return {0.0f, 0.0f};
    const auto first = std::lower_bound(points.begin(), points.end(), x,
                                        [](const juce::Point<float>& p, float value) { return p.x < value; });
    const auto last = std::upper_bound(points.begin(), points.end(), x,
                                       [](float value, const juce::Point<float>& p) { return value < p.x; });
    if (first != last)
        return {first->y, std::prev(last)->y};
    const float y = lineYAt(points, x);
    return {y, y};
}

} // namespace

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

// Every sample of either line is kept, in x order, so the steps of a drawn stretch and the corners of the old curve
// both show while the two blend; a step's two samples keep their order.
Line blend(const Line& before, const Line& now, float w) {
    if (w <= 0.0f || now.empty() || before.empty())
        return now;
    if (hasVerticalStep(before) || hasVerticalStep(now))
        return blendByLength(before, now, w);
    Line out;
    out.reserve(now.size() + before.size());
    size_t i = 0, j = 0;
    while (i < before.size() || j < now.size()) {
        const bool takeOld = j >= now.size() || (i < before.size() && before[i].x < now[j].x);
        if (takeOld) {
            const auto& p = before[i++];
            out.push_back({p.x, lineYAt(now, p.x) * (1.0f - w) + p.y * w});
        } else {
            const auto& p = now[j++];
            out.push_back({p.x, p.y * (1.0f - w) + lineYAt(before, p.x) * w});
        }
    }
    return out;
}

// A lane's line is a level per x with jumps at its points (a Hold, a point's value just before it), so blending the
// arriving and the leaving level separately keeps each jump a jump while it eases, and no x ever shows a level outside
// the two lines' (a blend by length slides the line sideways when a peak goes).
Line blendAcrossSteps(const Line& before, const Line& now, float w) {
    if (w <= 0.0f || now.empty() || before.empty())
        return now;
    std::vector<float> xs;
    xs.reserve(before.size() + now.size());
    for (const auto* line : {&before, &now})
        for (const auto& p : *line)
            xs.push_back(p.x);
    std::sort(xs.begin(), xs.end());
    xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
    Line out;
    out.reserve(xs.size() + 8);
    for (const float x : xs) {
        const auto [wasIn, wasOut] = levelsAt(before, x);
        const auto [isIn, isOut] = levelsAt(now, x);
        const float in = isIn * (1.0f - w) + wasIn * w;
        const float leaving = isOut * (1.0f - w) + wasOut * w;
        out.push_back({x, in});
        if (leaving != in)
            out.push_back({x, leaving});
    }
    return out;
}

} // namespace synth::ui::line_morph
