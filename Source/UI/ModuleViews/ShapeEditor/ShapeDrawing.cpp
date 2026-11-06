// ShapeDrawing.cpp -- lane draw shapes as shape curves: drawOnCurve and the strokes the mouse and the keyboard make.
#include "ShapeDrawing.h"
#include "Timeline/AutomationRecorder.h"
#include "UI/Timeline/AutomationLanes/LaneShapes/LaneShapeGenerator.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {

constexpr double kEps = 1e-9;
/** Generating more than this many points is never worth it: the curve holds LfoCustomWave::kMaxPoints. */
constexpr long long kGenerateLimit = 4096;

using Point = LfoCustomWave::Point;

/** A lane breakpoint with the Hold curve keeps its value until the next beat; a curve point has no such mode, so
    the hold is drawn as a step: a second point at the next point's x with the held value. LfoCustomWave reads
    equal-x points as a vertical step. */
std::vector<Point> periodicPoints(const ShapeDrawStroke& s, double cycleWidth) {
    const auto points = generateShapePoints(s.shape, s.x0, s.x1, cycleWidth, (double)s.lo, (double)s.hi);
    std::vector<Point> out;
    const int hold = static_cast<int>(synth::BreakpointCurve::Hold);
    for (size_t k = 0; k < points.size(); ++k) {
        out.push_back({(float)points[k].beat, (float)points[k].value, 0.0f});
        if (points[k].curve == hold && k + 1 < points.size())
            out.push_back({(float)points[k + 1].beat, (float)points[k].value, 0.0f});
    }
    return out;
}

/** The mouse samples thinned with the lane Pencil's rule; epsilon is the same fraction of the (0..1) level range. */
std::vector<Point> freehandPoints(const ShapeDrawStroke& s, double epsilon) {
    auto samples = s.freehand;
    std::stable_sort(samples.begin(), samples.end(), [](const Point& a, const Point& b) { return a.x < b.x; });
    std::vector<synth::AutomationRecorder::CapturedPoint> raw;
    for (const auto& p : samples) {
        if (!raw.empty() && std::abs(raw.back().beat - (double)p.x) < kEps)
            raw.back().value = (double)p.y; // equal x: the latest sample wins
        else
            raw.push_back({(double)p.x, (double)p.y});
    }
    if (raw.size() < 2)
        return {};
    const auto thinned = synth::AutomationRecorder::thinPoints(raw, epsilon);
    std::vector<Point> out;
    for (const auto& p : thinned)
        out.push_back({(float)p.beat, (float)p.value, 0.0f});
    return out;
}

/** `current` with its points inside [x0, x1] swapped for `drawn`. */
LfoCustomWave spliced(const LfoCustomWave& current, const std::vector<Point>& drawn, double x0, double x1) {
    LfoCustomWave out;
    for (const auto& p : current.points)
        if ((double)p.x < x0 - kEps || (double)p.x > x1 + kEps)
            out.points.push_back(p);
    out.points.insert(out.points.end(), drawn.begin(), drawn.end());
    std::stable_sort(out.points.begin(), out.points.end(), [](const Point& a, const Point& b) { return a.x < b.x; });
    return out;
}

} // namespace

ShapeDrawResult drawOnCurve(const LfoCustomWave& current, const ShapeDrawStroke& s) {
    ShapeDrawResult result;
    result.curve = current;
    const auto finish = [&](LfoCustomWave wave, double cycles) {
        wave.sanitise();
        result.changed = !(wave == current);
        result.curve = result.changed ? wave : current;
        result.cycles = cycles;
        return result;
    };
    const auto tooMany = [&] {
        result.refusal = "Too many points for one shape (" + juce::String(LfoCustomWave::kMaxPoints) +
                         " at most): draw a shorter stretch";
        return result;
    };

    if (s.shape == DrawShape::Free) {
        // A long squiggle is thinned harder until it fits the point cap, instead of being refused.
        for (double eps = synth::AutomationRecorder::kThinningEpsilonFraction; eps < 1.0; eps *= 2.0) {
            const auto drawn = freehandPoints(s, eps);
            if (drawn.size() < 2)
                return result;
            auto wave = spliced(current, drawn, (double)drawn.front().x, (double)drawn.back().x);
            if ((int)wave.points.size() <= LfoCustomWave::kMaxPoints)
                return finish(std::move(wave), 0.0);
        }
        return tooMany();
    }

    const double span = s.x1 - s.x0;
    if (span <= kEps)
        return result;
    if (s.shape == DrawShape::Line) {
        auto wave = spliced(current, {{(float)s.x0, s.y0, 0.0f}, {(float)s.x1, s.y1, 0.0f}}, s.x0, s.x1);
        return (int)wave.points.size() > LfoCustomWave::kMaxPoints ? tooMany() : finish(std::move(wave), 0.0);
    }
    if (!isPeriodicShape(s.shape) || s.cycleWidth <= kEps)
        return result;

    // The asked-for cycle width first; when its points do not fit, the most whole cycles that do.
    const double asked = span / s.cycleWidth;
    std::vector<double> cycleCounts{asked};
    for (int k = (int)std::floor(asked - kEps); k >= 1; --k)
        if (std::abs((double)k - asked) > kEps)
            cycleCounts.push_back((double)k);
    for (const double cycles : cycleCounts) {
        const double width = span / cycles;
        if (estimateShapePointCount(s.shape, s.x0, s.x1, width) > kGenerateLimit)
            continue;
        const auto drawn = periodicPoints(s, width);
        if (drawn.size() < 2)
            continue;
        auto wave = spliced(current, drawn, s.x0, s.x1);
        if ((int)wave.points.size() <= LfoCustomWave::kMaxPoints)
            return finish(std::move(wave), cycles);
    }
    return tooMany();
}

ShapeDrawStroke spanStroke(DrawShape shape, const LfoCustomWave& current, const ShapeDrawRange& range, double x0,
                           double x1) {
    ShapeDrawStroke s;
    s.shape = shape;
    s.x0 = x0;
    s.x1 = x1;
    s.lo = range.lo;
    s.hi = range.hi;
    s.cycleWidth = (x1 - x0) / (double)juce::jmax(1, range.cycles);
    if (shape == DrawShape::Line) {
        s.y0 = current.evaluate((float)x0);
        s.y1 = current.evaluate((float)juce::jmin(x1, 0.999999));
        if (x1 >= 1.0 && !current.points.empty())
            s.y1 = current.points.back().y;
    }
    return s;
}

ShapeDrawStroke wholeCurveStroke(DrawShape shape, const LfoCustomWave& current, const ShapeDrawRange& range) {
    auto s = spanStroke(shape, current, range, 0.0, 1.0);
    if (shape == DrawShape::Line && !current.points.empty()) {
        s.y0 = current.points.front().y;
        s.y1 = current.points.back().y;
    }
    return s;
}

LfoCustomWave curveForDrawShape(DrawShape shape, const LfoCustomWave& current, const ShapeDrawRange& range) {
    if (shape == DrawShape::Free)
        return current;
    return drawOnCurve(current, wholeCurveStroke(shape, current, range)).curve;
}

} // namespace synth::ui
