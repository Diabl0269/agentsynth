// Concern: MixerZonesPane's drag of a row between the three groups, on the shared vertical
// ReorderDragAnimator: the row stays under the pointer, its neighbours (group headings included)
// make room, and the drop only assigns the channel to the group it lands in.
#include "MixerZonesPane.h"

#include <algorithm>

namespace synth::ui {

namespace {
constexpr int kDragRepeatMs = 40;
}

float MixerZonesPane::pointerYInList(const juce::MouseEvent& e) {
    return e.getEventRelativeTo(&listContent_).position.y;
}

// The animator's keys are indices into the list as it stood at the press; dragIdentities_ maps them
// back to rows and headings across the rebuild a drop causes. The grab offset is captured once, in
// list coordinates, so it stays valid while the list scrolls.
void MixerZonesPane::beginRowDrag(const juce::String& id, const juce::MouseEvent& e) {
    frames_.stop();
    cancelKey_.disarm();
    dragCancelled_ = false;
    reorder_.cancel();
    draggedId_ = {};
    dragIdentities_.clear();
    std::vector<ReorderDragAnimator::Slot> slots;
    int pressedKey = -1;
    float y = 0.0f;
    for (const auto& item : items_) {
        if (!item.isHeader && item.channelId == id)
            pressedKey = static_cast<int>(slots.size());
        dragIdentities_.push_back(item.identity());
        slots.push_back({y, static_cast<float>(item.height())});
        y += static_cast<float>(item.height());
    }
    if (pressedKey < 0)
        return;
    const float pointer = pointerYInList(e);
    reorder_.begin(slots, pressedKey, pointer - slots[static_cast<size_t>(pressedKey)].start, pointer, isShowing());
    draggedId_ = id;
}

void MixerZonesPane::dragRow(const juce::MouseEvent& e) {
    if (!reorder_.isPressed() && !reorder_.isDragging())
        return;
    if (!reorder_.dragTo(pointerYInList(e)))
        return;
    if (!cancelKey_.isArmed()) {
        cancelKey_.arm(*this, [this] { cancelRowDrag(); });
        if (isShowing())
            juce::Component::beginDragAutoRepeat(kDragRepeatMs);
    }
    startDragFramesIfNeeded();
    placeItems();
}

// A frame pump runs only while a tween is in flight, so a held drag with the pointer at rest and a
// settled list cost no frames.
void MixerZonesPane::startDragFramesIfNeeded() {
    if (reorder_.getTweenGeneration() == generationSeen_)
        return;
    generationSeen_ = reorder_.getTweenGeneration();
    if (reorder_.needsFrames())
        frames_.run(ReorderDragAnimator::kMakeRoomMs + 20.0, [this] { onDragFrame(); });
}

void MixerZonesPane::onDragFrame() {
    reorder_.finishIfSettled();
    if (!reorder_.isReordering())
        draggedId_ = {};
    placeItems();
}

void MixerZonesPane::endRowDrag(const juce::MouseEvent&) {
    cancelKey_.disarm();
    if (dragCancelled_) {
        dragCancelled_ = false;
        return;
    }
    if (reorder_.isPressed()) {
        reorder_.cancel();
        return;
    }
    if (reorder_.isDragging())
        commitRowDrag();
}

// The group a row lands in is the nearest heading above it in the order a release now would give;
// above the first heading it belongs to the first group.
synth::MixerZone MixerZonesPane::zoneForDrop(const std::vector<int>& newOrder) const {
    const auto at = std::find(newOrder.begin(), newOrder.end(), reorder_.getDraggedKey());
    for (auto it = at; it != newOrder.begin();) {
        --it;
        const auto& identity = dragIdentities_[static_cast<size_t>(*it)];
        if (identity.startsWith("h:"))
            return synth::mixerZoneFromString(identity.substring(2));
    }
    return synth::MixerZone::Left;
}

// The zone edit goes out through onSetZone, which normally answers with a fresh setChannels() before
// it returns; the settle then aims at the rebuilt list's real slots. If nothing changed (a drop in the
// same group, or an owner that did not answer) the list is unchanged and the row glides back.
void MixerZonesPane::commitRowDrag() {
    const auto zone = zoneForDrop(reorder_.getNewOrder());
    committing_ = true;
    if (const auto* channel = findChannel(draggedId_); channel != nullptr && channel->zone != zone && onSetZone)
        onSetZone(draggedId_, zone);
    committing_ = false;

    std::vector<float> finalStarts;
    float y = 0.0f;
    std::map<juce::String, float> starts;
    for (const auto& item : items_) {
        starts[item.identity()] = y;
        y += static_cast<float>(item.height());
    }
    for (const auto& identity : dragIdentities_) {
        const auto it = starts.find(identity);
        if (it == starts.end()) {
            reorder_.cancel();
            break;
        }
        finalStarts.push_back(it->second);
    }
    if (finalStarts.size() == dragIdentities_.size())
        reorder_.release(finalStarts);
    if (!reorder_.isReordering())
        draggedId_ = {};
    startDragFramesIfNeeded();
    placeItems();
}

// Esc: nothing is committed; the animator glues everything back.
void MixerZonesPane::cancelRowDrag() {
    cancelKey_.disarm();
    if (!reorder_.isDragging())
        return;
    dragCancelled_ = true;
    reorder_.abort();
    startDragFramesIfNeeded();
    onDragFrame();
}

void MixerZonesPane::discardRowDrag() {
    cancelKey_.disarm();
    frames_.stop();
    reorder_.cancel();
    draggedId_ = {};
    dragCancelled_ = false;
    placeItems();
}

} // namespace synth::ui
