// Concern: TimelineAutomationLanes' room for a dragged modulator row outside its own lane. Over another lane that can
// take it (any track), that lane opens a gap of the row's height at the slot the drop would take and the gap the row
// left in its own lane closes; the lane blocks, curve bodies, bands and tracks below move with both, because every gap
// is part of modulatorOffset and so of laneBlockHeight and extraHeights. Moving to another slot closes the old gap
// while the new one opens; Esc or a refused drop closes them all and the row glides back. The gaps are FoldTweens on
// the block driver (TimelineAutomationLanesBlockMotion.cpp: 160 ms easeOutCubic, retargeted from where each one is),
// and land at once off screen, under Reduce Motion and with Animations off. A drop hands the open gap to the row
// that arrives in it, and the keyboard's move into another lane travels the same way. docs/layout/animation.md
// "Modulator row drag".
#include "UI/Timeline/AutomationLanes/TimelineAutomationLanes/TimelineAutomationLanes.h"

#include "UI/Layout/ReducedMotion.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
int scaledGap(int height, float presence) { return (int)std::lround((double)height * (double)presence); }
} // namespace

// What the drag adds before the lane's `slot`th row, px: an opened gap in a lane it hovers, and minus the closed share
// of the dragged row's own slot in its lane (it stands before the row after it). Zero while no drag is on.
int TimelineAutomationLanes::dragGapAt(synth::LaneId lane, int slot) const {
    int px = 0;
    if (!dragGaps_.empty())
        if (const auto it = dragGaps_.find({lane, slot}); it != dragGaps_.end())
            px += scaledGap(dragModHeight_, it->second.now);
    if (dragClosure_.now > 0.0f && lane == dragModLane_ && slot == dragModIndex_ + 1)
        px -= scaledGap(dragModHeight_, dragClosure_.now);
    return px;
}

// All the drag adds to `lane`'s block, px (what dragGapAt sums over its slots).
int TimelineAutomationLanes::dragGapsIn(synth::LaneId lane) const {
    int px = 0;
    for (int slot = 0; slot <= modulatorCount(lane); ++slot)
        px += dragGapAt(lane, slot);
    return px;
}

// The pointer that holds the animator's gap on the row's own slot (the slot it was picked up from): while the row is
// away the source lane's rows stand still, and only that one gap closes.
float TimelineAutomationLanes::pointerAtDragOrigin() const {
    const int key = modDrag_.animator().getDraggedKey();
    return dragModRestOf(key) + dragModGrab_;
}

// Opens the gap at the drop's slot (and closes the source's) when the drop is a lane other than the row's own that can
// take it, closes every gap otherwise. A refused lane opens nothing: the drop there would change nothing.
void TimelineAutomationLanes::updateDragGaps() {
    const bool away =
        modDrag_.animator().isDragging() && modDrop_.valid && modDrop_.lane.isValid() && modDrop_.lane != dragModLane_;
    const GapKey target{modDrop_.lane, modDrop_.index};
    bool changed = false;
    for (auto& [key, tween] : dragGaps_) {
        const float to = away && key == target ? 1.0f : 0.0f;
        if (tween.to != to) {
            tween.to = to;
            changed = true;
        }
    }
    if (away && dragGaps_.count(target) == 0) {
        dragGaps_[target] = {0.0f, 1.0f, 0.0f};
        changed = true;
    }
    const float closed = away ? 1.0f : 0.0f;
    if (dragClosure_.to != closed) {
        dragClosure_.to = closed;
        changed = true;
    }
    if (!changed)
        return;
    if (canAnimateBlocks()) {
        startBlockMotion();
        return;
    }
    landDragGaps();
}

// Every gap at its target; the closed ones go.
void TimelineAutomationLanes::landDragGaps() {
    for (auto it = dragGaps_.begin(); it != dragGaps_.end();) {
        it->second.now = it->second.from = it->second.to;
        it = it->second.to <= 0.0f ? dragGaps_.erase(it) : std::next(it);
    }
    dragClosure_.now = dragClosure_.from = dragClosure_.to;
}

// The drag stopped without moving the row to another lane (Esc, a refused drop, a move the host turned down): the gaps
// close, and a row that was away glides back from where it hung into its own slot, as the in-lane rows do.
void TimelineAutomationLanes::closeDragGaps() {
    if (std::exchange(dragFree_, false))
        if (auto* row = draggedModulatorRow(); row != nullptr && canAnimateBlocks()) {
            const float offset = (float)(row->getY() - draggedRowRestY_);
            if (offset != 0.0f) {
                glideTweens_[row->getInfo().key()] = {offset, 0.0f, offset};
                runModulatorGlide();
            }
        }
    updateDragGaps();
}

void TimelineAutomationLanes::clearDragGaps() {
    dragGaps_.clear();
    dragClosure_ = {0.0f, 0.0f, 0.0f};
    dragFree_ = false;
    dragModRest_.clear();
    dragModIndex_ = -1;
    dragModHeight_ = 0;
}

// After the host moved the row to `landing.lane`: the row that arrived there (already sized from the gap it took, see
// rebuildModulators) glides in from where it stood, drawn whole until it lands.
void TimelineAutomationLanes::settleMovedRow(const Landing& landing) {
    if (landing.newKey.isEmpty() || !canAnimateBlocks())
        return;
    const auto it = modulators_.find(landing.lane);
    if (it == modulators_.end())
        return;
    for (size_t i = 0; i < it->second.infos.size() && i < it->second.rows.size(); ++i) {
        if (it->second.infos[i].key() != landing.newKey)
            continue;
        const float offset = (float)(landing.fromY - it->second.rows[i]->getY());
        if (offset == 0.0f)
            return;
        travellingRows_.insert(landing.newKey);
        glideTweens_[landing.newKey] = {offset, 0.0f, offset};
        runModulatorGlide();
        return;
    }
}

} // namespace synth::ui
