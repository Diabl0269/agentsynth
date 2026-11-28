#include "CurveEditorComponent.h"

#include "CurveEditorStateProbe.h"
#include "UI/Layout/PointEditing/SegmentBendDrag.h"
#include "UI/Timeline/AutomationLanes/LaneShapes/DrawShape.h"
#include "UI/Timeline/AutomationLanes/LaneToolCursors.h"
#include <cmath>

namespace synth::ui {

CurveEditorComponent::CurveEditorComponent() {
    setWantsKeyboardFocus(true);
    stateProbe_ = makeCurveEditorStateProbe(*this);
    addChildComponent(*stateProbe_);
    stateProbe_->setVisible(true);
    setTitle("Curve editor");
    setDescription(
        "Breakpoint curve. Left and Right pick a point, Shift with them selects several, Alt with an arrow "
        "moves the selection and stops at a neighbour, Alt and Shift with Left or Right moves one point past it, "
        "Control Shift with an arrow moves it finely, Up and Down change its level, Control "
        "T inserts a point after the current one, Delete removes it.");
    // The tip is the name and the keys to start with; the full list is the description above and the module help
    // (docs/modules/modulation.md "Curve editor keys"). A long how-to hid the whole editor on a recording.
    setTooltip("Curve editor: Left/Right pick a point, Alt+arrows move it, double-click adds or removes");
    keyHold_.speedFactor = [this](KeyAxis) { return keyDragSpeedFactor(); };
    keyHold_.onFrame = [this] { applyKeyDrag(false); };
    keyHold_.onEnd = [this] { endKeyDrag(); };
    cursorProbe_ = makeLaneStateProbe("curveEditorCursor", [this](juce::DynamicObject& into) {
        into.setProperty("cursor", laneCursorKind(getMouseCursor()));
        const auto pen = getDrawPen();
        into.setProperty("drawPen", pen.has_value() ? juce::String(drawShapeName(*pen)) : juce::String());
        into.setProperty("drawChip", draw_.chipText()); // the live chip of a stroke in flight ("8 peaks, ...")
    });
    addAndMakeVisible(*cursorProbe_);
}

CurveEditorGeometry CurveEditorComponent::currentGeometry() const {
    CurveGeometryConfig config = geometryConfig_;
    if (!config.explicitVisibleRange.has_value() && dragFrozenRange_.has_value())
        config.explicitVisibleRange = dragFrozenRange_;
    return CurveEditorGeometry(model_, getLocalBounds().toFloat(), config);
}

void CurveEditorComponent::setModel(CurveModel model, bool glideMoved) {
    draw_.cancel(); // a stroke in flight was drawn on the old curve
    const auto placesBefore = glideMoved ? nodePositions() : std::vector<juce::Point<float>>();
    const auto bendsBefore = glideMoved ? drawnBends() : std::vector<float>();
    auto lineBefore = glideMoved ? lineBeforeEdit() : std::vector<juce::Point<float>>();
    const int oldNumNodes = model_.getNumNodes();
    const CurveMode oldMode = model_.getMode();
    // An undo of a delete or redo of an add (and the reverse) is one dot more or less: keep the others steady.
    const auto singleEdit = glideMoved ? singleDotEdit(model_, model) : std::nullopt;
    model_ = std::move(model);

    const int newNumNodes = model_.getNumNodes();
    const bool topologyChanged = newNumNodes != oldNumNodes || model_.getMode() != oldMode;
    const bool indexOutOfRange =
        dragIndex_ >= newNumNodes || hoveredIndex_ >= newNumNodes || cursorIndex() >= newNumNodes;

    if (topologyChanged || indexOutOfRange) {
        if (gestureActive_) {
            endGesture(); // pair the gesture right here -- a mouseUp can no longer arrive
                          // meaningfully for the old topology
            gestureActive_ = false;
        }
        dragKind_ = DragKind::None;
        dragIndex_ = -1;
        dragFrozenRange_.reset();
        boxSelect_.cancel();
        hoveredKind_ = CurveHitKind::None;
        hoveredIndex_ = -1;
        selection_ = {};
        pendingToggleIndex_ = -1;
        pendingCollapseIndex_ = -1;
    }
    if (topologyChanged) {
        if (glideMoved) {
            if (singleEdit)
                glideLineFrom(std::move(lineBefore), *singleEdit);
            else
                glideLineFrom(std::move(lineBefore));
        } else {
            pointTween_.clear();
            bendTween_.clear();
            morphTween_.clear();
            morphFrom_.clear();
            editMorph_ = {};
        }
    } else if (glideMoved) {
        glideNodesFrom(placesBefore);
        glideBendsFrom(bendsBefore);
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

void CurveEditorComponent::setBackdropPainter(BackdropPainter painter) {
    backdrop_ = std::move(painter);
    repaint();
}

void CurveEditorComponent::setAxisGuides(std::optional<AxisGuides> guides) {
    guides_ = std::move(guides);
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
    // The marker is a hairline down the editor plus a dot, both drawn at paint time from the playhead and the
    // current geometry, so the strips it leaves and takes are all that change; a curve edit repaints whole itself.
    if (playhead_.has_value())
        repaint(playheadMarkerBounds(*playhead_));
    playhead_ = playhead;
    if (playhead_.has_value())
        repaint(playheadMarkerBounds(*playhead_));
}

juce::Rectangle<int> CurveEditorComponent::playheadMarkerBounds(const CurvePlayhead& playhead) const {
    const float x = currentGeometry().playheadPosition(playhead).x;
    // The dot reaches 4 px either side of x and the hairline is one px from (int)x; 1 px more for the dot's soft edge.
    return juce::Rectangle<float>(x - 5.0f, 0.0f, 10.0f, (float)getHeight()).getSmallestIntegerContainer();
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
    double y = point.y;
    if (gridHasY() && grid_->snapY) {
        const double yDiv = (double)grid_->yDivisions;
        y = std::round(juce::jlimit(0.0, 1.0, point.y) * yDiv) / yDiv;
    }
    return {snapGridX(point.x), y};
}

int CurveEditorComponent::dragNodeTo(int index, juce::Point<float> point, bool bypassSnap, bool pass) {
    if (index < 0 || index >= model_.getNumNodes())
        return index;

    const auto geometry = currentGeometry();
    const juce::Point<double> snapped =
        snapModelPoint(geometry, {geometry.timeForX(point.x), (double)geometry.levelForY(point.y)}, bypassSnap);
    return applyNodeTarget(index, snapped, pass);
}

int CurveEditorComponent::moveNodeInModel(CurveModel& model, int index, juce::Point<double> target, bool pass) {
    const CurveNode constraints = model.getNode(index);
    if (constraints.yMovable)
        model.setNodeY(index, (float)target.y);
    int newIndex = index;
    if (constraints.xMovable)
        newIndex = model.setNodeX(index, target.x, pass).newIndex;
    return newIndex;
}

int CurveEditorComponent::applyNodeTarget(int index, juce::Point<double> target, bool pass) {
    const int newIndex = moveNodeInModel(model_, index, target, pass);

    if (newIndex != index)
        remapSelectionForMove(index, newIndex);
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

    const float directionSign = upwardBendSign(startLevel, endLevel);
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

int CurveEditorComponent::insertPointAt(juce::Point<float> point, bool bypassSnap) {
    if (model_.getMode() != CurveMode::Free || model_.getNumNodes() >= CurveModel::kMaxNodes)
        return -1;

    const auto geometry = currentGeometry();
    const juce::Point<double> snapped =
        snapModelPoint(geometry, {geometry.timeForX(point.x), (double)geometry.levelForY(point.y)}, bypassSnap);

    auto lineBefore = lineBeforeEdit();
    const int index = model_.addPoint(snapped.x, (float)snapped.y);
    if (index >= 0)
        glideLineFrom(std::move(lineBefore), {true, index, index - 1, index});
    setSelectedIndex(index);
    if (onPointsChanged)
        onPointsChanged();
    repaint();
    return index;
}

int CurveEditorComponent::addPointAt(juce::Point<float> point, bool bypassSnap) {
    if (model_.getMode() != CurveMode::Free || model_.getNumNodes() >= CurveModel::kMaxNodes)
        return -1;
    beginGesture();
    const int index = insertPointAt(point, bypassSnap);
    endGesture(); // change callback fires before the gesture closes -- see CurveEditorGridSnapTests
    return index;
}

bool CurveEditorComponent::removeNode(int index) {
    if (model_.getMode() != CurveMode::Free)
        return false;
    if (!model_.canRemovePoint(index))
        return false;

    beginGesture();
    auto lineBefore = lineBeforeEdit();
    const bool removed = model_.removePoint(index);
    if (removed) {
        glideLineFrom(std::move(lineBefore), {true, -1, index - 1, index - 1});
        std::vector<int> kept;
        for (const int j : selection_.selected())
            if (j != index)
                kept.push_back(j > index ? j - 1 : j);
        const auto shift = [index](int j) { return j > index ? j - 1 : j; };
        const int anchor = selection_.anchor().value_or(-1);
        setSelection(std::move(kept), cursorIndex() == index ? -1 : shift(cursorIndex()),
                     anchor == index ? -1 : shift(anchor));
        if (onPointsChanged)
            onPointsChanged();
    }
    endGesture(); // change callback fires before the gesture closes -- see CurveEditorGridSnapTests

    if (removed)
        repaint();
    return removed;
}

void CurveEditorComponent::updateHover(juce::Point<float> point) {
    const auto lineBefore = highlightedLine(currentGeometry());
    pointer_ = point;
    if (lineBefore != highlightedLine(currentGeometry()))
        repaint();
    if (draw_.getPen().has_value())
        return; // the pen's crosshair stays; there are no points to hover
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
    if (draw_.press(e))
        return;
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
        pendingToggleIndex_ = -1;
        pendingCollapseIndex_ = -1;
        const bool additive = e.mods.isShiftDown() || e.mods.isCommandDown() || e.mods.isCtrlDown();
        if (additive && isSelected(hit.index))
            pendingToggleIndex_ = hit.index; // a click removes it; a drag keeps it and moves the group
        else if (additive)
            toggleSelected(hit.index);
        else if (isSelected(hit.index) && selection_.size() > 1) {
            pendingCollapseIndex_ = hit.index; // a click collapses to it; a drag moves the group
            setSelection(selection_.selected(), hit.index, selection_.anchor().value_or(-1));
        } else
            setSelectedIndex(hit.index);
    }
    if (dragKind_ != DragKind::None)
        repaint();
    else if (hit.kind == CurveHitKind::None)
        boxSelect_.press(e.position.roundToInt(), e.mods.isShiftDown() || e.mods.isCommandDown() || e.mods.isCtrlDown(),
                         selection_.selected());
}

void CurveEditorComponent::mouseDrag(const juce::MouseEvent& e) {
    if (draw_.drag(e))
        return;
    if (dragKind_ == DragKind::None) {
        dragBoxTo(e.position);
        return;
    }

    // A press that has not travelled the drag threshold is still a click (a hand wobbles a pixel or two): no gesture
    // starts, so the release can still settle a click on one of a group or toggle a point.
    if (!gestureActive_ && (pendingCollapseIndex_ >= 0 || pendingToggleIndex_ >= 0) &&
        !e.mouseWasDraggedSinceMouseDown())
        return;
    if (!gestureActive_) {
        beginGesture();
        gestureActive_ = true;
    }

    if (dragKind_ == DragKind::Node) {
        if (selection_.size() > 1 && isSelected(dragIndex_))
            dragSelectionTo(dragIndex_, e.position, e.mods.isShiftDown());
        else
            dragIndex_ = dragNodeTo(dragIndex_, e.position, e.mods.isShiftDown(), e.mods.isAltDown());
    } else if (dragKind_ == DragKind::Bend) {
        const float deltaY = e.position.y - lastDragPos_.y;
        dragBendBy(dragIndex_, deltaY);
    }
    lastDragPos_ = e.position;
}

void CurveEditorComponent::mouseUp(const juce::MouseEvent& e) {
    if (draw_.release(e))
        return;
    endBox();
    if (!gestureActive_) {
        if (pendingToggleIndex_ >= 0)
            toggleSelected(pendingToggleIndex_);
        else if (pendingCollapseIndex_ >= 0)
            setSelectedIndex(pendingCollapseIndex_);
    }
    pendingToggleIndex_ = -1;
    pendingCollapseIndex_ = -1;
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
    if (pointer_.has_value()) {
        const bool lit = highlightedLine(currentGeometry()).has_value();
        pointer_.reset();
        if (lit)
            repaint();
    }
    if (hoveredKind_ != CurveHitKind::None) {
        hoveredKind_ = CurveHitKind::None;
        hoveredIndex_ = -1;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void CurveEditorComponent::mouseDoubleClick(const juce::MouseEvent& e) {
    if (draw_.getPen().has_value())
        return; // the pen draws; points are edited with it off
    const CurveHitResult hit = hitTest(e.position);
    if (hit.kind == CurveHitKind::BendHandle) {
        resetBend(hit.index);
        return;
    }
    if (model_.getMode() != CurveMode::Free)
        return;
    if (hit.kind == CurveHitKind::Node) {
        // Same path as Delete: just this dot goes, and the selection moves to the next one.
        setSelectedIndex(hit.index);
        removeSelectedNodes();
    } else if (!(onEmptyDoubleClick && onEmptyDoubleClick(e)))
        addPointAt(e.position, e.mods.isShiftDown());
}

void CurveEditorComponent::resized() { repaint(); }

} // namespace synth::ui
