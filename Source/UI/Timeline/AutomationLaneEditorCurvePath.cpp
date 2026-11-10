// AutomationLaneEditor -- the curve's path: the kernel real playback uses, sampled every two pixels and at the exact
// place of every point, so a step narrower than a sample (two points a tick apart, a Hold) is drawn as a true
// vertical step instead of a ramp across the sample. The class is declared in AutomationLaneEditor.h.

#include "AutomationLaneEditor.h"
#include "Timeline/AutomationKernel.h"
#include <algorithm>

namespace synth::ui {

namespace {
constexpr int kCurveSamplePx = 2;
// A point's value is read here just before its beat as well as at it: far below the tick a duplicate leaves between
// two points, and far above a double's rounding at any beat a song reaches.
constexpr double kJustBeforePointBeats = 1e-7;

struct CurveStop {
    double x = 0.0;
    double beat = 0.0;
    bool point = false;
};
} // namespace

// Every sample and every point inside the width, in x order. A point adds two vertices at its x, the value just before
// it and its own, so the segment arriving at it and the one leaving it meet at its exact place.
juce::Path AutomationLaneEditor::curvePath(const std::vector<LaneBreakpoint>& points,
                                           const synth::AutomationLane& lane) const {
    std::vector<TimelineSnapshot::Point> pts;
    pts.reserve(points.size());
    for (const auto& bp : points)
        pts.push_back({bp.beat, bp.value, bp.tension, bp.curve});

    const int width = getWidth();
    std::vector<CurveStop> stops;
    stops.reserve((std::size_t)(width / kCurveSamplePx + 1) + points.size());
    for (int x = 0; x < width; x += kCurveSamplePx)
        stops.push_back({(double)x, viewState_.xToBeat((double)x), false});
    for (const auto& bp : points) {
        const double x = viewState_.beatToX(bp.beat);
        if (x > 0.0 && x < (double)width)
            stops.push_back({x, bp.beat, true});
    }
    // A point on a sample's column goes first: its value just before, then the value the sample reads as well.
    std::stable_sort(stops.begin(), stops.end(), [](const CurveStop& a, const CurveStop& b) {
        return a.x < b.x || (a.x == b.x && a.point && !b.point);
    });

    juce::Path path;
    AutomationCursor cursor{};
    const double flat = dragMode_ == DragMode::LaneConstant ? previewValue_ : (double)lane.range.defaultValue;
    const auto yAt = [&](double beat) {
        const double value = pts.empty() ? flat
                                         : AutomationKernel::evaluate(pts.data(), (int)pts.size(), beat,
                                                                      (double)lane.range.defaultValue, cursor);
        return (float)valueToY(value);
    };
    // Path::isEmpty stays true across a flat start, so the first vertex is tracked here.
    bool started = false;
    const auto add = [&path, &started](float x, float y) {
        if (started)
            path.lineTo(x, y);
        else
            path.startNewSubPath(x, y);
        started = true;
    };
    for (const auto& stop : stops) {
        if (stop.point)
            add((float)stop.x, yAt(stop.beat - kJustBeforePointBeats));
        add((float)stop.x, yAt(stop.beat));
    }
    return path;
}

} // namespace synth::ui
