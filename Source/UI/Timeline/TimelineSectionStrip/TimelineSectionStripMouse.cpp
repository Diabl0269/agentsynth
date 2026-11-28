// TimelineSectionStripMouse.cpp
//
// The Sections strip's pointer gestures: make a section by dragging across empty strip (outline snapped to the
// timeline's Snap, free with Shift, stopping at a neighbour), select / rename / menu on a block, hover. The drag of a
// placed block (move, place again) starts from pressedBlock_ and hooks into dragPressedBlock / releasePressedBlock
// below.

#include "UI/Layout/DragCursor.h"
#include "UI/Layout/KeyGlide/KeyedTween.h"
#include "UI/Layout/ReorderDrag/ReorderDragAnimator.h"
#include "UI/Timeline/EdgeAutoScroll.h"
#include "UI/Timeline/TimelineSectionStrip/TimelineSectionStrip.h"
#include "UI/Timeline/TimelineViewState.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
// Where a section drag is drawn along the strip: the pointer kept inside it. A real pointer can leave the window
// mid-drag; the edge scroll reads the real position, the ghost and the outline stay at the edge it left by.
float keptInStrip(float x, int width) {
    return x >= 0.0f && x < (float)width ? x : (float)keepInsideSpan((int)x, 0, width);
}
} // namespace

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
    edgeScroll_.press(e.getPosition());
    pressedBlock_ = {};
    pressedOnChip_ = false;
    pendingCollapse_ = {};
    blockDragging_ = false;
    dragCancelled_ = false;
    makeArmed_ = false;
    makeDragging_ = false;
    if (doc_ == nullptr)
        return;

    const auto id = blockAt(e.position);
    if (e.mods.isPopupMenu()) {
        if (id.isValid()) {
            if (!isPlacementSelected(id))
                selectPlacement(id); // a menu on one of several selected acts on all of them
            openSectionContextMenu(id);
        } else {
            openStripContextMenu();
        }
        return;
    }
    if (id.isValid()) {
        grabKeyboardFocus(); // a press on a block always lands the keys here, whatever held focus before
        pressBlock(e, id);
        return;
    }
    makeArmed_ = true;
    makeAnchorBeat_ = beatAtX((double)e.position.x);
}

// Cmd toggles the block and Shift extends the run to it (neither opens the chip's menu); a drag after either moves the
// resulting set when the block is in it. A plain press on one of several selected keeps the set, so the press can drag
// all of them, and narrows to the block on a release that did not drag.
void TimelineSectionStrip::pressBlock(const juce::MouseEvent& e, synth::PlacementId id) {
    if (e.mods.isCommandDown() || e.mods.isShiftDown()) {
        if (e.mods.isShiftDown())
            extendPlacementSelectionTo(id);
        else
            togglePlacementSelected(id);
        if (isPlacementSelected(id))
            pressedBlock_ = id;
        return;
    }
    if (actsOnSelection(id))
        pendingCollapse_ = id;
    else
        selectPlacement(id);
    pressedBlock_ = id;
    pressedOnChip_ = chipAt(e.position) == id; // opens its menu on release, unless the press became a drag
}

void TimelineSectionStrip::beginMakeDrag(const juce::MouseEvent& e) {
    makeDragging_ = true;
    updateMakeDrag(e);
}

void TimelineSectionStrip::updateMakeDrag(const juce::MouseEvent& e) {
    updateMakeDragAt(e.position, e.mods.isShiftDown());
}

// The grid a section's edges snap to, in beats: the timeline's Snap unit, 0 (free) with Snap Off. `free` (Shift on a
// make drag) places it freely. A block move or drop passes false: Shift there already toggles make-room and cover.
double TimelineSectionStrip::stripGridBeats(bool free) const {
    return sectionstrip::stripGrid(viewState_.divisionBeats(beatsPerBar()), free);
}

// The outline at a pointer position; a drag event and an edge-scroll tick both land here.
void TimelineSectionStrip::updateMakeDragAt(juce::Point<float> pos, bool shift) {
    lastDragPointer_ = pos;
    lastDragShift_ = shift;
    updateEdgeScrollArming();
    const auto span = sectionstrip::makeDragSpan(makeAnchorBeat_, beatAtX((double)keptInStrip(pos.x, getWidth())),
                                                 stripGridBeats(shift), blockSpans());
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
    makeOrigin_.reset();
    if (was)
        makeOrigin_ = makeAnchorBeat_ > (span.start + span.end) * 0.5 ? MotionOrigin::Right : MotionOrigin::Left;
    if (commit && was && sectionstrip::spansAtLeastOneUnit(span, stripGridBeats(lastDragShift_)))
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
        const bool collapse = pendingCollapse_.isValid() && !blockDragging_ && !dragCancelled_;
        releasePressedBlock(e);
        if (collapse)
            selectPlacement(pendingCollapse_);
        pendingCollapse_ = {};
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

// A press on one of several selected blocks carries the whole set; any other press carries just its block.
void TimelineSectionStrip::beginBlockDrag(const juce::MouseEvent& e) {
    for (const auto& block : buildBlocks()) {
        if (block.id != pressedBlock_)
            continue;
        dragBlock_ = block;
        dragCopy_ = e.mods.isAltDown();
        settlingIds_.clear();
        dropHint_.clear(); // a new drag's first words appear with the lift, not as a swap from the last drag's
        dropHintFrom_.clear();
        dragGrabOffsetPx_ = pressPos_.x - block.bounds.getX();
        dragMembers_.clear();
        const auto ids =
            actsOnSelection(pressedBlock_) ? getSelectedPlacements() : std::vector<synth::PlacementId>{pressedBlock_};
        // Each ghost rides at its block's real distance from the group's left edge, so the gaps between members show.
        double groupStart = block.startBeat;
        for (const auto id : ids)
            if (const auto member = blockOf(id))
                groupStart = std::min(groupStart, member->startBeat);
        for (const auto id : ids)
            if (const auto member = id == pressedBlock_ ? std::optional<Block>(block) : blockOf(id)) {
                if (id == pressedBlock_)
                    dragLeadBeats_ = member->startBeat - groupStart;
                dragMembers_.push_back({*member, member->startBeat - groupStart, 0.0f});
            }
        for (auto& member : dragMembers_)
            member.offsetBeats -= dragLeadBeats_;
        blockDragging_ = true;
        showDragCursor(*this, dragCopy_);
        motion_.liftTo(true);
        return;
    }
}

// A block's full description wherever it is, on screen or not (buildBlocks only lists the visible ones).
std::optional<TimelineSectionStrip::Block> TimelineSectionStrip::blockOf(synth::PlacementId id) const {
    const auto* placement = doc_ != nullptr ? doc_->getPlacement(id) : nullptr;
    const auto* section = placement != nullptr ? doc_->getSection(placement->section) : nullptr;
    if (section == nullptr)
        return std::nullopt;
    Block block;
    block.id = id;
    block.section = placement->section;
    block.name = section->name;
    block.colour = colourGlide_.drawn(section->id.value, juce::Colour(section->colourArgb));
    block.startBeat = placement->startBeat;
    block.lengthBeats = section->lengthBeats;
    block.count = (int)doc_->placementsOf(placement->section).size();
    block.ordinal = std::max(1, doc_->placementOrdinal(id));
    block.variation = placement->variation;
    block.variationCount = (int)section->variations.size();
    block.bounds = sectionstrip::blockRect((float)xAtBeat(block.startBeat),
                                           (float)xAtBeat(block.startBeat + block.lengthBeats), getHeight());
    return block;
}

// The unselected sections between the dragged ones (sectionstrip::blocksBetween), whatever the mode, so the hint pill
// is sized for the words that name them before Shift picks the mode.
int TimelineSectionStrip::dragBetweenCount() const {
    std::vector<std::int64_t> members;
    for (const auto& member : dragMembers_)
        members.push_back(member.block.id.value);
    return dragCopy_ ? 0 : (int)sectionstrip::blocksBetween(placedSpans(), members).size();
}

bool TimelineSectionStrip::isDragMember(synth::PlacementId id) const {
    return std::any_of(dragMembers_.begin(), dragMembers_.end(),
                       [id](const DragMember& member) { return member.block.id == id; });
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
    dragGhostX_ = keptInStrip(pos.x, getWidth()) - dragGrabOffsetPx_;
    dragOutside_ = pos.y < 0.0f;
    dragMakeRoom_ = dropMode(shift) == synth::SectionDropMode::MakeRoom;
    std::vector<std::int64_t> members;
    for (const auto& member : dragMembers_)
        members.push_back(member.block.id.value);
    // The group's left edge: the pressed block's ghost less its distance from the group's first member.
    const double groupLeft = (double)dragGhostX_ - dragLeadBeats_ * viewState_.pixelsPerBeat;
    setDropPlan(sectionstrip::planGroupDrop(placedSpans(), members, beatAtX(groupLeft), dragCopy_, dragMakeRoom_,
                                            stripGridBeats(false), viewState_.pixelsPerBeat, clipSpans()));
    updateDropHint(); // after the plan: whether anything moves picks "insert" or "place"
    updateEdgeScrollArming();
    repaint();
}

void TimelineSectionStrip::modifierKeysChanged(const juce::ModifierKeys& mods) {
    if (makeDragging_ && mods.isShiftDown() != lastDragShift_)
        updateMakeDragAt(lastDragPointer_, mods.isShiftDown());
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

// Every clip's span: music loose between blocks counts as taken when the plan decides whether a drop opens room.
std::vector<sectionstrip::Span> TimelineSectionStrip::clipSpans() const {
    std::vector<sectionstrip::Span> spans;
    if (doc_ != nullptr)
        for (const auto& track : doc_->getTracks())
            for (const auto& clip : track.clips)
                spans.push_back({clip.startBeat, clip.startBeat + clip.lengthBeats});
    return spans;
}

// Every other block eases from where it is drawn now to where the new plan puts it; a block the old and new plans
// agree on keeps its glide.
void TimelineSectionStrip::setDropPlan(sectionstrip::DropPlan plan) {
    if (doc_ == nullptr)
        return;
    for (const auto& placement : doc_->getPlacements()) {
        if (!dragCopy_ && isDragMember(placement.id))
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

// A block whose section just grew (or shrank back on an undo) eases its right edge from the old length to the new one.
double TimelineSectionStrip::drawnLengthOf(synth::PlacementId id, double docLength) const {
    return (double)lengthGlide_->drawnAt(id.value, {(float)docLength, 0.0f}).x;
}

double TimelineSectionStrip::getDrawnLengthBeatsForTest(synth::PlacementId id) const {
    const auto* placement = doc_ != nullptr ? doc_->getPlacement(id) : nullptr;
    const auto* section = placement != nullptr ? doc_->getSection(placement->section) : nullptr;
    return section == nullptr ? 0.0 : drawnLengthOf(id, section->lengthBeats);
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

std::vector<synth::PlacementId> TimelineSectionStrip::getDragMembersForTest() const {
    std::vector<synth::PlacementId> ids;
    for (const auto& member : dragMembers_)
        ids.push_back(member.block.id);
    return ids;
}

juce::Rectangle<float> TimelineSectionStrip::getDragMemberGhostBoundsForTest(int index) const {
    return index >= 0 && index < (int)dragMembers_.size() ? memberGhostRect((std::size_t)index)
                                                          : juce::Rectangle<float>();
}

// Member `index`'s ghost: at its distance from the pressed one's while dragging (gaps kept), easing to its own settle
// place after.
juce::Rectangle<float> TimelineSectionStrip::memberGhostRect(std::size_t index) const {
    const auto& member = dragMembers_[index];
    const float dragX = dragGhostX_ + (float)(member.offsetBeats * viewState_.pixelsPerBeat);
    const float x = blockDragging_ ? dragX : member.settleX + (dragX - member.settleX) * motion_.lift();
    return member.block.bounds.withX(x);
}

void TimelineSectionStrip::releasePressedBlock(const juce::MouseEvent& e) {
    if (!blockDragging_)
        return;
    const bool commit = !dragCancelled_ && e.position.y >= 0.0f && doc_ != nullptr;
    if (commit)
        updateBlockDragAt(e.position, e.mods.isShiftDown()); // the release's own place and Shift are the last word
    const auto mode = dragMakeRoom_ ? synth::SectionDropMode::MakeRoom : synth::SectionDropMode::Cover;
    blockDragging_ = false;
    stopEdgeScroll(); // after the last re-plan, which re-arms it while the drag still holds
    endDragCursor(*this, hovered_.isValid() ? dragGrabCursor() : juce::MouseCursor());
    motion_.liftTo(false);
    if (!commit || (!dragCopy_ && !dropChangesAnything())) {
        dragSettleX_ = (float)xAtBeat(dragBlock_.startBeat); // the view may have scrolled since the press
        for (auto& member : dragMembers_)
            member.settleX = (float)xAtBeat(member.block.startBeat);
        clearDropPlan();
        repaint();
        return;
    }
    commitBlockDrop(mode);
}

// Whether the drop the plan shows moves anything: a group landing at its own left edge keeps its gaps and moves
// nothing, unless a cover drop takes a block or a make-room drop shifts one.
bool TimelineSectionStrip::dropChangesAnything() const {
    for (const auto& [id, start] : dragPlan_.group)
        if (const auto* placement = doc_->getPlacement(synth::PlacementId{id});
            placement != nullptr && std::abs(placement->startBeat - start) > sectionstrip::kBarEpsilon)
            return true;
    return dropMovesOthers();
}

// Whether the plan covers a block or moves one that is not dragged (the hint says "insert" then, "place" otherwise).
// The sections between the dragged ones go along with them, so they do not count.
bool TimelineSectionStrip::dropMovesOthers() const {
    if (doc_ == nullptr)
        return false;
    for (const auto& other : dragPlan_.others) {
        if (dragPlan_.isCarried(other.id))
            continue;
        const auto* placement = doc_->getPlacement(synth::PlacementId{other.id});
        if (other.covered ||
            (placement != nullptr && std::abs(placement->startBeat - other.start) > sectionstrip::kBarEpsilon))
            return true;
    }
    return false;
}

// Writes the drop the plan shows as one undo step and leaves the dropped blocks (or their copies) selected, each ghost
// settling where its block now starts.
void TimelineSectionStrip::commitBlockDrop(synth::SectionDropMode mode) {
    // A cover drop takes the blocks under the landing away; each shrinks out where it was (trackChangesForMotion).
    const double target = dragPlan_.landing;
    std::vector<synth::PlacementId> ids;
    for (const auto& member : dragMembers_)
        ids.push_back(member.block.id);
    for (std::size_t i = 0; i < dragMembers_.size() && i < dragPlan_.group.size(); ++i)
        dragMembers_[i].settleX = (float)xAtBeat(dragPlan_.group[i].second);
    dragSettleX_ = (float)xAtBeat(target + dragLeadBeats_);
    // The blocks this drop covers are dimmed now; each exit starts from that look, not from full brightness.
    for (auto& [id, ghost] : lastGhosts_)
        if (dragPlan_.isCovered(id))
            ghost.alpha = sectionstrip::kMovedOriginalAlpha;
    const double spb = secondsPerBeat();
    std::vector<synth::PlacementId> placed;
    if (dragCopy_)
        performEdit(
            [this, &ids, target, mode, spb, &placed] { placed = doc_->placeSectionsTogether(ids, target, mode, spb); });
    else
        performEdit([this, &ids, target, mode, spb] { doc_->movePlacementsTogether(ids, target, mode, spb); });
    // The doc now has every block where the plan drew it, so the glides in flight carry on toward the same places;
    // refreshing while the plan is still held tells trackChangesForMotion not to start glides of its own.
    refresh();
    hasDragPlan_ = false;
    dragPlan_ = {};
    // The edit can rebuild the panel (which clears the selection); the blocks just dropped stay the selected ones.
    settlingIds_ = dragCopy_ && !placed.empty() ? placed : ids;
    synth::PlacementId anchor = pressedBlock_;
    for (std::size_t i = 0; dragCopy_ && i < placed.size() && i < ids.size(); ++i)
        if (ids[i] == pressedBlock_)
            anchor = placed[i];
    selectPlacements(settlingIds_, anchor);
}

// Esc mid-drag: the ghosts settle back where the blocks are and the blocks they moved aside ease back; nothing is
// written. Later drag events of the same press are ignored.
void TimelineSectionStrip::cancelBlockDrag() {
    dragCancelled_ = true;
    stopEdgeScroll();
    dragSettleX_ = (float)xAtBeat(dragBlock_.startBeat);
    for (auto& member : dragMembers_)
        member.settleX = (float)xAtBeat(member.block.startBeat);
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
