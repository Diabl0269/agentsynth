// TimelineSectionStripMouse.cpp
//
// The Sections strip's pointer gestures: make a section by dragging across empty strip (outline snapped to whole bars,
// stopping at a neighbour), select / rename / menu on a block, hover. The drag of a placed block (move, place again)
// starts from pressedBlock_ and hooks into dragPressedBlock / releasePressedBlock below.

#include "UI/Layout/DragCursor.h"
#include "UI/Layout/KeyGlide/KeyedTween.h"
#include "UI/Layout/ReorderDrag/ReorderDragAnimator.h"
#include "UI/Timeline/TimelineSectionStrip/TimelineSectionStrip.h"

namespace synth::ui {

std::vector<sectionstrip::Span> TimelineSectionStrip::blockSpans() const {
    std::vector<sectionstrip::Span> spans;
    if (doc_ == nullptr)
        return spans;
    for (const auto& placement : doc_->getPlacements())
        if (const auto* section = doc_->getSection(placement.section))
            spans.push_back({placement.startBeat, placement.startBeat + section->lengthBeats});
    return spans;
}

void TimelineSectionStrip::mouseDown(const juce::MouseEvent& e) {
    // Any open name editor commits first (clicking away is a commit), before the press can change what a block is.
    finishRename(true);
    pressPos_ = e.position;
    pressedBlock_ = {};
    pressedOnChip_ = false;
    blockDragging_ = false;
    dragCancelled_ = false;
    makeArmed_ = false;
    makeDragging_ = false;
    if (doc_ == nullptr)
        return;

    const auto id = blockAt(e.position);
    if (e.mods.isPopupMenu()) {
        if (id.isValid()) {
            selectPlacement(id);
            openSectionContextMenu(id);
        } else {
            openStripContextMenu();
        }
        return;
    }
    if (id.isValid()) {
        grabKeyboardFocus(); // a press on a block always lands the keys here, whatever held focus before
        selectPlacement(id);
        pressedBlock_ = id;
        pressedOnChip_ = chipAt(e.position) == id; // opens its menu on release, unless the press became a drag
        return;
    }
    makeArmed_ = true;
    makeAnchorBeat_ = beatAtX((double)e.position.x);
}

void TimelineSectionStrip::beginMakeDrag(const juce::MouseEvent& e) {
    makeDragging_ = true;
    updateMakeDrag(e);
}

void TimelineSectionStrip::updateMakeDrag(const juce::MouseEvent& e) { updateMakeDragAt(e.position); }

// The outline at a pointer position; a drag event and an edge-scroll tick both land here.
void TimelineSectionStrip::updateMakeDragAt(juce::Point<float> pos) {
    lastDragPointer_ = pos;
    updateEdgeScrollArming();
    const auto span = sectionstrip::makeDragSpan(makeAnchorBeat_, beatAtX((double)pos.x), beatsPerBar(), blockSpans());
    if (span.start == makeDragSpan_.start && span.end == makeDragSpan_.end)
        return;
    makeDragSpan_ = span;
    repaint();
}

void TimelineSectionStrip::endMakeDrag(bool commit) {
    const bool was = makeDragging_;
    const auto span = makeDragSpan_;
    makeDragging_ = false;
    stopEdgeScroll();
    makeArmed_ = false;
    makeDragSpan_ = {};
    if (was)
        repaint();
    growFromRight_ = was && makeAnchorBeat_ > (span.start + span.end) * 0.5;
    if (commit && was && sectionstrip::spansAtLeastOneBar(span, beatsPerBar()))
        makeSectionFromRange(span.start, span.end);
}

void TimelineSectionStrip::mouseDrag(const juce::MouseEvent& e) {
    if (pressedBlock_.isValid()) {
        dragPressedBlock(e);
        return;
    }
    if (!makeArmed_)
        return;
    if (!makeDragging_) {
        if (e.position.getDistanceFrom(pressPos_) < sectionstrip::kDragStartPx)
            return;
        beginMakeDrag(e);
        return;
    }
    updateMakeDrag(e);
}

void TimelineSectionStrip::mouseUp(const juce::MouseEvent& e) {
    if (pressedBlock_.isValid()) {
        const auto id = pressedBlock_;
        const bool chipClick = pressedOnChip_ && !blockDragging_ && !e.mouseWasDraggedSinceMouseDown();
        releasePressedBlock(e);
        pressedBlock_ = {};
        pressedOnChip_ = false;
        blockDragging_ = false;
        if (chipClick)
            openVariationMenu(id);
        return;
    }
    if (makeDragging_) {
        endMakeDrag(true);
        return;
    }
    // A plain click on empty strip clears the selection.
    if (makeArmed_ && !e.mouseWasDraggedSinceMouseDown())
        selectPlacement({});
    makeArmed_ = false;
}

void TimelineSectionStrip::mouseDoubleClick(const juce::MouseEvent& e) {
    const auto id = blockAt(e.position);
    if (id.isValid())
        beginRenameSection(id);
}

bool TimelineSectionStrip::cancelGesture() {
    const bool any = makeDragging_ || blockDragging_;
    if (makeDragging_)
        endMakeDrag(false);
    if (blockDragging_) {
        cancelBlockDrag();
        blockDragging_ = false;
    }
    makeArmed_ = false;
    return any;
}

//==============================================================================
// The drag of a placed block. Option held at the first drag event makes it a copy (place again), else it moves; the
// drop beat snaps (sectionstrip::dropBeat); a release above the strip, or Esc, changes nothing.

void TimelineSectionStrip::dragPressedBlock(const juce::MouseEvent& e) {
    if (dragCancelled_)
        return;
    if (!blockDragging_) {
        if (e.position.getDistanceFrom(pressPos_) < ReorderDragAnimator::kDragThresholdPx)
            return;
        beginBlockDrag(e);
        if (!blockDragging_)
            return;
    }
    updateBlockDrag(e);
}

void TimelineSectionStrip::beginBlockDrag(const juce::MouseEvent& e) {
    for (const auto& block : buildBlocks()) {
        if (block.id != pressedBlock_)
            continue;
        dragBlock_ = block;
        dragCopy_ = e.mods.isAltDown();
        settlingId_ = {};
        dragGrabOffsetPx_ = pressPos_.x - block.bounds.getX();
        blockDragging_ = true;
        showDragCursor(*this, dragCopy_);
        motion_.liftTo(true);
        return;
    }
}

void TimelineSectionStrip::updateBlockDrag(const juce::MouseEvent& e) {
    updateBlockDragAt(e.position, e.mods.isShiftDown());
}

// The preview of a block drag at a pointer position: the ghost under the pointer, and the plan of what a release there
// does (sectionstrip::planSectionDrop): the drop line at the landing, the other blocks easing to where the drop puts
// them. A drag event, a Shift press or release, and an edge-scroll tick all land here.
void TimelineSectionStrip::updateBlockDragAt(juce::Point<float> pos, bool shift) {
    lastDragPointer_ = pos;
    lastDragShift_ = shift;
    dragGhostX_ = pos.x - dragGrabOffsetPx_;
    dragOutside_ = pos.y < 0.0f;
    dragMakeRoom_ = dropMode(shift) == synth::SectionDropMode::MakeRoom;
    setDropPlan(sectionstrip::planSectionDrop(placedSpans(), pressedBlock_.value, beatAtX((double)dragGhostX_),
                                              dragCopy_, dragMakeRoom_, beatsPerBar(), viewState_.pixelsPerBeat));
    updateEdgeScrollArming();
    repaint();
}

void TimelineSectionStrip::modifierKeysChanged(const juce::ModifierKeys& mods) {
    if (blockDragging_ && !dragCancelled_ && mods.isShiftDown() != lastDragShift_)
        updateBlockDragAt(lastDragPointer_, mods.isShiftDown());
}

std::vector<sectionstrip::PlacedSpan> TimelineSectionStrip::placedSpans() const {
    std::vector<sectionstrip::PlacedSpan> spans;
    if (doc_ != nullptr)
        for (const auto& placement : doc_->getPlacements())
            if (const auto* section = doc_->getSection(placement.section))
                spans.push_back({placement.id.value, placement.startBeat, section->lengthBeats});
    return spans;
}

// Every other block eases from where it is drawn now to where the new plan puts it; a block the old and new plans
// agree on keeps its glide.
void TimelineSectionStrip::setDropPlan(sectionstrip::DropPlan plan) {
    if (doc_ == nullptr)
        return;
    for (const auto& placement : doc_->getPlacements()) {
        if (placement.id == pressedBlock_ && !dragCopy_)
            continue;
        const double was =
            hasDragPlan_ ? dragPlan_.startOf(placement.id.value).value_or(placement.startBeat) : placement.startBeat;
        const double now = plan.startOf(placement.id.value).value_or(placement.startBeat);
        if (std::abs(was - now) > sectionstrip::kBarEpsilon)
            dropGlide_->retarget(placement.id.value, dropGlide_->drawnAt(placement.id.value, {(float)was, 0.0f}),
                                 {(float)now, 0.0f});
    }
    dragPlan_ = std::move(plan);
    hasDragPlan_ = true;
}

// The drag ended with nothing written: the blocks it moved aside ease back to where they are.
void TimelineSectionStrip::clearDropPlan() {
    if (hasDragPlan_)
        setDropPlan({});
    hasDragPlan_ = false;
    dragPlan_ = {};
}

double TimelineSectionStrip::drawnStartOf(synth::PlacementId id, double docStart) const {
    const double resting = hasDragPlan_ ? dragPlan_.startOf(id.value).value_or(docStart) : docStart;
    return (double)dropGlide_->drawnAt(id.value, {(float)resting, 0.0f}).x;
}

double TimelineSectionStrip::getDrawnStartBeatForTest(synth::PlacementId id) const {
    const auto* placement = doc_ != nullptr ? doc_->getPlacement(id) : nullptr;
    return placement == nullptr ? 0.0 : drawnStartOf(id, placement->startBeat);
}

juce::Rectangle<float> TimelineSectionStrip::getDragGhostBoundsForTest() const {
    const float lift = motion_.lift();
    const float x = blockDragging_ ? dragGhostX_ : dragSettleX_ + (dragGhostX_ - dragSettleX_) * lift;
    return dragBlock_.bounds.withX(x);
}

void TimelineSectionStrip::releasePressedBlock(const juce::MouseEvent& e) {
    if (!blockDragging_)
        return;
    const bool commit = !dragCancelled_ && e.position.y >= 0.0f && doc_ != nullptr;
    if (commit)
        updateBlockDragAt(e.position, e.mods.isShiftDown()); // the release's own place and Shift are the last word
    const auto id = pressedBlock_;
    const auto section = dragBlock_.section;
    const double target = dragPlan_.landing;
    const auto mode = dragMakeRoom_ ? synth::SectionDropMode::MakeRoom : synth::SectionDropMode::Cover;
    const bool copy = dragCopy_;
    const double spb = secondsPerBeat();
    const bool moved = std::abs(target - dragBlock_.startBeat) > sectionstrip::kBarEpsilon;
    blockDragging_ = false;
    stopEdgeScroll(); // after the last re-plan, which re-arms it while the drag still holds
    endDragCursor(*this, hovered_.isValid() ? dragGrabCursor() : juce::MouseCursor());
    motion_.liftTo(false);
    if (!commit || (!copy && !moved)) {
        dragSettleX_ = (float)xAtBeat(dragBlock_.startBeat); // the view may have scrolled since the press
        clearDropPlan();
        repaint();
        return;
    }
    // A cover drop takes the blocks under the landing away: the first of them shrinks out where it was.
    for (const auto& other : dragPlan_.others)
        if (other.covered) {
            if (const auto ghost = ghostOf(synth::PlacementId{other.id}))
                motion_.startExit(*ghost);
            break;
        }
    dragSettleX_ = (float)xAtBeat(target);
    synth::PlacementId placed;
    if (copy)
        performEdit(
            [this, section, target, mode, spb, &placed] { placed = doc_->placeSection(section, target, mode, spb); });
    else
        performEdit([this, id, target, mode, spb] { doc_->movePlacement(id, target, mode, spb); });
    // The doc now has every block where the plan drew it, so the glides in flight carry on toward the same places;
    // refreshing while the plan is still held tells trackChangesForMotion not to start glides of its own.
    refresh();
    hasDragPlan_ = false;
    dragPlan_ = {};
    // The edit can rebuild the panel (which clears the selection); the block just dropped stays the selected one.
    settlingId_ = placed.isValid() ? placed : id;
    selectPlacement(settlingId_);
}

// Esc mid-drag: the ghost settles back where the block is and the blocks it moved aside ease back; nothing is written.
// Later drag events of the same press are ignored.
void TimelineSectionStrip::cancelBlockDrag() {
    dragCancelled_ = true;
    stopEdgeScroll();
    dragSettleX_ = (float)xAtBeat(dragBlock_.startBeat);
    clearDropPlan();
    endDragCursor(*this, hovered_.isValid() ? dragGrabCursor() : juce::MouseCursor());
    motion_.liftTo(false);
    repaint();
}

void TimelineSectionStrip::setHoveredBlock(synth::PlacementId id, bool overChip) {
    if (id == hovered_ && overChip == hoveredChip_)
        return;
    hoveredChip_ = overChip;
    if (id == hovered_) {
        repaint();
        return;
    }
    hovered_ = id;
    setMouseCursor(id.isValid() ? dragGrabCursor() : juce::MouseCursor::NormalCursor);
    repaint();
}

void TimelineSectionStrip::mouseMove(const juce::MouseEvent& e) {
    const auto id = blockAt(e.position);
    setHoveredBlock(id, id.isValid() && chipAt(e.position) == id);
}

void TimelineSectionStrip::mouseExit(const juce::MouseEvent&) { setHoveredBlock({}); }

} // namespace synth::ui
