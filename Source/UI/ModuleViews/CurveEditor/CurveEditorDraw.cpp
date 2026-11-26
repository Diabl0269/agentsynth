// CurveEditorDraw.cpp -- the curve editor's pen (docs/control/point-editing.md, "Draw tools"): the pieces the draw
// gesture asks the editor for (snapped model points, pixel positions, the grid cell, the preview path) and the one
// commit. The gesture itself is in CurveDrawGesture.cpp.
#include "CurveDrawGesture.h"
#include "CurveEditorComponent.h"
#include "CurveWaveConversion.h"
#include "UI/Timeline/AutomationLanes/LaneShapes/DrawShape.h"
#include "UI/Timeline/AutomationLanes/LaneToolCursors.h"
#include <cmath>

namespace synth::ui {

void CurveEditorComponent::setDrawPen(std::optional<DrawShape> pen) {
    draw_.setPen(pen);
    // While a pen is on, the tooltip describes the pen, not the point editing it replaces; the host's text returns
    // when the pen goes off.
    if (pen.has_value()) {
        if (!tooltipBeforePen_.has_value())
            tooltipBeforePen_ = getTooltip();
        setTooltip("Drag to draw a " + juce::String(drawShapeName(*pen)) +
                   " here; Esc cancels; Shift ignores the grid; Option+drag up or down, or [ and ], sets the cycles");
    } else if (tooltipBeforePen_.has_value()) {
        setTooltip(*tooltipBeforePen_);
        tooltipBeforePen_.reset();
    }
    setMouseCursor(juce::MouseCursor::NormalCursor);
    updateMouseCursor(); // the pen's cursor (or the arrow) shows at once, without a mouse move
    repaint();
}

CurveEditorComponent::~CurveEditorComponent() = default;

juce::MouseCursor CurveEditorComponent::getMouseCursor() {
    if (const auto pen = draw_.getPen()) {
        if (penCursors_ == nullptr)
            penCursors_ = std::make_unique<LaneToolCursors>();
        return penCursors_->get(*this, EditTool::Draw, pen);
    }
    return juce::Component::getMouseCursor();
}

void CurveEditorComponent::lookAndFeelChanged() {
    if (penCursors_ != nullptr)
        penCursors_->invalidate(); // re-tinted icons -> different cursor images
    updateMouseCursor();
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
    if (!snapToGrid_ || !grid_.has_value() || !grid_->snapX)
        return 0.0;
    if (grid_->xStep > 0.0)
        return grid_->xStep;
    return (model_.getMaxX() - model_.getMinX()) / (double)juce::jmax(1, grid_->xDivisions);
}

/** A pitch grid's lines are not even cells, so a periodic pen does not tile them: it puts one peak on each line a
 *  point could snap to (an out-of-key note never), which is what a comb or formant shape needs. */
CurveEditorComponent::DrawPeaks CurveEditorComponent::drawPeaks() const {
    DrawPeaks peaks;
    if (!snapToGrid_ || !grid_.has_value() || !grid_->snapX || grid_->xLines.empty())
        return peaks;
    bool notes = false, harmonics = false;
    for (const auto& line : grid_->xLines) {
        if (line.tier == GridLine::Tier::outOfKey || line.x < model_.getMinX() - 1e-9 ||
            line.x > model_.getMaxX() + 1e-9)
            continue;
        peaks.x.push_back(line.x);
        notes = notes || line.kind == GridLine::Kind::note;
        harmonics = harmonics || line.kind == GridLine::Kind::harmonic;
    }
    peaks.noun = notes ? "note" : harmonics ? "harmonic" : "line";
    return peaks;
}

/** The curve `wave` as pixel points on the editor's own axes: what the drag's preview strokes (and what it glides
 *  between when the cycle count changes). */
std::vector<juce::Point<float>> CurveEditorComponent::drawPreviewLine(const LfoCustomWave& wave) const {
    const auto geometry = currentGeometry();
    const auto preview = curveModelFromWave(wave);
    constexpr int kSteps = 24;
    std::vector<juce::Point<float>> line;
    for (int seg = 0; seg < preview.getNumSegments(); ++seg) {
        const double x0 = preview.getNode(seg).x;
        const double x1 = preview.getNode(seg + 1).x;
        for (int step = (seg == 0 ? 0 : 1); step <= kSteps; ++step) {
            const float progress = (float)step / (float)kSteps;
            line.push_back(
                {geometry.xForTime(x0 + (x1 - x0) * progress), geometry.yForLevel(preview.valueAt(seg, progress))});
        }
    }
    return line;
}

juce::Path CurveEditorComponent::drawPreviewPath(const std::vector<juce::Point<float>>& line) {
    juce::Path path;
    for (size_t i = 0; i < line.size(); ++i) {
        if (i == 0)
            path.startNewSubPath(line[i]);
        else
            path.lineTo(line[i]);
    }
    return path;
}

juce::Path CurveEditorComponent::drawPreviewPath(const LfoCustomWave& wave) const {
    return drawPreviewPath(drawPreviewLine(wave));
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
