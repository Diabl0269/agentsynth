//==============================================================================
// TimelinePanelTrackHeight.cpp
//
// One track's row height: the drag on a header's bottom edge, and the taller/shorter/default steps
// from its keys and menu. The height is Track::heightScale (saved with the project) on top of the
// shared vertical zoom; TimelineClipLaneArea::getRowLayout() turns it into that track's row, so the
// headers, clips, lanes and scroll all follow. TimelinePanelComponent is declared in
// TimelinePanelComponent.h; sibling TimelinePanel*.cpp files in this directory hold the rest of the class.

#include "TimelinePanelComponent.h"

#include <set>

namespace synth::ui {

int TimelinePanelComponent::trackIndexOf(synth::TrackId track) const {
    if (doc_ == nullptr)
        return -1;
    const auto& tracks = doc_->getTracks();
    for (int i = 0; i < (int)tracks.size(); ++i)
        if (tracks[(size_t)i].id == track)
            return i;
    return -1;
}

// Rows above the pointer keep their place; the scroll is only clamped into the new range.
void TimelinePanelComponent::relayoutTrackRows() {
    layoutTrackHeaders();
    viewState_.scrollTracksPx(0.0, maxTrackScrollPx());
    syncTrackScroll();
}

// The drag previews in the clip lanes' layout and writes the doc once, on release: a doc write per
// mouse move would republish the audio snapshot and dirty the project on every pixel.
// A track's resize strip sits on the bottom of its own row, but the row's open lane and modulator rows hang
// beneath it, so the seam above the next track is the lane block's bottom, not the row's. A second strip
// along that edge does the same thing (same name, tooltip, drag and double-click), so the edge a person
// sees above the next track resizes the track it belongs to whether or not lanes are open. It is placed
// after the lane headers and brought to the front so no lane row takes its hits.
void TimelinePanelComponent::placeLaneSeamHandles(const TimelineRowLayout& layout) {
    constexpr int kSeamThickness = 5;
    const int width = std::max(0, trackHeaderViewport_.getMaximumVisibleWidth());
    std::set<synth::TrackId> live;
    for (int i = 0; i < trackHeaderList_.headers.size(); ++i) {
        auto* header = trackHeaderList_.headers.getUnchecked(i);
        const auto id = header->getTrackId();
        auto& ownHandle = header->getHeightHandle();
        const int extra = layout.trackExtraHeight(i);
        if (extra <= 0 || !ownHandle.isVisible())
            continue;
        live.insert(id);
        auto& slot = laneSeamHandles_[id];
        if (slot == nullptr) {
            slot = std::make_unique<EdgeResizeHandle>(EdgeResizeHandle::Axis::Vertical);
            slot->setComponentID("trackHeightHandleBelowLanes");
            slot->onDragStarted = [this, id] { beginTrackHeightDrag(id); };
            slot->onDragged = [this, id](int delta) { dragTrackHeight(id, delta); };
            slot->onDragEnded = [this, id] { endTrackHeightDrag(id); };
            slot->onResetRequested = [this, id] { stepTrackHeight(id, 0); };
            trackHeaderList_.addAndMakeVisible(*slot);
        }
        slot->setTitle(ownHandle.getTitle());
        slot->setTooltip(ownHandle.getTooltip());
        slot->setBounds(0, header->getBottom() + extra - kSeamThickness, width, kSeamThickness);
        slot->toFront(false);
    }
    for (auto it = laneSeamHandles_.begin(); it != laneSeamHandles_.end();) {
        if (live.count(it->first) == 0)
            it = laneSeamHandles_.erase(it);
        else
            ++it;
    }
}

void TimelinePanelComponent::beginTrackHeightDrag(synth::TrackId track) {
    const int index = trackIndexOf(track);
    if (index < 0)
        return;
    heightDragTrack_ = track;
    heightDragStartPx_ = rowLayout().trackRowHeight(index);
    heightDragScale_ = doc_->getTracks()[(size_t)index].heightScale;
}

void TimelinePanelComponent::dragTrackHeight(synth::TrackId track, int deltaPx) {
    if (!(track == heightDragTrack_))
        return;
    const int base = clipLaneArea_.getRowHeight();
    if (base <= 0)
        return;
    heightDragScale_ = std::clamp((double)(heightDragStartPx_ + deltaPx) / (double)base, synth::Track::kMinHeightScale,
                                  synth::Track::kMaxHeightScale);
    clipLaneArea_.setTrackHeightPreview(track, heightDragScale_);
    relayoutTrackRows();
}

void TimelinePanelComponent::endTrackHeightDrag(synth::TrackId track) {
    if (!(track == heightDragTrack_))
        return;
    heightDragTrack_ = {};
    clipLaneArea_.clearTrackHeightPreview();
    const double scale = heightDragScale_;
    auto mutate = [this, track, scale] { doc_->setTrackHeightScale(track, scale); };
    if (trackHeaderHost_ != nullptr)
        trackHeaderHost_->performTrackEdit(mutate); // one undo step
    else if (doc_ != nullptr)
        mutate();
    relayoutTrackRows(); // also when the doc did not change (a drag back to where it started)
}

void TimelinePanelComponent::stepTrackHeight(synth::TrackId track, int direction) {
    const int index = trackIndexOf(track);
    if (index < 0 || doc_->getTracks()[(size_t)index].kind == synth::TrackKind::Automation)
        return;
    const double current = doc_->getTracks()[(size_t)index].heightScale;
    const double scale = direction > 0   ? current * kTrackHeightStepFactor
                         : direction < 0 ? current / kTrackHeightStepFactor
                                         : 1.0;
    auto mutate = [this, track, scale] { doc_->setTrackHeightScale(track, scale); };
    if (trackHeaderHost_ != nullptr)
        trackHeaderHost_->performTrackEdit(mutate);
    else
        mutate();
}

// Vertical zoom gives every track the same height: clears each track's own height as one undo step.
// False when every track already had the default, so a run of wheel ticks writes the doc once.
bool TimelinePanelComponent::clearTrackHeightOverrides() {
    if (doc_ == nullptr)
        return false;
    const auto& tracks = doc_->getTracks();
    if (std::none_of(tracks.begin(), tracks.end(), [](const synth::Track& t) { return t.heightScale != 1.0; }))
        return false;
    auto mutate = [this] { doc_->resetTrackHeightScales(); };
    if (trackHeaderHost_ != nullptr)
        trackHeaderHost_->performTrackEdit(mutate);
    else
        mutate();
    return true;
}

} // namespace synth::ui
