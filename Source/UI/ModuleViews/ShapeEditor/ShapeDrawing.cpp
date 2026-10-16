// ShapeDrawing.cpp -- lane draw shapes as shape curves.
#include "ShapeDrawing.h"
#include "UI/Timeline/AutomationLanes/LaneShapes/LaneShapeGenerator.h"

namespace synth::ui {

/** A lane breakpoint with the Hold curve keeps its value until the next beat; a curve point has no such mode, so
    the hold is drawn as a step: a second point at the next point's x with the held value. LfoCustomWave reads
    equal-x points as a vertical step. */
LfoCustomWave curveForDrawShape(DrawShape shape, const LfoCustomWave& current, const ShapeDrawRange& range) {
    LfoCustomWave out = current;
    if (shape == DrawShape::Free)
        return out;
    if (shape == DrawShape::Line) {
        const float first = current.points.empty() ? range.hi : current.points.front().y;
        const float last = current.points.empty() ? range.hi : current.points.back().y;
        out.points = {{0.0f, first, 0.0f}, {1.0f, last, 0.0f}};
        out.sanitise();
        return out;
    }

    const int cycles = juce::jmax(1, range.cycles);
    const auto points = generateShapePoints(shape, 0.0, 1.0, 1.0 / (double)cycles, (double)range.lo, (double)range.hi);
    if (points.size() < 2)
        return out;
    out.points.clear();
    const int hold = static_cast<int>(synth::BreakpointCurve::Hold);
    for (size_t k = 0; k < points.size(); ++k) {
        out.points.push_back({(float)points[k].beat, (float)points[k].value, 0.0f});
        if (points[k].curve == hold && k + 1 < points.size())
            out.points.push_back({(float)points[k + 1].beat, (float)points[k].value, 0.0f});
    }
    out.sanitise();
    return out;
}

} // namespace synth::ui
