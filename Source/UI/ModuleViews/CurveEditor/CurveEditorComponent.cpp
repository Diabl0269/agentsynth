#include "CurveEditorComponent.h"
#include <cmath>

namespace synth::ui {

CurveEditorComponent::CurveEditorComponent() {
    setWantsKeyboardFocus(true);
    setTitle("Curve editor");
    setDescription("Breakpoint curve");
    setTooltip("Curve editor: Left/Right pick a point, Alt+arrows move it, Delete removes it. Mouse: drag points "
               "and bend handles, double-click to add or remove a point");
}

CurveEditorGeometry CurveEditorComponent::currentGeometry() const {
    CurveGeometryConfig config = geometryConfig_;
    if (!config.explicitVisibleRange.has_value() && dragFrozenRange_.has_value())
        config.explicitVisibleRange = dragFrozenRange_;
    return CurveEditorGeometry(model_, getLocalBounds().toFloat(), config);
}

void CurveEditorComponent::setModel(CurveModel model) {
    const int oldNumNodes = model_.getNumNodes();
    const CurveMode oldMode = model_.getMode();
    model_ = std::move(model);

    const int newNumNodes = model_.getNumNodes();
    const bool topologyChanged = newNumNodes != oldNumNodes || model_.getMode() != oldMode;
    const bool indexOutOfRange =
        dragIndex_ >= newNumNodes || hoveredIndex_ >= newNumNodes || selectedIndex_ >= newNumNodes;

    if (topologyChanged || indexOutOfRange) {
        if (gestureActive_) {
            endGesture(); // pair the gesture right here -- a mouseUp can no longer arrive
                          // meaningfully for the old topology
            gestureActive_ = false;
        }
        dragKind_ = DragKind::None;
        dragIndex_ = -1;
        dragFrozenRange_.reset();
        hoveredKind_ = CurveHitKind::None;
        hoveredIndex_ = -1;
        selectedIndex_ = -1;
    }
    repaint();
    refreshAccessibilityValue();
}

void CurveEditorComponent::setMinVisibleRange(double minRange) {
    geometryConfig_.minVisibleRange = minRange;
    repaint();
}

void CurveEditorComponent::setVisibleRangeOverride(std::optional<double> range) {
    geometryConfig_.explicitVisibleRange = range;
    repaint();
}

void CurveEditorComponent::setZeroSegmentPx(float px) {
    geometryConfig_.zeroSegmentPx = px;
    repaint();
}

void CurveEditorComponent::setTimeLabelFormatter(std::function<juce::String(double)> formatter) {
    timeLabelFormatter_ = formatter ? std::move(formatter) : &CurveEditorGeometry::defaultTimeLabel;
    repaint();
}

void CurveEditorComponent::setGrid(std::optional<CurveGrid> grid) {
    grid_ = grid;
    repaint();
}

void CurveEditorComponent::setSnapToGrid(bool enabled) { snapToGrid_ = enabled; }

void CurveEditorComponent::setFillBaselineLevel(float level) {
    fillBaselineLevel_ = level;
    repaint();
}

void CurveEditorComponent::setPlayhead(std::optional<CurvePlayhead> playhead) {
    if (playhead_ == playhead)
        return;
    playhead_ = playhead;
    repaint();
}

void CurveEditorComponent::beginGesture() {
    if (onGestureStart)
        onGestureStart();
}

void CurveEditorComponent::endGesture() {
    if (onGestureEnd)
        onGestureEnd();
}

CurveHitResult CurveEditorComponent::hitTest(juce::Point<float> point) const {
    return currentGeometry().hitTest(point);
}

juce::Point<double> CurveEditorComponent::snapModelPoint(const CurveEditorGeometry&, juce::Point<double> point,
                                                         bool bypassSnap) const {
    if (!snapToGrid_ || !grid_.has_value() || bypassSnap)
        return point;

    const double minX = model_.getMinX();
    const double maxX = model_.getMaxX();
    const double xDiv = (double)juce::jmax(1, grid_->xDivisions);
    const double yDiv = (double)juce::jmax(1, grid_->yDivisions);

    double x = point.x;
    if (maxX > minX) {
        const double frac = juce::jlimit(0.0, 1.0, (x - minX) / (maxX - minX));
        x = minX + (std::round(frac * xDiv) / xDiv) * (maxX - minX);
    }
    const double y = std::round(juce::jlimit(0.0, 1.0, point.y) * yDiv) / yDiv;
    return {x, y};
}

int CurveEditorComponent::dragNodeTo(int index, juce::Point<float> point, bool bypassSnap) {
    if (index < 0 || index >= model_.getNumNodes())
        return index;

    const auto geometry = currentGeometry();
    const juce::Point<double> snapped =
        snapModelPoint(geometry, {geometry.timeForX(point.x), (double)geometry.levelForY(point.y)}, bypassSnap);
    return applyNodeTarget(index, snapped);
}

int CurveEditorComponent::moveNodeInModel(CurveModel& model, int index, juce::Point<double> target) {
    const CurveNode constraints = model.getNode(index);
    if (constraints.yMovable)
        model.setNodeY(index, (float)target.y);
    int newIndex = index;
    if (constraints.xMovable)
        newIndex = model.setNodeX(index, target.x).newIndex;
    return newIndex;
}

int CurveEditorComponent::applyNodeTarget(int index, juce::Point<double> target) {
    const int newIndex = moveNodeInModel(model_, index, target);

    if (selectedIndex_ == index)
        selectedIndex_ = newIndex;
    if (onNodeChanged)
        onNodeChanged(newIndex);
    if (newIndex != index && onPointsChanged)
        onPointsChanged();
    repaint();
    refreshAccessibilityValue();
    return newIndex;
}

void CurveEditorComponent::dragBendBy(int segment, float deltaPixelsY) {
    if (segment < 0 || segment >= model_.getNumSegments())
        return;
    if (!model_.isBendable(segment))
        return;

    const float startLevel = model_.getNode(segment).y;
    const float endLevel = model_.getNode(segment + 1).y;
    if (startLevel == endLevel)
        return; // flat segment: bend has no visible effect

    // Dragging toward the side the curve already bulges increases the bulge, for both a rising
    // and a falling segment: rising (end > start) bulges upward for a positive bend (the curve
    // reads closer to `end` at the midpoint, which is the higher value), so dragging UP (negative
    // pixelsY) should increase bend; falling is the mirror image.
    const float directionSign = (endLevel >= startLevel) ? -1.0f : 1.0f;
    const float bendDelta = directionSign * (deltaPixelsY / kBendSensitivityPx) * 2.0f;
    const float newBend = juce::jlimit(-1.0f, 1.0f, model_.getBend(segment) + bendDelta);

    model_.setBend(segment, newBend);
    if (onBendChanged)
        onBendChanged(segment);
    repaint();
}

void CurveEditorComponent::resetBend(int segment) {
    if (segment < 0 || segment >= model_.getNumSegments())
        return;
    if (!model_.isBendable(segment))
        return;

    beginGesture();
    model_.setBend(segment, 0.0f);
    if (onBendChanged)
        onBendChanged(segment);
    endGesture(); // change callback fires before the gesture closes -- see CurveEditorGridSnapTests
    repaint();
}

int CurveEditorComponent::addPointAt(juce::Point<float> point, bool bypassSnap) {
    if (model_.getMode() != CurveMode::Free)
        return -1;

    const auto geometry = currentGeometry();
    const juce::Point<double> snapped =
        snapModelPoint(geometry, {geometry.timeForX(point.x), (double)geometry.levelForY(point.y)}, bypassSnap);

    beginGesture();
    const int index = model_.addPoint(snapped.x, (float)snapped.y);
    setSelectedIndex(index);
    if (onPointsChanged)
        onPointsChanged();
    endGesture(); // change callback fires before the gesture closes -- see CurveEditorGridSnapTests
    repaint();
    return index;
}

bool CurveEditorComponent::removeNode(int index) {
    if (model_.getMode() != CurveMode::Free)
        return false;
    if (!model_.canRemovePoint(index))
        return false;

    beginGesture();
    const bool removed = model_.removePoint(index);
    if (removed) {
        if (selectedIndex_ == index)
            setSelectedIndex(-1);
        else if (selectedIndex_ > index)
            setSelectedIndex(selectedIndex_ - 1);
        if (onPointsChanged)
            onPointsChanged();
    }
    endGesture(); // change callback fires before the gesture closes -- see CurveEditorGridSnapTests

    if (removed)
        repaint();
    return removed;
}

void CurveEditorComponent::updateHover(juce::Point<float> point) {
    const CurveHitResult hit = hitTest(point);
    if (hit.kind == hoveredKind_ && hit.index == hoveredIndex_)
        return;

    hoveredKind_ = hit.kind;
    hoveredIndex_ = hit.index;
    setMouseCursor(hit.kind != CurveHitKind::None ? juce::MouseCursor::UpDownLeftRightResizeCursor
                                                  : juce::MouseCursor::NormalCursor);
    repaint();
}

void CurveEditorComponent::mouseDown(const juce::MouseEvent& e) {
    const CurveHitResult hit = hitTest(e.position);

    // A right-click starts no drag and opens no gesture -- it either shows a context menu (if a
    // caller installed one) or is a plain no-op.
    if (e.mods.isPopupMenu()) {
        dragKind_ = DragKind::None;
        dragIndex_ = -1;
        if (onContextMenu)
            onContextMenu(e, hit);
        return;
    }

    // No gesture opened here -- a plain click that never becomes a drag would otherwise push an
    // empty undo step. beginGesture happens on the first mouseDrag (mirrors EQCurveComponent).
    dragKind_ = (hit.kind == CurveHitKind::Node)         ? DragKind::Node
                : (hit.kind == CurveHitKind::BendHandle) ? DragKind::Bend
                                                         : DragKind::None;
    dragIndex_ = hit.index;
    lastDragPos_ = e.position;

    if (hit.kind == CurveHitKind::Node) {
        // Freeze the visible range for the duration of this drag, computed from the model as it
        // stood right now (before any state changes) -- see `dragFrozenRange_`'s doc comment.
        // Never frozen for a bend-handle drag: bend never moves x, nothing to freeze against.
        dragFrozenRange_ = currentGeometry().getVisibleRange();
        setSelectedIndex(hit.index);
    }
    if (dragKind_ != DragKind::None)
        repaint();
}

void CurveEditorComponent::mouseDrag(const juce::MouseEvent& e) {
    if (dragKind_ == DragKind::None)
        return;

    if (!gestureActive_) {
        beginGesture();
        gestureActive_ = true;
    }

    if (dragKind_ == DragKind::Node) {
        dragIndex_ = dragNodeTo(dragIndex_, e.position, e.mods.isShiftDown());
    } else if (dragKind_ == DragKind::Bend) {
        const float deltaY = e.position.y - lastDragPos_.y;
        dragBendBy(dragIndex_, deltaY);
    }
    lastDragPos_ = e.position;
}

void CurveEditorComponent::mouseUp(const juce::MouseEvent&) {
    if (gestureActive_) {
        endGesture();
        gestureActive_ = false;
    }
    dragKind_ = DragKind::None;
    dragIndex_ = -1;
    if (dragFrozenRange_.has_value()) {
        dragFrozenRange_.reset();
        repaint(); // re-fit the view now that the frozen range no longer applies
    }
}

void CurveEditorComponent::mouseMove(const juce::MouseEvent& e) { updateHover(e.position); }

void CurveEditorComponent::mouseExit(const juce::MouseEvent&) {
    if (hoveredKind_ != CurveHitKind::None) {
        hoveredKind_ = CurveHitKind::None;
        hoveredIndex_ = -1;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void CurveEditorComponent::mouseDoubleClick(const juce::MouseEvent& e) {
    const CurveHitResult hit = hitTest(e.position);
    if (hit.kind == CurveHitKind::BendHandle) {
        resetBend(hit.index);
        return;
    }
    if (model_.getMode() != CurveMode::Free)
        return;
    if (hit.kind == CurveHitKind::Node)
        removeNode(hit.index);
    else
        addPointAt(e.position, e.mods.isShiftDown());
}

void CurveEditorComponent::resized() { repaint(); }

} // namespace synth::ui
