//==============================================================================
// TimelinePanelTrackHeight.cpp
//
// One track's row height: the drag on a header's bottom edge, and the taller/shorter/default steps
// from its keys and menu. The height is Track::heightScale (saved with the project) on top of the
// shared vertical zoom; TimelineClipLaneArea::getRowLayout() turns it into that track's row, so the
// headers, clips, lanes and scroll all follow. TimelinePanelComponent is declared in
// TimelinePanelComponent.h; sibling TimelinePanel*.cpp files in this directory hold the rest of the class.

#include "TimelinePanelComponent.h"

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
