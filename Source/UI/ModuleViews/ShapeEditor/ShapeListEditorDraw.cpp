// ShapeListEditorDraw.cpp -- the shape window's Draw pen: choosing it, remembering it, the button that shows it, and
// the keyboard route that applies it without a mouse. The stroke itself is CurveDrawGesture (the curve editor owns it).
#include "ShapeDrawing.h"
#include "ShapeListEditor.h"
#include "ShapeWindowPrefs.h"
#include "UI/Layout/ButtonTooltip.h"
#include "UI/Layout/PopupHost.h"
#include "UI/Timeline/AutomationLanes/LaneShapes/DrawShapeFlyout.h"
#include <algorithm>

namespace synth::ui {

namespace {

std::optional<DrawShape> shapeNamed(const juce::String& name) {
    for (const auto shape : kAllDrawShapes)
        if (name == drawShapeName(shape))
            return shape;
    return std::nullopt;
}

} // namespace

/** The pen the window last used: on, off with its shape kept, or never chosen. */
void ShapeListEditor::restorePen() {
    const auto saved = shapewindowprefs::loadText(adapter_->getApplicationProperties(),
                                                  shapewindowprefs::penKey(adapter_->getPreferenceKind()));
    const bool off = saved.startsWith("off:");
    const auto shape = shapeNamed(off ? saved.fromFirstOccurrenceOf("off:", false, false) : saved);
    if (shape.has_value())
        lastPen_ = *shape;
    pen_ = shape.has_value() && !off ? shape : std::nullopt;
    curve_.setDrawPen(pen_);
    updatePenButton();
}

/** The cycle width the count gesture last left (Option+drag up/down, `[` and `]`): used by the next stroke and saved,
 *  so a stroke made tomorrow packs the same number of cycles. */
void ShapeListEditor::restoreCycleWidth() {
    auto& gesture = curve_.getDrawGesture();
    const auto kind = adapter_->getPreferenceKind();
    const double saved =
        shapewindowprefs::loadText(adapter_->getApplicationProperties(), shapewindowprefs::cycleWidthKey(kind))
            .getDoubleValue();
    if (saved > 0.0 && saved <= 1.0)
        gesture.setCycleWidth(saved);
    gesture.onCycleWidthChanged = [this, kind](double width) {
        shapewindowprefs::saveText(adapter_->getApplicationProperties(), shapewindowprefs::cycleWidthKey(kind),
                                   juce::String(width, 6));
    };
    gesture.onNote = [this](const juce::String& note) { showStatus(note, false); };
}

/** The button shows the pen, its title and tooltip say so, and the choice goes to the settings. */
void ShapeListEditor::updatePenButton() {
    drawButton_.setPen(pen_, lastPen_);
    const juce::String name = drawShapeName(lastPen_);
    drawButton_.setTitle(pen_.has_value() ? "Draw, " + name + " pen on" : "Draw");
    setButtonTooltip(
        drawButton_,
        pen_.has_value()
            ? "The " + name +
                  " pen is on. Drag on the curve to draw that stretch (Shift: no grid snap, Esc: cancel; Option+drag "
                  "up or "
                  "down, or [ and ], sets the cycles); with the curve focused, Enter applies it to the selected "
                  "stretch. Press again to leave draw mode"
            : "Pick a pen (Free, Line, Sine, Triangle, Saw or Square), then drag on the curve to draw that "
              "stretch of it");
}

void ShapeListEditor::setPen(std::optional<DrawShape> pen) {
    if (pen.has_value())
        lastPen_ = *pen;
    pen_ = pen;
    curve_.setDrawPen(pen_);
    updatePenButton();
    const juce::String name = drawShapeName(lastPen_);
    shapewindowprefs::saveText(adapter_->getApplicationProperties(),
                               shapewindowprefs::penKey(adapter_->getPreferenceKind()),
                               pen_.has_value() ? name : "off:" + name);
    juce::AccessibilityHandler::postAnnouncement(pen_.has_value() ? name + " pen on" : "Draw mode off",
                                                 juce::AccessibilityHandler::AnnouncementPriority::medium);
}

void ShapeListEditor::applyDrawShape(DrawShape shape) {
    if (shape == DrawShape::Free || shapes_.empty())
        return;
    const auto range = adapter_->getDrawRange();
    auto stroke = wholeCurveStroke(shape, shapes_[(size_t)selected_].curve, range);
    stroke.peaks = curve_.drawPeaks().x; // a pitch grid: one peak per line, as the mouse draws it
    ShapeDrawResult result = drawOnCurve(shapes_[(size_t)selected_].curve, stroke);
    if (!result.changed) {
        if (result.refusal.isNotEmpty())
            showStatus(result.refusal);
        return;
    }
    edit([&](std::vector<ShapeItem>& shapes) { shapes[(size_t)selected_].curve = result.curve; });
}

bool ShapeListEditor::applyPenToSelection() {
    if (!pen_.has_value() || shapes_.empty())
        return false;
    if (*pen_ == DrawShape::Free) {
        showStatus("Free draws with the mouse: drag on the curve, or pick another pen to apply from the keyboard");
        return true;
    }
    const auto& model = curve_.getModel();
    const auto& chosen = curve_.getSelectedIndices();
    double x0 = model.getMinX();
    double x1 = model.getMaxX();
    if (chosen.size() >= 2) {
        x0 = model.getNode(chosen.front()).x;
        x1 = model.getNode(chosen.back()).x;
        if (x0 > x1)
            std::swap(x0, x1);
    }
    const auto& current = shapes_[(size_t)selected_].curve;
    auto stroke = spanStroke(*pen_, current, adapter_->getDrawRange(), x0, x1);
    stroke.peaks = curve_.drawPeaks().x; // a pitch grid: one peak per line, as the mouse draws it
    const auto result = drawOnCurve(current, stroke);
    if (!result.changed) {
        showStatus(result.refusal.isNotEmpty() ? result.refusal : "Nothing to draw between those points");
        return true;
    }
    edit([&](std::vector<ShapeItem>& shapes) { shapes[(size_t)selected_].curve = result.curve; });
    return true;
}

/** The lane Draw tool's own flyout, so the six shapes, their icons and their order are the lane's. A pick chooses the
    pen. Headless tests take the content through the lane's hook instead of a call-out. */
void ShapeListEditor::openDrawFlyout() {
    juce::Component::SafePointer<ShapeListEditor> safe(this);
    auto flyout = std::make_unique<DrawShapeFlyout>(
        lastPen_, [](DrawShape) { return juce::String(); },
        [safe](DrawShape shape) {
            if (safe != nullptr)
                safe->setPen(shape);
        });
    if (auto& hook = test_hooks::drawShapeFlyoutHookForTest()) {
        hook(std::move(flyout));
        return;
    }
    launchPopover(std::move(flyout), drawButton_.getScreenBounds(), nullptr, {.trigger = &drawButton_});
}

} // namespace synth::ui
