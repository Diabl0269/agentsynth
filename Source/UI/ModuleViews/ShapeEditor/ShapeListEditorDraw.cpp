// ShapeListEditorDraw.cpp -- the shape window's Draw pen: choosing it, remembering it, the button that shows it, and
// the keyboard route that applies it without a mouse. The stroke itself is CurveDrawGesture (the curve editor owns it).
#include "ShapeDrawing.h"
#include "ShapeListEditor.h"
#include "ShapeWindowPrefs.h"
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

/** The button shows the pen, its title and tooltip say so, and the choice goes to the settings. */
void ShapeListEditor::updatePenButton() {
    drawButton_.setPen(pen_, lastPen_);
    const juce::String name = drawShapeName(lastPen_);
    drawButton_.setTitle(pen_.has_value() ? "Draw, " + name + " pen on" : "Draw");
    drawButton_.setTooltip(
        pen_.has_value()
            ? "Draw: the " + name +
                  " pen is on. Drag on the curve to draw that stretch (Shift: no grid snap, Esc: cancel); with the "
                  "curve focused, Enter applies it to the selected stretch. Press again to leave draw mode"
            : "Draw: pick a pen (Free, Line, Sine, Triangle, Saw or Square), then drag on the curve to draw that "
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
    ShapeDrawResult result =
        drawOnCurve(shapes_[(size_t)selected_].curve, wholeCurveStroke(shape, shapes_[(size_t)selected_].curve, range));
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
    const auto result = drawOnCurve(current, spanStroke(*pen_, current, adapter_->getDrawRange(), x0, x1));
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
    launchDrawShapeCallOut(std::move(flyout), drawButton_.getScreenBounds(), [] { return false; });
}

} // namespace synth::ui
