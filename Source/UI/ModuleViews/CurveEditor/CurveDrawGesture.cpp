#include "CurveDrawGesture.h"

#include "CurveEditorComponent.h"
#include "CurveWaveConversion.h"
#include "UI/Layout/PointEditing/DrawCountGlide.h"
#include "UI/ModuleViews/ShapeEditor/CycleCountStepper.h"
#include "UI/ModuleViews/ShapeEditor/ShapeDrawStroke.h"
#include "UI/ModuleViews/ShapeEditor/ShapeDrawing.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Timeline/AutomationLanes/LaneShapes/DrawShape.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {

juce::String formatCycles(double cycles) {
    const bool whole = std::abs(cycles - std::round(cycles)) < 1e-6;
    const auto text = whole ? juce::String((int)std::llround(cycles)) : juce::String(cycles, 1);
    return text + (std::abs(cycles - 1.0) < 1e-6 ? " cycle" : " cycles");
}

} // namespace

static_assert(CurveDrawGesture::kMaxCycles == kMaxDrawCycles);
static_assert(CurveDrawGesture::kCountStepPx == CycleCountStepper::kStepPx);

struct CurveDrawGesture::State {
    LfoCustomWave start;
    std::vector<LfoCustomWave::Point> samples;
};

CurveDrawGesture::CurveDrawGesture(CurveEditorComponent& editor)
    : editor_(editor)
    , stepper_(std::make_unique<CycleCountStepper>())
    , glide_(std::make_unique<DrawCountGlide>(editor))
    , s_(std::make_unique<State>()) {}

CurveDrawGesture::~CurveDrawGesture() = default;

void CurveDrawGesture::setPen(std::optional<DrawShape> pen) {
    cancel();
    pen_ = pen;
}

void CurveDrawGesture::cancel() {
    glide_->clear();
    if (!active_)
        return;
    active_ = false;
    stepper_->reset();
    gridHint_ = atLimit_ = false;
    s_->samples.clear();
    editor_.repaint();
}

bool CurveDrawGesture::press(const juce::MouseEvent& e) {
    if (!pen_.has_value() || e.mods.isPopupMenu())
        return false;
    active_ = true;
    glide_->clear();
    stepper_->reset();
    gridHint_ = false;
    atLimit_ = false;
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
    if (e.mods.isAltDown() && pen_.has_value() && isPeriodicShape(*pen_)) {
        dragCount(e); // Option: the box stays, up/down is the count
        editor_.repaint();
        return true;
    }
    stepper_->reset();
    gridHint_ = false;
    atLimit_ = false;
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
    glide_->clear();
    stepper_->reset();
    gridHint_ = atLimit_ = false;
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
    const auto [lo, hi] = boxLevels(a, b);
    s.lo = lo;
    s.hi = hi;
    s.cycleWidth = effectiveCycleWidth();
    s.peaks = editor_.drawPeaks().x;
    s.freehand = s_->samples;
    return s;
}

/** A grid's cell while one snaps, else the remembered width, else the default count across the full width. */
double CurveDrawGesture::effectiveCycleWidth() const {
    const double cell = editor_.drawCellWidth();
    if (cell > 0.0)
        return cell;
    const double full = editor_.getModel().getMaxX() - editor_.getModel().getMinX();
    return cycleWidth_.value_or(full / (double)juce::jmax(1, band_.cycles));
}

/** The count is the user's only with the grid Off; with a grid, one cycle per cell. */
bool CurveDrawGesture::countApplies() const { return editor_.drawCellWidth() <= 0.0; }

/** The most whole cycles the current pen draws over `span`: from the curve as it stands (as it stood at the press
 *  while a drag is in flight), over the dragged stretch or the full width's start. */
int CurveDrawGesture::maxCycles(double span) const {
    if (!pen_.has_value())
        return kMaxCycles;
    ShapeDrawStroke s;
    s.shape = *pen_;
    s.cycleWidth = span;
    if (active_) {
        s.x0 = std::min(a_.x, b_.x);
        const auto [lo, hi] = boxLevels(a_, b_);
        s.lo = lo;
        s.hi = hi;
    } else {
        s.x0 = editor_.getModel().getMinX();
        s.lo = std::min(band_.lo, band_.hi);
        s.hi = std::max(band_.lo, band_.hi);
    }
    s.x1 = s.x0 + span;
    return maxDrawableCycles(active_ ? s_->start : waveFromCurveModel(editor_.getModel()), s);
}

/** Remembers `cycles` across `span` as a cycle width, never more than fit: the count asked is the count drawn. */
void CurveDrawGesture::setCycles(double span, int cycles, bool alreadyOver) {
    const auto drawnNow = drawnPreviewLine(); // before the count changes: the glide starts from what is painted now
    const int most = maxCycles(span);
    atLimit_ = alreadyOver || cycles > most;
    cycles = juce::jlimit(1, most, cycles);
    const double width = span / (double)cycles;
    if (cycleWidth_.has_value() && std::abs(*cycleWidth_ - width) < 1e-12)
        return;
    cycleWidth_ = width;
    glide_->begin(drawnNow, false);
    if (onCycleWidthChanged)
        onCycleWidthChanged(width);
}

/** Option held: the dragged box is frozen and the pointer's height off where Option went down counts the cycles. */
void CurveDrawGesture::dragCount(const juce::MouseEvent& e) {
    const double span = std::abs(b_.x - a_.x);
    if (!countApplies()) {
        gridHint_ = true;
        return;
    }
    if (span <= 1e-9)
        return;
    const int most = maxCycles(span);
    if (!stepper_->held())
        stepper_->begin(e.position.y, (int)std::lround(span / effectiveCycleWidth()), most);
    if (const auto count = stepper_->dragTo(e.position.y, most))
        setCycles(span, count->cycles, count->over);
}

bool CurveDrawGesture::stepCount(int delta) {
    if (!pen_.has_value() || !isPeriodicShape(*pen_))
        return false;
    if (!countApplies()) {
        if (onNote)
            onNote("A grid sets the cycles: turn Grid Off to change the count");
        return true;
    }
    const double full = editor_.getModel().getMaxX() - editor_.getModel().getMinX();
    const double span = active_ ? std::abs(b_.x - a_.x) : full;
    if (span <= 1e-9)
        return true;
    const int most = maxCycles(span);
    const int now = juce::jlimit(1, most, (int)std::lround(span / effectiveCycleWidth())); // the drawn count
    const auto next = CycleCountStepper::step(now, delta, most);
    setCycles(span, next.cycles, next.over);
    if (onNote) {
        if (atLimit_)
            onNote(formatCycles(most) + " (most that fit)");
        else
            onNote(juce::String(next.cycles) + (active_ ? " cycles" : " cycles across the full width"));
    }
    editor_.repaint();
    return true;
}

// Like the lane's Draw tool, the press is one corner of the box and the drag the other, so the wave spans the two
// levels. A drag that barely leaves the press's level keeps the default band's height, centred on the press, so a
// plain sideways drag still draws a wave rather than a flat line.
std::pair<float, float> CurveDrawGesture::boxLevels(const juce::Point<double>& a, const juce::Point<double>& b) const {
    const float heightPx = std::abs(editor_.pixelForDrawPoint(a).y - editor_.pixelForDrawPoint(b).y);
    if (heightPx >= kMinBoxHeightPx)
        return {(float)std::min(a.y, b.y), (float)std::max(a.y, b.y)};
    const float half = std::abs(band_.hi - band_.lo) * 0.5f;
    const float centre = juce::jlimit(half, 1.0f - half, (float)a.y);
    return {centre - half, centre + half};
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
    if (gridHint_)
        return "A grid sets the cycles: turn Grid Off to count";
    const auto drawn = result();
    if (drawn.refusal.isNotEmpty())
        return drawn.refusal;
    if (!drawn.changed || !isPeriodicShape(*pen_))
        return {};
    if (drawn.peaks > 0)
        return juce::String(drawn.peaks) + (drawn.peaks == 1 ? " peak" : " peaks") + ", one per " +
               editor_.drawPeaks().noun;
    return formatCycles(drawn.cycles) + (atLimit_ ? " (most that fit)" : "");
}

/** The preview as it is painted now: the resting wave, or while a count change glides the blend from the line shown
 *  when it changed. Empty when no preview is showing. */
std::vector<juce::Point<float>> CurveDrawGesture::drawnPreviewLine() const {
    if (!active_ || !pen_.has_value())
        return {};
    const auto drawn = result();
    if (!drawn.changed)
        return {};
    return glide_->drawn(editor_.drawPreviewLine(drawn.curve));
}

void CurveDrawGesture::paint(juce::Graphics& g) const {
    if (!active_ || !pen_.has_value())
        return;
    const auto& colors = synth::theme::themeOf(editor_).colors;
    const auto drawn = result();
    const auto s = stroke(a_, b_);

    // The stretch being drawn, like the lane's box: a wash with dashed edges. A periodic shape's box spans the levels
    // it swings between; a Line's spans the whole height.
    if (*pen_ != DrawShape::Free && s.x1 > s.x0) {
        const float x0 = editor_.pixelForDrawPoint({s.x0, 0.0}).x;
        const float x1 = editor_.pixelForDrawPoint({s.x1, 0.0}).x;
        const bool periodic = isPeriodicShape(*pen_);
        const float top = periodic ? editor_.pixelForDrawPoint({s.x0, (double)s.hi}).y : 0.0f;
        const float bottom = periodic ? editor_.pixelForDrawPoint({s.x0, (double)s.lo}).y : (float)editor_.getHeight();
        const auto box = juce::Rectangle<float>(x0, top, x1 - x0, bottom - top);
        g.setColour(colors.accent.withAlpha(0.10f));
        g.fillRect(box);
        const float dash[] = {4.0f, 3.0f};
        g.setColour(colors.accent.withAlpha(0.8f));
        g.drawDashedLine({box.getTopLeft(), box.getBottomLeft()}, dash, 2, 1.0f);
        g.drawDashedLine({box.getTopRight(), box.getBottomRight()}, dash, 2, 1.0f);
        if (periodic) {
            g.drawDashedLine({box.getTopLeft(), box.getTopRight()}, dash, 2, 1.0f);
            g.drawDashedLine({box.getBottomLeft(), box.getBottomRight()}, dash, 2, 1.0f);
        }
    }
    if (drawn.changed) {
        g.setColour(colors.accent);
        g.strokePath(CurveEditorComponent::drawPreviewPath(glide_->drawn(editor_.drawPreviewLine(drawn.curve))),
                     juce::PathStrokeType(2.5f));
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
