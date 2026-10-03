// Concern: TimelineAutomationLanes' lane reorder -- dragging a lane header up or down within its track, and Move Lane
// Up/Down from the keyboard. The gesture is the shared ReorderDragSession; only the header column animates (a
// block is a lane row plus its modulator rows), the curve editors follow the new order when the drop commits, like the
// clip lanes follow a track reorder. A lane only ever moves inside its own track.
#include "UI/Timeline/AutomationLanes/TimelineAutomationLanes/TimelineAutomationLanes.h"

#include "ShortcutManager/ShortcutManager.h"
#include "UI/Timeline/AutomationLanes/AutomationLaneActions.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
int indexOfLane(const std::vector<synth::LaneId>& ids, synth::LaneId id) {
    const auto it = std::find(ids.begin(), ids.end(), id);
    return it == ids.end() ? -1 : static_cast<int>(it - ids.begin());
}

juce::KeyPress defaultMoveKey(int keyCode) {
    return juce::KeyPress(keyCode,
                          juce::ModifierKeys(juce::ModifierKeys::commandModifier | juce::ModifierKeys::altModifier), 0);
}
} // namespace

// Wires the header's gesture and keys to the pool. The header outlives neither the pool nor the doc, and every callback
// resolves the lane by id when it runs.
void TimelineAutomationLanes::wireHeader(AutomationLaneHeaderComponent& header, synth::LaneId id) {
    header.onDragPress = [this, id](int screenY) { beginLaneDrag(id, screenY); };
    header.onDragMove = [this](int screenY) { dragLane(screenY); };
    header.onDragRelease = [this] { return endLaneDrag(); };
    header.onLaneKey = [this, id](const juce::KeyPress& key) { return handleLaneKey(id, key); };
}

// The lanes of `track` that have a row (a modulator's amount lane has none), in track order.
std::vector<synth::LaneId> TimelineAutomationLanes::shownLanesOf(const synth::Track& track) const {
    std::vector<synth::LaneId> ids;
    for (const auto& lane : track.lanes)
        if (!isAmountLane(lane.id) && headers_.count(lane.id) > 0)
            ids.push_back(lane.id);
    return ids;
}

// The static start of each of `ids`' blocks in `track`'s CURRENT lane order, header-list coordinates.
std::vector<float> TimelineAutomationLanes::laneStartsFor(const synth::Track& track,
                                                          const std::vector<synth::LaneId>& ids) const {
    std::map<synth::LaneId, float> startOf;
    const auto origin = rowOrigin_.find(track.id);
    float y = origin != rowOrigin_.end() ? static_cast<float>(origin->second) : 0.0f;
    for (const auto& lane : track.lanes) {
        startOf[lane.id] = y;
        y += static_cast<float>(laneBlockHeight(lane));
    }
    std::vector<float> starts;
    for (const auto id : ids)
        starts.push_back(startOf[id]);
    return starts;
}

float TimelineAutomationLanes::laneDragPointerY(int screenY) const {
    return static_cast<float>(headerParent_.getLocalPoint(nullptr, juce::Point<int>(0, screenY)).y);
}

// Slots are the blocks' STATIC positions, never their current bounds, so a press during an earlier drop's settle still
// starts from the true layout. The grab offset is taken once, in list coordinates
// (docs/layout/animation.md#reorder-drag).
void TimelineAutomationLanes::beginLaneDrag(synth::LaneId lane, int screenY) {
    laneDrag_.discard();
    dragLaneIds_.clear();
    liftedLane_ = {};
    const auto* track = doc_ != nullptr ? doc_->getTrackForLane(lane) : nullptr;
    if (track == nullptr)
        return;
    const auto ids = shownLanesOf(*track);
    const int key = indexOfLane(ids, lane);
    if (key < 0)
        return;
    std::vector<ReorderDragAnimator::Slot> slots;
    const auto starts = laneStartsFor(*track, ids);
    for (size_t i = 0; i < ids.size(); ++i)
        slots.push_back({starts[i], static_cast<float>(laneBlockHeight(*doc_->getLane(ids[i])))});
    dragLaneIds_ = ids;
    dragTrack_ = track->id;
    const float pointer = laneDragPointerY(screenY);
    laneDrag_.begin(slots, key, pointer - slots[static_cast<size_t>(key)].start, pointer);
}

void TimelineAutomationLanes::dragLane(int screenY) {
    if (dragLaneIds_.empty() || !laneDrag_.dragTo(laneDragPointerY(screenY)))
        return;
    liftedLane_ = dragLaneIds_[static_cast<size_t>(laneDrag_.animator().getDraggedKey())];
    onLaneDragFrame();
}

// True when the press never became a drag, i.e. it was a plain click.
bool TimelineAutomationLanes::endLaneDrag() {
    if (dragLaneIds_.empty())
        return true;
    switch (laneDrag_.end()) {
    case ReorderDragSession::End::Click:
        return true;
    case ReorderDragSession::End::Commit:
        commitLaneDrag();
        return false;
    case ReorderDragSession::End::Cancelled:
    case ReorderDragSession::End::Nothing:
        break;
    }
    return false;
}

// The drop is ONE moveLaneOrderUndoable to the doc index of the visible lane whose slot the block landed in (hidden
// amount lanes share the list, so the animator's slot index is not a doc index). The doc notification re-places the
// headers synchronously, and the pooled headers survive it, so release() right after glides each block from where it
// was dropped to its final slot.
void TimelineAutomationLanes::commitLaneDrag() {
    const auto& animator = laneDrag_.animator();
    const int dragged = animator.getDraggedKey();
    const int insertion = animator.getInsertionIndex();
    const auto lane = dragLaneIds_[static_cast<size_t>(dragged)];
    const auto* track = doc_ != nullptr ? doc_->getTrack(dragTrack_) : nullptr;
    if (track != nullptr && insertion != dragged) {
        const auto target = dragLaneIds_[static_cast<size_t>(insertion)];
        int docIndex = -1;
        for (int i = 0; i < (int)track->lanes.size(); ++i)
            if (track->lanes[(size_t)i].id == target)
                docIndex = i;
        if (docIndex >= 0)
            moveLaneOrderUndoable(*doc_, undo_, lane, docIndex);
    }
    track = doc_ != nullptr ? doc_->getTrack(dragTrack_) : nullptr;
    if (track == nullptr) {
        laneDrag_.discard();
        return;
    }
    laneDrag_.release(laneStartsFor(*track, dragLaneIds_));
    onLaneDragFrame();
}

void TimelineAutomationLanes::discardLaneDrag() {
    laneDrag_.discard();
    dragLaneIds_.clear();
    liftedLane_ = {};
}

// Re-places the dragged track's headers from the animator; runs per drag event, per frame of a tween and once after
// Esc.
void TimelineAutomationLanes::onLaneDragFrame() {
    if (!laneDrag_.isReordering())
        liftedLane_ = {};
    const auto origin = rowOrigin_.find(dragTrack_);
    if (origin != rowOrigin_.end())
        placeHeadersFor(dragTrack_, origin->second, rowWidth_);
    headerParent_.repaint();
}

// Cmd+Alt+Up / Down (rebindable: timelineMoveLaneUp / timelineMoveLaneDown), offered by the focused header's controls
// and the focused editor.
bool TimelineAutomationLanes::handleLaneKey(synth::LaneId lane, const juce::KeyPress& key) {
    const auto matches = [this, &key](const char* action, int keyCode) {
        if (shortcuts_ == nullptr)
            return key == defaultMoveKey(keyCode);
        return ShortcutManager::keyPressMatches(shortcuts_->getBinding(action), key);
    };
    const bool up = matches("timelineMoveLaneUp", juce::KeyPress::upKey);
    const bool down = !up && matches("timelineMoveLaneDown", juce::KeyPress::downKey);
    if (!up && !down)
        return false;
    moveLaneBy(lane, up ? -1 : 1);
    return true; // consumed even at an end, so the combo or the point nudge never sees a Cmd+Alt+arrow
}

bool TimelineAutomationLanes::moveLaneBy(synth::LaneId lane, int delta) {
    const auto* track = doc_ != nullptr ? doc_->getTrackForLane(lane) : nullptr;
    auto* header = headerFor(lane);
    if (track == nullptr || header == nullptr || laneDrag_.isReordering())
        return false;
    const auto ids = shownLanesOf(*track);
    const int from = indexOfLane(ids, lane);
    const int to = from + delta;
    if (from < 0 || to < 0 || to >= (int)ids.size())
        return false;
    int docIndex = -1;
    for (int i = 0; i < (int)track->lanes.size(); ++i)
        if (track->lanes[(size_t)i].id == ids[(size_t)to])
            docIndex = i;
    const auto trackId = track->id;
    const float fromY = static_cast<float>(header->getY());
    if (docIndex < 0 || !moveLaneOrderUndoable(*doc_, undo_, lane, docIndex))
        return false;

    // The doc notification re-placed everything at its final slot; the moved block glides in from where it stood.
    const auto* moved = doc_->getTrack(trackId);
    if (moved == nullptr)
        return true;
    const auto order = shownLanesOf(*moved);
    const int key = indexOfLane(order, lane);
    if (key < 0)
        return true;
    std::vector<ReorderDragAnimator::Slot> slots;
    const auto starts = laneStartsFor(*moved, order);
    for (size_t i = 0; i < order.size(); ++i)
        slots.push_back({starts[i], static_cast<float>(laneBlockHeight(*doc_->getLane(order[i])))});
    dragLaneIds_ = order;
    dragTrack_ = trackId;
    liftedLane_ = lane;
    laneDrag_.settleInto(slots, key, fromY);
    onLaneDragFrame();
    return true;
}

} // namespace synth::ui
