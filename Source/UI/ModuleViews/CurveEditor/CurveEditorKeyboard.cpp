// CurveEditorKeyboard.cpp -- how CurveEditorComponent is reached from the keyboard and a screen
// reader: the keys, the selected point's spoken value, and the focus behaviour. The class is declared
// in CurveEditorComponent.h; its mouse handling is in CurveEditorComponent.cpp.
//
// A key move is a whole gesture (begin, change, end), so an envelope or LFO card records exactly one
// undo step per press, the same bracket a drag opens once for its whole length.

#include "CurveEditorComponent.h"

#include "UI/Layout/FocusRing.h"
#include "UI/Layout/PointEditorKeys.h"
#include "UI/ModuleViews/ModuleViewAccessibility.h"

namespace synth::ui {

namespace {
// Without snapping: a point moves a fiftieth of the visible time range and a twentieth of the level
// range per press. Shift divides either by this factor.
constexpr double kTimeStepFraction = 1.0 / 50.0;
constexpr double kLevelStep = 0.05;
constexpr double kFineDivisor = 5.0;

} // namespace

juce::Point<double> CurveEditorComponent::keyboardStep() const {
    if (snapToGrid_ && grid_.has_value()) {
        const double xDiv = (double)juce::jmax(1, grid_->xDivisions);
        const double yDiv = (double)juce::jmax(1, grid_->yDivisions);
        const double xStep = grid_->xStep > 0.0 ? grid_->xStep : (model_.getMaxX() - model_.getMinX()) / xDiv;
        return {xStep, 1.0 / yDiv};
    }
    return {currentGeometry().getVisibleRange() * kTimeStepFraction, kLevelStep};
}

bool CurveEditorComponent::nudgeSelectedNode(double dx, float dy, bool bypassSnap) {
    return moveSelectionBy(dx, dy, bypassSnap, false, true);
}

// The group's move, shared by a press and a hold's frames. A press brackets it as one gesture; a hold opens the
// bracket at its first real move and closes it when the key goes up, so a hold against a limit never reaches the undo
// stack.
bool CurveEditorComponent::moveSelectionBy(double dx, float dy, bool bypassSnap, bool holdGesture, bool ease) {
    if (selectedIndex_ < 0 || selectedIndex_ >= model_.getNumNodes())
        return false;
    const auto selectedBefore = selection_;
    const auto positionsBefore = selectionPositions();
    const auto easeToResult = [&] {
        const auto positionsAfter = selectionPositions();
        if (!ease || positionsAfter.size() != positionsBefore.size())
            return;
        for (std::size_t k = 0; k < positionsAfter.size(); ++k) {
            const auto from = pointTween_.drawnAt(selectedBefore[k], positionsBefore[k]);
            if (selectedBefore[k] != selection_[k])
                pointTween_.erase(selectedBefore[k]);
            pointTween_.retarget(selection_[k], from, positionsAfter[k]);
        }
    };
    const auto openGesture = [&] {
        if (holdGesture && keyDrag_.gestureOpen)
            return;
        beginGesture();
        keyDrag_.gestureOpen = keyDrag_.gestureOpen || holdGesture;
    };
    const auto closeGesture = [&] {
        if (!holdGesture)
            endGesture();
    };
    const CurveNode before = model_.getNode(selectedIndex_);
    const juce::Point<double> target{before.x + dx, (double)juce::jlimit(before.minY, before.maxY, before.y + dy)};
    const auto snapped = snapModelPoint(currentGeometry(), target, bypassSnap);

    if (selection_.size() > 1) {
        // Snapping aligns the cursor point; the rest of the group keeps its offsets from it.
        std::vector<int> changed;
        CurveModel moved = modelWithSelectionMoved(snapped.x - before.x, snapped.y - before.y, changed);
        if (changed.empty())
            return false;
        openGesture();
        commitSelectionMove(std::move(moved), changed);
        closeGesture();
        easeToResult();
        return true;
    }

    // Try the move on a copy first: a press that changes nothing (a pinned axis, a clamped end) must
    // not open a gesture, or the host would record an empty undo step.
    CurveModel probe = model_;
    const int movedTo = moveNodeInModel(probe, selectedIndex_, snapped);
    if (movedTo == selectedIndex_ && probe.getNode(movedTo).x == before.x && probe.getNode(movedTo).y == before.y)
        return false;

    // Opened before the change and closed after it, so the host captures its before-state first and
    // records the whole move as one undo step.
    openGesture();
    applyNodeTarget(selectedIndex_, snapped);
    closeGesture();
    easeToResult();
    return true;
}

juce::String CurveEditorComponent::getAccessibilityValueText() const {
    const int count = model_.getNumNodes();
    if (selectedIndex_ < 0 || selectedIndex_ >= count)
        return describeCurveSummary(count);
    const auto& node = model_.getNode(selectedIndex_);
    const auto point = describeCurvePoint(selectedIndex_, count, node.x, node.y);
    return selection_.size() > 1 ? juce::String((int)selection_.size()) + " points selected, " + point : point;
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

void CurveEditorComponent::focusLost(FocusChangeType) {
    keyHold_.end();
    repaint();
}

bool CurveEditorComponent::handleChord(const juce::KeyPress& key) {
    const auto mods = key.getModifiers();
    if (mods.isShiftDown() || mods.isAltDown())
        return false;
    if (isInsertPointKey(key))
        return insertAfterCursor();
    switch (key.getKeyCode()) {
    case 'A':
        return selectAllMovable();
    case 'C':
        return copySelection();
    case 'V':
        return pasteAtCursor();
    case 'D':
        return duplicateSelection();
    default:
        return false;
    }
}

bool CurveEditorComponent::keyPressed(const juce::KeyPress& key) {
    const auto mods = key.getModifiers();
    if (mods.isCommandDown() || mods.isCtrlDown())
        return handleChord(key);
    const int code = key.getKeyCode();
    const bool left = code == juce::KeyPress::leftKey;
    const bool right = code == juce::KeyPress::rightKey;
    const bool up = code == juce::KeyPress::upKey;
    const bool down = code == juce::KeyPress::downKey;

    if ((left || right) && !mods.isAltDown()) {
        if (mods.isShiftDown())
            extendSelectionRelative(left ? -1 : 1);
        else
            selectNodeRelative(left ? -1 : 1);
        return true;
    }
    if ((left || right || up || down) && mods.isAltDown()) {
        const auto step = keyboardStep();
        const bool fine = mods.isShiftDown();
        const double scale = fine ? 1.0 / kFineDivisor : 1.0;
        const double dx = left ? -step.x * scale : (right ? step.x * scale : 0.0);
        const double dy = down ? -step.y * scale : (up ? step.y * scale : 0.0);
        pressNudge(code, dx, dy, fine);
        return true;
    }
    if ((up || down) && !mods.isShiftDown()) {
        // Plain Up/Down move the value like Alt+Up/Down; with nothing selected the key is left to the app.
        if (selectedIndex_ < 0)
            return false;
        const double dy = down ? -keyboardStep().y : keyboardStep().y;
        pressNudge(code, 0.0, dy, false);
        return true;
    }
    if ((code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) &&
        model_.getMode() == CurveMode::Free) {
        removeSelectedNodes();
        return true;
    }
    return false;
}

} // namespace synth::ui
