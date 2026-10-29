// CurveEditorKeyboard.cpp -- how CurveEditorComponent is reached from the keyboard and a screen
// reader: the keys, the selected point's spoken value, and the focus behaviour. The class is declared
// in CurveEditorComponent.h; its mouse handling is in CurveEditorComponent.cpp.
//
// A key move is a whole gesture (begin, change, end), so an envelope or LFO card records exactly one
// undo step per press, the same bracket a drag opens once for its whole length.

#include "CurveEditorComponent.h"

#include "UI/Layout/FocusRing.h"
#include "UI/Layout/PointEditing/PointEditorKeys.h"
#include "UI/ModuleViews/ModuleViewAccessibility.h"

namespace synth::ui {

namespace {
// Without snapping: a point moves a fiftieth of the visible time range and of the level range per
// press. Shift divides either by this factor.
constexpr double kTimeStepFraction = 1.0 / 50.0;
constexpr double kLevelStep = 1.0 / 50.0;
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
    if (cursorIndex() < 0 || cursorIndex() >= model_.getNumNodes())
        return false;
    const auto selectedBefore = selection_.selected();
    const auto positionsBefore = selectionPositions();
    const auto easeToResult = [&] {
        const auto positionsAfter = selectionPositions();
        if (!ease || positionsAfter.size() != positionsBefore.size())
            return;
        for (std::size_t k = 0; k < positionsAfter.size(); ++k) {
            const auto from = pointTween_.drawnAt(selectedBefore[k], positionsBefore[k]);
            if (selectedBefore[k] != selection_.selected()[k])
                pointTween_.erase(selectedBefore[k]);
            pointTween_.retarget(selection_.selected()[k], from, positionsAfter[k]);
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
    const CurveNode before = model_.getNode(cursorIndex());
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
    const int movedTo = moveNodeInModel(probe, cursorIndex(), snapped);
    if (movedTo == cursorIndex() && probe.getNode(movedTo).x == before.x && probe.getNode(movedTo).y == before.y)
        return false;

    // Opened before the change and closed after it, so the host captures its before-state first and
    // records the whole move as one undo step.
    openGesture();
    applyNodeTarget(cursorIndex(), snapped);
    closeGesture();
    easeToResult();
    return true;
}

juce::String CurveEditorComponent::getAccessibilityValueText() const {
    const int count = model_.getNumNodes();
    if (cursorIndex() < 0 || cursorIndex() >= count)
        return describeCurveSummary(count);
    const auto& node = model_.getNode(cursorIndex());
    const auto point = describeCurvePoint(cursorIndex(), count, node.x, node.y);
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
    if (cause != focusChangedByMouseClick && cursorIndex() < 0)
        selectNodeRelative(1);
    repaint();
}

void CurveEditorComponent::focusLost(FocusChangeType) {
    keyHold_.end();
    repaint();
}

// The key map is decoded once for both point editors (pointKeyAction); the curve leaves Escape and Shift+Up/Down to
// the app, and takes Delete with any modifier but Cmd.
bool CurveEditorComponent::keyPressed(const juce::KeyPress& key) {
    using Kind = PointKeyAction::Kind;
    const auto action = pointKeyAction(key);
    if (keyHold_.active() && KeyHold::isArrow(key.getKeyCode()) && keyHold_.anyArrowDown())
        return true; // an arrow joining a running hold (or its auto-repeat): the hold reads it each frame
    switch (action.kind) {
    case Kind::insert:
        if (const auto why = insertRefusal(); why.isNotEmpty() && onInsertRefused) {
            onInsertRefused(why);
            return true;
        }
        return insertAfterCursor();
    case Kind::selectAll:
        return selectAllMovable();
    case Kind::copy:
        return copySelection();
    case Kind::paste:
        return pasteAtCursor();
    case Kind::duplicate:
        return duplicateSelection();
    case Kind::pick:
        selectNodeRelative(action.side);
        return true;
    case Kind::extend:
        extendSelectionRelative(action.side);
        return true;
    case Kind::move: {
        const auto step = keyboardStep();
        const bool fine = action.size == PointKeyAction::Size::fine;
        const double scale = fine ? 1.0 / kFineDivisor : 1.0;
        const double dx = action.time ? action.side * step.x * scale : 0.0;
        const double dy = action.time ? 0.0 : action.side * step.y * scale;
        pressNudge(key.getKeyCode(), dx, dy, fine);
        return true;
    }
    case Kind::value:
        // Plain Up/Down move the value like Alt+Up/Down; with nothing selected the key is left to the app.
        if (action.size != PointKeyAction::Size::normal || cursorIndex() < 0)
            return false;
        pressNudge(key.getKeyCode(), 0.0, action.side * keyboardStep().y, false);
        return true;
    case Kind::remove:
        if (model_.getMode() != CurveMode::Free)
            return false;
        removeSelectedNodes();
        return true;
    default:
        return false;
    }
}

} // namespace synth::ui
