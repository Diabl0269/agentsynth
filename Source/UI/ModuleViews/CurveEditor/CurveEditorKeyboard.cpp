// CurveEditorKeyboard.cpp -- how CurveEditorComponent is reached from the keyboard and a screen
// reader: the keys, the selected point's spoken value, and the focus behaviour. The class is declared
// in CurveEditorComponent.h; its mouse handling is in CurveEditorComponent.cpp.
//
// A key move is a whole gesture (begin, change, end), so an envelope or LFO card records exactly one
// undo step per press, the same bracket a drag opens once for its whole length.

#include "CurveEditorComponent.h"

#include "UI/Layout/FocusRing.h"
#include "UI/ModuleViews/ModuleViewAccessibility.h"

namespace synth::ui {

namespace {
// Without snapping: a point moves a fiftieth of the visible time range and a twentieth of the level
// range per press. Shift divides either by this factor.
constexpr double kTimeStepFraction = 1.0 / 50.0;
constexpr double kLevelStep = 0.05;
constexpr double kFineDivisor = 5.0;

bool isSelectable(const CurveNode& node) { return node.xMovable || node.yMovable; }
} // namespace

juce::Point<double> CurveEditorComponent::keyboardStep() const {
    if (snapToGrid_ && grid_.has_value()) {
        const double xDiv = (double)juce::jmax(1, grid_->xDivisions);
        const double yDiv = (double)juce::jmax(1, grid_->yDivisions);
        return {(model_.getMaxX() - model_.getMinX()) / xDiv, 1.0 / yDiv};
    }
    return {currentGeometry().getVisibleRange() * kTimeStepFraction, kLevelStep};
}

void CurveEditorComponent::setSelectedIndex(int index) {
    if (selectedIndex_ == index)
        return;
    selectedIndex_ = index;
    refreshAccessibilityValue();
}

int CurveEditorComponent::selectNodeRelative(int delta) {
    const int count = model_.getNumNodes();
    const bool fromStart = selectedIndex_ < 0;
    const int step = (fromStart || delta > 0) ? 1 : -1;
    for (int candidate = fromStart ? 0 : selectedIndex_ + step; candidate >= 0 && candidate < count;
         candidate += step) {
        if (isSelectable(model_.getNode(candidate))) {
            setSelectedIndex(candidate);
            repaint();
            return candidate;
        }
    }
    return selectedIndex_;
}

bool CurveEditorComponent::nudgeSelectedNode(double dx, float dy, bool bypassSnap) {
    if (selectedIndex_ < 0 || selectedIndex_ >= model_.getNumNodes())
        return false;
    const CurveNode before = model_.getNode(selectedIndex_);
    const juce::Point<double> target{before.x + dx, (double)juce::jlimit(before.minY, before.maxY, before.y + dy)};
    const auto snapped = snapModelPoint(currentGeometry(), target, bypassSnap);

    // Try the move on a copy first: a press that changes nothing (a pinned axis, a clamped end) must
    // not open a gesture, or the host would record an empty undo step.
    CurveModel probe = model_;
    const int movedTo = moveNodeInModel(probe, selectedIndex_, snapped);
    if (movedTo == selectedIndex_ && probe.getNode(movedTo).x == before.x && probe.getNode(movedTo).y == before.y)
        return false;

    // Opened before the change and closed after it, so the host captures its before-state first and
    // records the whole move as one undo step.
    beginGesture();
    applyNodeTarget(selectedIndex_, snapped);
    endGesture();
    return true;
}

juce::String CurveEditorComponent::getAccessibilityValueText() const {
    const int count = model_.getNumNodes();
    if (selectedIndex_ < 0 || selectedIndex_ >= count)
        return describeCurveSummary(count);
    const auto& node = model_.getNode(selectedIndex_);
    return describeCurvePoint(selectedIndex_, count, node.x, node.y);
}

void CurveEditorComponent::refreshAccessibilityValue() {
    const auto text = getAccessibilityValueText();
    if (text == announcedValueText_)
        return;
    announcedValueText_ = text;
    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
}

std::unique_ptr<juce::AccessibilityHandler> CurveEditorComponent::createAccessibilityHandler() {
    return makeValueTextHandler(*this, [this] { return getAccessibilityValueText(); });
}

void CurveEditorComponent::focusGained(FocusChangeType cause) {
    // Tabbing in lands on a point, so the screen reader has something to read straight away.
    if (cause != focusChangedByMouseClick && selectedIndex_ < 0)
        selectNodeRelative(1);
    repaint();
}

void CurveEditorComponent::focusLost(FocusChangeType) { repaint(); }

bool CurveEditorComponent::keyPressed(const juce::KeyPress& key) {
    const auto mods = key.getModifiers();
    if (mods.isCommandDown() || mods.isCtrlDown())
        return false;
    const int code = key.getKeyCode();
    const bool left = code == juce::KeyPress::leftKey;
    const bool right = code == juce::KeyPress::rightKey;
    const bool up = code == juce::KeyPress::upKey;
    const bool down = code == juce::KeyPress::downKey;

    if ((left || right) && !mods.isAltDown()) {
        selectNodeRelative(left ? -1 : 1);
        return true;
    }
    if ((left || right || up || down) && mods.isAltDown()) {
        const auto step = keyboardStep();
        const bool fine = mods.isShiftDown();
        const double scale = fine ? 1.0 / kFineDivisor : 1.0;
        const double dx = left ? -step.x * scale : (right ? step.x * scale : 0.0);
        const double dy = down ? -step.y * scale : (up ? step.y * scale : 0.0);
        nudgeSelectedNode(dx, (float)dy, fine);
        return true;
    }
    if ((code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) &&
        model_.getMode() == CurveMode::Free) {
        if (selectedIndex_ >= 0)
            removeNode(selectedIndex_);
        return true;
    }
    return false;
}

} // namespace synth::ui
