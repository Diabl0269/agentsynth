// Concern: TimelineAutomationLanes' modulator row drag -- grabbing a row in the header column and either reordering it
// among its lane's rows or dropping it on another lane to move the modulation there. The gesture is the shared
// ReorderDragSession over the source lane's rows; the lifted row follows the pointer, a line marks the slot it would
// take, and the drop is ONE undo step (the order write, or the host's move).
#include "UI/Timeline/AutomationLanes/TimelineAutomationLanes/TimelineAutomationLanes.h"

#include "UI/Layout/ReducedMotion.h"
#include "UI/Theme/Theme.h"
#include "UI/Timeline/TimelineTrackHeaderComponent/TimelineTrackHeaderComponent.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
constexpr int kIndicatorHeight = 2;
constexpr double kIndicatorGlideMs = 100.0;
constexpr float kRefusedAlpha = 0.5f; // the lifted row over a lane that can't take it
} // namespace

TimelineAutomationLanes::DropIndicator::DropIndicator() {
    setComponentID("modulatorDropIndicator");
    setInterceptsMouseClicks(false, false);
}

void TimelineAutomationLanes::DropIndicator::paint(juce::Graphics& g) {
    g.fillAll(synth::theme::themeOf(*this).colors.accent);
}

// The press, drag and release of a row reach the pool by the lane and the row itself, so a row that keeps its identity
// through an order change (permuteModulators) keeps its gesture too.
void TimelineAutomationLanes::wireModulatorDrag(ModulatorRow& row, synth::LaneId lane) {
    row.onDragPress = [this, lane, &row](int screenY) { beginModulatorDrag(lane, row.getInfo(), screenY); };
    row.onDragMove = [this](int screenY) { return dragModulator(screenY); };
    row.onDragRelease = [this] { endModulatorDrag(); };
}

// Where `lane`'s block starts, header-list coordinates (the static layout, never a header's current bounds).
float TimelineAutomationLanes::laneBlockStartOf(synth::LaneId lane) const {
    const auto* track = doc_ != nullptr ? doc_->getTrackForLane(lane) : nullptr;
    const auto origin = track != nullptr ? rowOrigin_.find(track->id) : rowOrigin_.end();
    if (origin == rowOrigin_.end())
        return 0.0f;
    float y = static_cast<float>(origin->second);
    for (const auto& candidate : track->lanes) {
        if (candidate.id == lane)
            return y;
        y += static_cast<float>(laneBlockHeight(candidate));
    }
    return y;
}

// Where the lane's `index`th modulator row starts (`index` == count: the slot after the last row).
float TimelineAutomationLanes::modulatorSlotStart(synth::LaneId lane, int index) const {
    return laneBlockStartOf(lane) + static_cast<float>(laneRowHeight(lane) + modulatorOffset(lane, index));
}

std::vector<ReorderDragAnimator::Slot> TimelineAutomationLanes::modulatorSlotsFor(synth::LaneId lane) const {
    std::vector<ReorderDragAnimator::Slot> slots;
    for (int i = 0; i < modulatorCount(lane); ++i)
        slots.push_back({modulatorSlotStart(lane, i), static_cast<float>(modulatorRowHeightFor(lane, i))});
    return slots;
}

ModulatorRow* TimelineAutomationLanes::draggedModulatorRow() const {
    const auto it = modulators_.find(dragModLane_);
    if (it == modulators_.end())
        return nullptr;
    for (size_t i = 0; i < it->second.infos.size() && i < it->second.rows.size(); ++i)
        if (it->second.infos[i].key() == dragModInfo_.key())
            return it->second.rows[i].get();
    return nullptr;
}

// Slots are the rows' STATIC positions, so a press during an earlier drop's settle still starts from the true layout.
void TimelineAutomationLanes::beginModulatorDrag(synth::LaneId lane, const ModulatorInfo& info, int screenY) {
    discardModulatorDrag();
    const auto it = modulators_.find(lane);
    if (doc_ == nullptr || it == modulators_.end() || laneDrag_.isReordering())
        return;
    for (const auto& row : it->second.infos)
        dragModKeys_.push_back(row.key());
    const auto at = std::find(dragModKeys_.begin(), dragModKeys_.end(), info.key());
    if (at == dragModKeys_.end()) {
        dragModKeys_.clear();
        return;
    }
    const int key = static_cast<int>(at - dragModKeys_.begin());
    const auto slots = modulatorSlotsFor(lane);
    dragModLane_ = lane;
    dragModInfo_ = info;
    const float pointer = laneDragPointerY(screenY);
    dragModGrab_ = pointer - slots[static_cast<size_t>(key)].start;
    dragModPointer_ = pointer;
    modDrop_ = {};
    modDrag_.begin(slots, key, dragModGrab_, pointer);
}

// True once the pointer is really dragging the row (past the threshold, not cancelled by Esc).
bool TimelineAutomationLanes::dragModulator(int screenY) {
    if (dragModKeys_.empty())
        return false;
    dragModPointer_ = laneDragPointerY(screenY);
    if (!modDrag_.dragTo(dragModPointer_))
        return false;
    modDrop_ = modulatorDropAt(dragModPointer_);
    onModulatorDragFrame();
    return modDrag_.animator().isDragging();
}

// The drop under the pointer: the lane whose block (its row and its modulator rows) the pointer is over, any track. The
// row's own lane takes the animator's slot; another lane takes the slot between its rows the pointer is at, but only
// when the host says it can hold the modulation.
TimelineAutomationLanes::ModulatorDrop TimelineAutomationLanes::modulatorDropAt(float pointerY) const {
    ModulatorDrop drop;
    if (doc_ == nullptr)
        return drop;
    for (const auto& track : doc_->getTracks()) {
        const auto origin = rowOrigin_.find(track.id);
        if (origin == rowOrigin_.end() || !isVisibleLane(track))
            continue;
        float y = static_cast<float>(origin->second);
        for (const auto& lane : track.lanes) {
            const float height = static_cast<float>(laneBlockHeight(lane));
            if (height > 0.0f && pointerY >= y && pointerY < y + height)
                drop.lane = lane.id;
            y += height;
        }
    }
    if (!drop.lane.isValid())
        return drop;
    if (drop.lane == dragModLane_) {
        drop.valid = true;
        drop.index = modDrag_.animator().getInsertionIndex();
        return drop;
    }
    drop.valid = host_ != nullptr && host_->canMoveModulatorToLane(dragModInfo_, drop.lane);
    if (!drop.valid)
        return drop;
    drop.index = 0;
    for (int i = 0; i < modulatorCount(drop.lane); ++i) {
        const float height = static_cast<float>(modulatorRowHeightFor(drop.lane, i));
        if (modulatorSlotStart(drop.lane, i) + height * 0.5f < pointerY)
            drop.index = i + 1;
    }
    return drop;
}

// How the layout poses the lane's `index`th row (resting at content y `restY`) while a drag is on: the dragged row
// follows the pointer (clamped to its own lane while the drop is there, free over any other lane), the others take the
// animator's slots, and the lifted row is drawn raised.
TimelineAutomationLanes::ModulatorDragPose TimelineAutomationLanes::modulatorDragPose(synth::LaneId lane, int index,
                                                                                      int restY) const {
    if (!modDrag_.isReordering() || lane != dragModLane_)
        return {};
    const auto it = modulators_.find(lane);
    if (it == modulators_.end() || index < 0 || index >= (int)it->second.infos.size())
        return {};
    const auto at = std::find(dragModKeys_.begin(), dragModKeys_.end(), it->second.infos[(size_t)index].key());
    if (at == dragModKeys_.end())
        return {};
    const int key = static_cast<int>(at - dragModKeys_.begin());
    const auto& animator = modDrag_.animator();
    if (key != animator.getDraggedKey())
        return {static_cast<int>(std::lround(animator.getLayoutStart(key))) - restY, 0.0f};
    const bool free = animator.isDragging() && modDrop_.lane != dragModLane_;
    const float y = free ? dragModPointer_ - dragModGrab_ : animator.getDraggedStart();
    return {static_cast<int>(std::lround(y)) - restY, animator.getLift()};
}

// Runs per drag event, per frame of a tween and once after Esc: re-places the rows and bands, marks the slot, and
// dims the lifted row over a lane that can't take it.
void TimelineAutomationLanes::onModulatorDragFrame() {
    const bool dragging = modDrag_.animator().isDragging();
    if (auto* row = draggedModulatorRow())
        row->setAlpha(dragging && !modDrop_.valid ? kRefusedAlpha : 1.0f);
    updateDropIndicator();
    if (onLayoutChanged)
        onLayoutChanged();
    headerParent_.repaint();
}

// A line at the top of the slot the drop would take, fading in and out with the drag and gliding between slots.
void TimelineAutomationLanes::updateDropIndicator() {
    const bool show = modDrag_.animator().isDragging() && modDrop_.valid;
    if (!show) {
        dropFade_->setShown(false);
        return;
    }
    const float slotY = modDrop_.lane == dragModLane_
                            ? modDrag_.animator().getLayoutStart(modDrag_.animator().getDraggedKey())
                            : modulatorSlotStart(modDrop_.lane, modDrop_.index);
    const float y = slotY - static_cast<float>(kIndicatorHeight) * 0.5f;
    const int x = ModulatorRow::kIndent;
    const auto place = [this, x](float atY) {
        dropIndicator_->setBounds(x, static_cast<int>(std::lround(atY)), std::max(0, rowWidth_ - x), kIndicatorHeight);
    };
    const bool glide = dropFade_->isShown() && FadeVisibility::canAnimateIn(&headerParent_) && !prefersReducedMotion();
    if (y != dropToY_ || !dropFade_->isShown()) {
        dropFromY_ = glide ? static_cast<float>(dropIndicator_->getY()) : y;
        dropToY_ = y;
        if (glide && dropFromY_ != y)
            dropDriver_.start(
                foldUpdater_, kIndicatorGlideMs, easeOutCubic,
                [this, place](float t) { place(dropFromY_ + (dropToY_ - dropFromY_) * t); }, [] {});
        else
            place(y);
    }
    dropFade_->setShown(true);
    dropIndicator_->toFront(false);
}

// A plain click starts nothing; a drag ends with the drop (or, after Esc, with everything back where it was).
void TimelineAutomationLanes::endModulatorDrag() {
    if (dragModKeys_.empty())
        return;
    switch (modDrag_.end()) {
    case ReorderDragSession::End::Click:
        discardModulatorDrag(/*relayout=*/false); // nothing was lifted, so nothing moved
        break;
    case ReorderDragSession::End::Commit:
        commitModulatorDrag();
        break;
    case ReorderDragSession::End::Cancelled:
    case ReorderDragSession::End::Nothing:
        break;
    }
}

// The drop: on the row's own lane ONE order write (the doc notification keeps the same rows and the settle glides the
// lifted one in from where it was dropped); on another lane the host's move, which rebuilds the rows (the moved routing
// has a new key) so the row fades in at its slot under the lane. A refused or unchanged drop glides everything back.
void TimelineAutomationLanes::commitModulatorDrag() {
    const auto drop = modDrop_;
    const auto info = dragModInfo_;
    const auto lane = dragModLane_;
    const auto& animator = modDrag_.animator();
    const int dragged = animator.getDraggedKey();
    const float droppedAt = animator.getDraggedStart();
    const int insertion = animator.getInsertionIndex();
    dropFade_->setShown(false);
    if (!drop.valid) {
        modDrag_.abort();
        return;
    }
    if (drop.lane != lane) {
        droppedRoutingKey_ = info.key();
        const bool moved = host_ != nullptr && host_->moveModulatorToLane(info, drop.lane, drop.index);
        droppedRoutingKey_.clear();
        if (moved)
            discardModulatorDrag(); // the host's refresh rebuilt the rows; nothing of the gesture is left to settle
        else
            modDrag_.abort();
        return;
    }
    if (insertion == dragged) {
        std::vector<float> starts;
        for (const auto& slot : modulatorSlotsFor(lane))
            starts.push_back(slot.start);
        modDrag_.release(starts);
        onModulatorDragFrame();
        return;
    }
    if (!moveModulatorInOrder(lane, modulatorOrderKey(info), insertion)) {
        modDrag_.abort();
        return;
    }
    const auto it = modulators_.find(lane);
    if (it == modulators_.end()) {
        discardModulatorDrag();
        return;
    }
    dragModKeys_.clear();
    for (const auto& row : it->second.infos)
        dragModKeys_.push_back(row.key());
    const auto at = std::find(dragModKeys_.begin(), dragModKeys_.end(), info.key());
    if (at == dragModKeys_.end()) {
        discardModulatorDrag();
        return;
    }
    modDrag_.settleInto(modulatorSlotsFor(lane), static_cast<int>(at - dragModKeys_.begin()), droppedAt);
    onModulatorDragFrame();
}

void TimelineAutomationLanes::discardModulatorDrag(bool relayout) {
    const bool had = !dragModKeys_.empty() || modDrag_.isReordering();
    modDrag_.discard();
    if (auto* row = draggedModulatorRow()) {
        row->setAlpha(1.0f);
        row->setLift(0.0f);
    }
    dragModKeys_.clear();
    modDrop_ = {};
    dragModLane_ = {};
    dragModInfo_ = {};
    if (dropFade_ != nullptr)
        dropFade_->setShown(false);
    if (relayout && had && onLayoutChanged && doc_ != nullptr)
        onLayoutChanged();
}

} // namespace synth::ui
