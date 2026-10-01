// TimelinePanelClipKeyboard.cpp
//
// The panel's half of clip keyboard mode: carrying keyboard focus from a track header into that
// track's clips and back, and keeping the track rows scrolled to the keyboard clip.
// TimelinePanelComponent is declared in TimelinePanelComponent.h; sibling TimelinePanel*.cpp files
// in this directory hold the rest of the class. The key handling itself lives in
// TimelineClipLaneArea (TimelineClipLaneKeyboard.cpp).

#include "TimelinePanelComponent.h"

#include "Transport/TransportService.h"
#include "UI/Timeline/ClipKeyboardNav.h"

namespace synth::ui {

// Double-click (and the Open Clip key) open the piano roll; the keyboard-clip callbacks keep the
// track rows and focused-track index following the clip the arrow keys are on.
void TimelinePanelComponent::wireClipLaneCallbacks() {
    clipLaneArea_.onClipDoubleClicked = [this](synth::ClipId id) { openPianoRoll(id); };
    clipLaneArea_.onKeyboardClipChanged = [this](synth::ClipId id) { followKeyboardClip(id); };
    clipLaneArea_.onReturnToTrackHeaderRequested = [this](synth::TrackId id) { returnToTrackHeader(id); };
}

// Keys for the panel root holding focus. Down enters the track-header column at its top, "+ Track"
// (the first stop, with or without tracks), and Down again reaches the first track; the Next Clip key enters the clips
// of the focused track, else of the first track that has any.
bool TimelinePanelComponent::handleRootFocusKey(const juce::KeyPress& key) {
    if (key.isKeyCode(juce::KeyPress::downKey)) {
        addTrackFromTop_ = true;
        focusAddTrackButton();
        return true;
    }
    if (doc_ == nullptr || !matchesAction(key, "timelineClipNext",
                                          juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys::noModifiers, 0)))
        return false;
    const auto& tracks = doc_->getTracks();
    if (juce::isPositiveAndBelow(focusedTrackIndex_, (int)tracks.size()))
        return enterTrackClips(tracks[(std::size_t)focusedTrackIndex_].id);
    for (const auto& track : tracks) {
        if (!track.clips.empty())
            return enterTrackClips(track.id);
    }
    return false;
}

// Right on a track header. False, doing nothing, for a track with no clips.
// The playhead is the anchor, so entering a track lands where the user is listening; with no
// transport it is beat 0 and the track's first clip is chosen.
bool TimelinePanelComponent::enterTrackClips(synth::TrackId trackId) {
    if (doc_ == nullptr || pianoRoll_.isOpen())
        return false;
    const auto* track = doc_->getTrack(trackId);
    if (track == nullptr)
        return false;

    const double playheadBeat = transport_ != nullptr ? transport_->getPositionSnapshot().ppq : 0.0;
    const auto clipId = clipnav::firstClipFrom(*track, playheadBeat);
    if (!clipId.isValid())
        return false;

    clipLaneArea_.setKeyboardClip(clipId);
    // A best-effort no-op without a native peer, like every grabKeyboardFocus() in this panel.
    clipLaneArea_.grabKeyboardFocus();
    return true;
}

// Keeps the focused-track index on the keyboard clip's row, so Escape and a later Up/Down from the
// header continue from where the clips left off.
void TimelinePanelComponent::followKeyboardClip(synth::ClipId id) {
    if (doc_ == nullptr)
        return;
    const auto* track = doc_->getTrackForClip(id);
    if (track == nullptr)
        return;
    for (int i = 0; i < trackHeaderList_.headers.size(); ++i) {
        if (trackHeaderList_.headers.getUnchecked(i)->getTrackId() != track->id)
            continue;
        if (focusedTrackIndex_ != i) {
            focusedTrackIndex_ = i;
            refreshRoutingPane();
        }
        ensureTrackVisible(i);
        return;
    }
}

void TimelinePanelComponent::returnToTrackHeader(synth::TrackId trackId) {
    for (int i = 0; i < trackHeaderList_.headers.size(); ++i) {
        auto* header = trackHeaderList_.headers.getUnchecked(i);
        if (header->getTrackId() != trackId)
            continue;
        focusedTrackIndex_ = i;
        header->grabKeyboardFocus();
        ensureTrackVisible(i);
        refreshRoutingPane();
        return;
    }
}

} // namespace synth::ui
