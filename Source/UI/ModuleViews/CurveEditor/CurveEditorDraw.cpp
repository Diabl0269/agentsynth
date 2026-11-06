// CurveEditorDraw.cpp -- the curve editor's pen (docs/control/point-editing.md, "Draw tools"): the pieces the draw
// gesture asks the editor for (snapped model points, pixel positions, the grid cell, the preview path) and the one
// commit. The gesture itself is in CurveDrawGesture.cpp.
#include "CurveDrawGesture.h"
#include "CurveEditorComponent.h"
#include "CurveWaveConversion.h"
#include <cmath>

namespace synth::ui {

void CurveEditorComponent::setDrawPen(std::optional<DrawShape> pen) {
    draw_.setPen(pen);
    setMouseCursor(pen.has_value() ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor);
    repaint();
}

std::optional<DrawShape> CurveEditorComponent::getDrawPen() const noexcept { return draw_.getPen(); }

void CurveEditorComponent::setDrawBand(float lo, float hi, int cycles) { draw_.setBand({lo, hi, cycles}); }

bool CurveEditorComponent::isDrawing() const noexcept { return draw_.isActive(); }

/** The pixel position in model units, rounded to the grid unless `bypassSnap`, with the level clamped to 0..1 and the
 *  time to the curve's own extent. */
juce::Point<double> CurveEditorComponent::drawPointAt(juce::Point<float> pixel, bool bypassSnap) const {
    const auto geometry = currentGeometry();
    const juce::Point<double> raw{juce::jlimit(model_.getMinX(), model_.getMaxX(), geometry.timeForX(pixel.x)),
                                  (double)juce::jlimit(0.0f, 1.0f, geometry.levelForY(pixel.y))};
    return snapModelPoint(geometry, raw, bypassSnap);
}

juce::Point<float> CurveEditorComponent::pixelForDrawPoint(juce::Point<double> point) const {
    const auto geometry = currentGeometry();
    return {geometry.xForTime(point.x), geometry.yForLevel((float)point.y)};
}

/** One grid cell's width in model units while snapping to a grid; 0 with no grid (the shape then spreads its
 *  default number of cycles over the span). */
double CurveEditorComponent::drawCellWidth() const {
    if (!snapToGrid_ || !grid_.has_value())
        return 0.0;
    if (grid_->xStep > 0.0)
        return grid_->xStep;
    return (model_.getMaxX() - model_.getMinX()) / (double)juce::jmax(1, grid_->xDivisions);
}

/** The curve `wave` as a polyline on the editor's own axes, for the drag's preview. */
juce::Path CurveEditorComponent::drawPreviewPath(const LfoCustomWave& wave) const {
    const auto geometry = currentGeometry();
    const auto preview = curveModelFromWave(wave);
    constexpr int kSteps = 24;
    juce::Path path;
    bool first = true;
    for (int seg = 0; seg < preview.getNumSegments(); ++seg) {
        const double x0 = preview.getNode(seg).x;
        const double x1 = preview.getNode(seg + 1).x;
        for (int step = (seg == 0 ? 0 : 1); step <= kSteps; ++step) {
            const float progress = (float)step / (float)kSteps;
            const juce::Point<float> p{geometry.xForTime(x0 + (x1 - x0) * progress),
                                       geometry.yForLevel(preview.valueAt(seg, progress))};
            if (first)
                path.startNewSubPath(p);
            else
                path.lineTo(p);
            first = false;
        }
    }
    return path;
}

/** The drawn curve replaces the model as ONE gesture, through the callbacks a drag of a point runs, so the host
 *  writes it and records a single undo step. */
void CurveEditorComponent::commitDrawnCurve(const LfoCustomWave& wave) {
    beginGesture();
    setModel(curveModelFromWave(wave));
    if (onPointsChanged)
        onPointsChanged();
    endGesture();
}

void CurveEditorComponent::paintOverChildren(juce::Graphics& g) { draw_.paint(g); }

} // namespace synth::ui
