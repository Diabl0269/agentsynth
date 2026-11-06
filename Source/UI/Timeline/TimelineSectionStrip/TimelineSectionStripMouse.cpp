// TimelineSectionStripMouse.cpp
//
// The Sections strip's pointer gestures: make a section by dragging across empty strip (outline snapped to whole bars,
// stopping at a neighbour), select / rename / menu on a block, hover. The drag of a placed block (move, place again)
// starts from pressedBlock_ and hooks into dragPressedBlock / releasePressedBlock below.

#include "UI/Layout/DragCursor.h"
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
    blockDragging_ = false;
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
        selectPlacement(id);
        pressedBlock_ = id;
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
        releasePressedBlock(e);
        pressedBlock_ = {};
        blockDragging_ = false;
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

// The drag of a placed block (move, Option-drag to place again) is not wired yet: a press on a block only selects it.
void TimelineSectionStrip::dragPressedBlock(const juce::MouseEvent& e) { juce::ignoreUnused(e); }

void TimelineSectionStrip::releasePressedBlock(const juce::MouseEvent& e) { juce::ignoreUnused(e); }

void TimelineSectionStrip::cancelBlockDrag() {}

void TimelineSectionStrip::setHoveredBlock(synth::PlacementId id) {
    if (id == hovered_)
        return;
    hovered_ = id;
    setMouseCursor(id.isValid() ? dragGrabCursor() : juce::MouseCursor::NormalCursor);
    repaint();
}

void TimelineSectionStrip::mouseMove(const juce::MouseEvent& e) { setHoveredBlock(blockAt(e.position)); }

void TimelineSectionStrip::mouseExit(const juce::MouseEvent&) { setHoveredBlock({}); }

} // namespace synth::ui
