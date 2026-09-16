#include "CurveEditorComponent.h"

namespace synth::ui {

CurveEditorComponent::CurveEditorComponent() { setWantsKeyboardFocus(false); }

CurveEditorGeometry CurveEditorComponent::currentGeometry() const {
    return CurveEditorGeometry(model_, getLocalBounds().toFloat(), geometryConfig_);
}

void CurveEditorComponent::setModel(CurveModel model) {
    model_ = std::move(model);
    dragKind_ = DragKind::None;
    dragIndex_ = -1;
    hoveredKind_ = CurveHitKind::None;
    hoveredIndex_ = -1;
    selectedIndex_ = -1;
    repaint();
}

void CurveEditorComponent::setMinVisibleRange(double minRange) {
    geometryConfig_.minVisibleRange = minRange;
    repaint();
}

void CurveEditorComponent::setVisibleRangeOverride(std::optional<double> range) {
    geometryConfig_.explicitVisibleRange = range;
    repaint();
}

void CurveEditorComponent::setTimeLabelFormatter(std::function<juce::String(double)> formatter) {
    timeLabelFormatter_ = formatter ? std::move(formatter) : &CurveEditorGeometry::defaultTimeLabel;
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

int CurveEditorComponent::dragNodeTo(int index, juce::Point<float> point) {
    if (index < 0 || index >= model_.getNumNodes())
        return index;

    const auto geometry = currentGeometry();
    const CurveNode constraints = model_.getNode(index);

    if (constraints.yMovable)
        model_.setNodeY(index, geometry.levelForY(point.y));

    int newIndex = index;
    if (constraints.xMovable) {
        const double time = geometry.timeForX(point.x);
        newIndex = model_.setNodeX(index, time).newIndex;
    }

    if (onNodeChanged)
        onNodeChanged(newIndex);
    if (newIndex != index && onPointsChanged)
        onPointsChanged();
    repaint();
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
    endGesture();
    if (onBendChanged)
        onBendChanged(segment);
    repaint();
}

int CurveEditorComponent::addPointAt(juce::Point<float> point) {
    if (model_.getMode() != CurveMode::Free)
        return -1;

    const auto geometry = currentGeometry();
    const double time = geometry.timeForX(point.x);
    const float level = geometry.levelForY(point.y);

    beginGesture();
    const int index = model_.addPoint(time, level);
    endGesture();

    selectedIndex_ = index;
    if (onPointsChanged)
        onPointsChanged();
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
    endGesture();

    if (removed) {
        if (selectedIndex_ == index)
            selectedIndex_ = -1;
        if (onPointsChanged)
            onPointsChanged();
        repaint();
    }
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
    // No gesture opened here -- a plain click that never becomes a drag would otherwise push an
    // empty undo step. beginGesture happens on the first mouseDrag (mirrors EQCurveComponent).
    const CurveHitResult hit = hitTest(e.position);
    dragKind_ = (hit.kind == CurveHitKind::Node)         ? DragKind::Node
                : (hit.kind == CurveHitKind::BendHandle) ? DragKind::Bend
                                                         : DragKind::None;
    dragIndex_ = hit.index;
    lastDragPos_ = e.position;

    if (hit.kind == CurveHitKind::Node)
        selectedIndex_ = hit.index;
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
        dragIndex_ = dragNodeTo(dragIndex_, e.position);
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
        addPointAt(e.position);
}

void CurveEditorComponent::resized() { repaint(); }

} // namespace synth::ui
