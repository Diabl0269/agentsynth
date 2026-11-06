// TimelineSectionStripMouse.cpp
//
// The Sections strip's pointer gestures: make a section by dragging across empty strip (outline snapped to whole bars,
// stopping at a neighbour), select / rename / menu on a block, hover. The drag of a placed block (move, place again)
// starts from pressedBlock_ and hooks into dragPressedBlock / releasePressedBlock below.

#include "UI/Layout/DragCursor.h"
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

void TimelineSectionStrip::updateMakeDrag(const juce::MouseEvent& e) {
    const auto span =
        sectionstrip::makeDragSpan(makeAnchorBeat_, beatAtX((double)e.position.x), beatsPerBar(), blockSpans());
    if (span.start == makeDragSpan_.start && span.end == makeDragSpan_.end)
        return;
    makeDragSpan_ = span;
    repaint();
}

void TimelineSectionStrip::endMakeDrag(bool commit) {
    const bool was = makeDragging_;
    const auto span = makeDragSpan_;
    makeDragging_ = false;
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
        dragGrabOffsetPx_ = pressPos_.x - block.bounds.getX();
        blockDragging_ = true;
        showDragCursor(*this, dragCopy_);
        motion_.liftTo(true);
        return;
    }
}

void TimelineSectionStrip::updateBlockDrag(const juce::MouseEvent& e) {
    dragGhostX_ = e.position.x - dragGrabOffsetPx_;
    dragOutside_ = e.position.y < 0.0f;
    // A move ignores the block's own span, which it is about to leave; a copy leaves the original where it is, so the
    // original counts as an obstacle.
    std::vector<sectionstrip::Span> others;
    if (doc_ != nullptr)
        for (const auto& placement : doc_->getPlacements())
            if (const auto* section = doc_->getSection(placement.section))
                if (dragCopy_ || placement.id != pressedBlock_)
                    others.push_back({placement.startBeat, placement.startBeat + section->lengthBeats});
    dragTargetBeat_ = sectionstrip::dropBeat(beatAtX((double)dragGhostX_), dragBlock_.lengthBeats, beatsPerBar(),
                                             viewState_.pixelsPerBeat, others);
    repaint();
}

void TimelineSectionStrip::releasePressedBlock(const juce::MouseEvent& e) {
    if (!blockDragging_)
        return;
    const bool commit = !dragCancelled_ && e.position.y >= 0.0f && doc_ != nullptr;
    const auto id = pressedBlock_;
    const auto section = dragBlock_.section;
    const double target = dragTargetBeat_;
    const auto mode = dropMode(e.mods.isShiftDown());
    const bool copy = dragCopy_;
    const double spb = secondsPerBeat();
    const bool moved = std::abs(target - dragBlock_.startBeat) > sectionstrip::kBarEpsilon;
    dragSettleX_ = commit ? (float)xAtBeat(target) : dragBlock_.bounds.getX();
    blockDragging_ = false;
    endDragCursor(*this, hovered_.isValid() ? dragGrabCursor() : juce::MouseCursor());
    motion_.liftTo(false);
    if (!commit || (!copy && !moved)) {
        dragSettleX_ = dragBlock_.bounds.getX();
        repaint();
        return;
    }
    synth::PlacementId placed;
    if (copy)
        performEdit(
            [this, section, target, mode, spb, &placed] { placed = doc_->placeSection(section, target, mode, spb); });
    else
        performEdit([this, id, target, mode, spb] { doc_->movePlacement(id, target, mode, spb); });
    refresh();
    // The edit can rebuild the panel (which clears the selection); the block just dropped stays the selected one.
    selectPlacement(placed.isValid() ? placed : id);
}

// Esc mid-drag: the ghost settles back where the block is; nothing is written. Later drag events of the same press
// are ignored.
void TimelineSectionStrip::cancelBlockDrag() {
    dragCancelled_ = true;
    dragSettleX_ = dragBlock_.bounds.getX();
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
