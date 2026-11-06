#include "CurveDrawGesture.h"

#include "CurveEditorComponent.h"
#include "CurveWaveConversion.h"
#include "UI/ModuleViews/ShapeEditor/ShapeDrawStroke.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Timeline/AutomationLanes/LaneShapes/DrawShape.h"
#include <cmath>

namespace synth::ui {

namespace {

juce::String formatCycles(double cycles) {
    const bool whole = std::abs(cycles - std::round(cycles)) < 1e-6;
    const auto text = whole ? juce::String((int)std::llround(cycles)) : juce::String(cycles, 1);
    return text + (std::abs(cycles - 1.0) < 1e-6 ? " cycle" : " cycles");
}

} // namespace

struct CurveDrawGesture::State {
    LfoCustomWave start;
    std::vector<LfoCustomWave::Point> samples;
};

CurveDrawGesture::CurveDrawGesture(CurveEditorComponent& editor)
    : editor_(editor)
    , s_(std::make_unique<State>()) {}

CurveDrawGesture::~CurveDrawGesture() = default;

void CurveDrawGesture::setPen(std::optional<DrawShape> pen) {
    cancel();
    pen_ = pen;
}

void CurveDrawGesture::cancel() {
    if (!active_)
        return;
    active_ = false;
    s_->samples.clear();
    editor_.repaint();
}

bool CurveDrawGesture::press(const juce::MouseEvent& e) {
    if (!pen_.has_value() || e.mods.isPopupMenu())
        return false;
    active_ = true;
    s_->start = waveFromCurveModel(editor_.getModel());
    a_ = b_ = editor_.drawPointAt(e.position, *pen_ == DrawShape::Free || e.mods.isShiftDown());
    s_->samples.clear();
    if (*pen_ == DrawShape::Free)
        s_->samples.push_back({(float)a_.x, (float)a_.y, 0.0f});
    editor_.repaint();
    return true;
}

bool CurveDrawGesture::drag(const juce::MouseEvent& e) {
    if (!active_)
        return false;
    // Free follows the mouse unsnapped; the others snap their end to the grid unless Shift is held.
    b_ = editor_.drawPointAt(e.position, *pen_ == DrawShape::Free || e.mods.isShiftDown());
    if (*pen_ == DrawShape::Free)
        s_->samples.push_back({(float)b_.x, (float)b_.y, 0.0f});
    editor_.repaint();
    return true;
}

/** The release writes what the last drag frame showed; it does not read the release position again. */
bool CurveDrawGesture::release(const juce::MouseEvent&) {
    if (!active_)
        return false;
    const auto drawn = result();
    active_ = false;
    s_->samples.clear();
    if (drawn.changed)
        editor_.commitDrawnCurve(drawn.curve);
    else if (drawn.refusal.isNotEmpty() && editor_.onDrawRefused)
        editor_.onDrawRefused(drawn.refusal);
    editor_.repaint();
    return true;
}

ShapeDrawStroke CurveDrawGesture::stroke(const juce::Point<double>& a, const juce::Point<double>& b) const {
    ShapeDrawStroke s;
    s.shape = pen_.value_or(DrawShape::Free);
    const bool forward = a.x <= b.x;
    s.x0 = forward ? a.x : b.x;
    s.x1 = forward ? b.x : a.x;
    s.y0 = (float)(forward ? a.y : b.y);
    s.y1 = (float)(forward ? b.y : a.y);
    s.lo = band_.lo;
    s.hi = band_.hi;
    const double cell = editor_.drawCellWidth();
    s.cycleWidth = cell > 0.0 ? cell
                              : (editor_.getModel().getMaxX() - editor_.getModel().getMinX()) /
                                    (double)juce::jmax(1, band_.cycles);
    s.freehand = s_->samples;
    return s;
}

ShapeDrawResult CurveDrawGesture::result() const {
    if (!active_ || !pen_.has_value()) {
        ShapeDrawResult none;
        none.curve = s_->start;
        return none;
    }
    return drawOnCurve(s_->start, stroke(a_, b_));
}

juce::String CurveDrawGesture::chipText() const {
    if (!active_ || !pen_.has_value())
        return {};
    const auto drawn = result();
    if (drawn.refusal.isNotEmpty())
        return drawn.refusal;
    return drawn.changed && isPeriodicShape(*pen_) ? formatCycles(drawn.cycles) : juce::String();
}

void CurveDrawGesture::paint(juce::Graphics& g) const {
    if (!active_ || !pen_.has_value())
        return;
    const auto& colors = synth::theme::themeOf(editor_).colors;
    const auto drawn = result();
    const auto s = stroke(a_, b_);

    // The stretch being drawn, like the lane's box: a wash with dashed edges.
    if (*pen_ != DrawShape::Free && s.x1 > s.x0) {
        const float x0 = editor_.pixelForDrawPoint({s.x0, 0.0}).x;
        const float x1 = editor_.pixelForDrawPoint({s.x1, 0.0}).x;
        const auto box = juce::Rectangle<float>(x0, 0.0f, x1 - x0, (float)editor_.getHeight());
        g.setColour(colors.accent.withAlpha(0.10f));
        g.fillRect(box);
        const float dash[] = {4.0f, 3.0f};
        g.setColour(colors.accent.withAlpha(0.8f));
        g.drawDashedLine({box.getTopLeft(), box.getBottomLeft()}, dash, 2, 1.0f);
        g.drawDashedLine({box.getTopRight(), box.getBottomRight()}, dash, 2, 1.0f);
    }
    if (drawn.changed) {
        g.setColour(colors.accent);
        g.strokePath(editor_.drawPreviewPath(drawn.curve), juce::PathStrokeType(2.5f));
    }

    const auto chip = chipText();
    if (chip.isEmpty())
        return;
    const auto font = juce::Font(juce::FontOptions(12.0f));
    const int w = juce::GlyphArrangement::getStringWidthInt(font, chip) + 14;
    const float anchor = editor_.pixelForDrawPoint({s.x0, 0.0}).x;
    const auto area = juce::Rectangle<float>(
        juce::jlimit(2.0f, juce::jmax(2.0f, (float)editor_.getWidth() - w - 2.0f), anchor), 4.0f, (float)w, 20.0f);
    g.setColour(colors.bg1.withAlpha(0.92f));
    g.fillRoundedRectangle(area, 10.0f);
    g.setColour(drawn.refusal.isNotEmpty() ? colors.error : colors.accent);
    g.drawRoundedRectangle(area, 10.0f, 1.0f);
    g.setColour(drawn.refusal.isNotEmpty() ? colors.error : colors.textPrimary);
    g.setFont(font);
    g.drawText(chip, area, juce::Justification::centred, false);
}

} // namespace synth::ui
