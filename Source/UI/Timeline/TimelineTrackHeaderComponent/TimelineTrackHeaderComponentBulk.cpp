// Concern: M and S on a track row when several tracks are selected -- the single-track toggle for the clicked row,
// then every other selected track made to take the state it ended in, as ONE undo step.
#include "TimelineTrackHeaderComponent.h"

namespace synth::ui {

namespace {
const juce::KeyPress kCopyTracksKey('c', juce::ModifierKeys::commandModifier, 0);
const juce::KeyPress kPasteTracksKey('v', juce::ModifierKeys::commandModifier, 0);
} // namespace

// Cmd+C / Cmd+V on a row whose track is one of several selected / with tracks copied: the track clipboard
// (docs/timeline/tracks.md#selecting-several-tracks). Anything else bubbles, so a single selected row's Cmd+C and
// Cmd+V mean what they always did.
bool TimelineTrackHeaderComponent::handleTrackClipboardKey(const juce::KeyPress& key) {
    if (isSectionHeader() || host_ == nullptr)
        return false;
    if (matchesAction(key, "timelineCopyFocusedTracks", kCopyTracksKey)) {
        const auto targets = bulkTargets();
        if (targets.size() > 1) {
            host_->copyTracks(targets);
            return true;
        }
    }
    if (host_->canPasteTracks() && matchesAction(key, "timelinePasteTracks", kPasteTracksKey)) {
        host_->pasteTracks(trackId_);
        return true;
    }
    return false;
}

std::vector<synth::TrackId> TimelineTrackHeaderComponent::bulkTargets() const {
    return host_ != nullptr ? host_->tracksActedOnBy(trackId_) : std::vector<synth::TrackId>{trackId_};
}

bool TimelineTrackHeaderComponent::isMutedNow(synth::TrackId id) const {
    if (auto* link = linkSurface())
        if (const auto linked = link->linkedChannelMuted(id))
            return *linked;
    const auto* t = doc_.getTrack(id);
    return t != nullptr && t->muted;
}

bool TimelineTrackHeaderComponent::isSoloedNow(synth::TrackId id) const {
    if (auto* link = linkSurface())
        if (const auto linked = link->linkedChannelSoloed(id))
            return *linked;
    const auto* t = doc_.getTrack(id);
    return t != nullptr && t->soloed;
}

// A linked track's M is its channel strip's mute (one stored mute, docs/mixer/mixer.md), anything else is the doc's.
void TimelineTrackHeaderComponent::setOtherTrackMuted(synth::TrackId id, bool muted) {
    if (isMutedNow(id) == muted)
        return;
    if (auto* link = linkSurface(); link != nullptr && link->linkedChannelMuted(id).has_value()) {
        link->toggleLinkedChannelMuted(id);
        return;
    }
    performEdit([this, id, muted] { doc_.setTrackMuted(id, muted); });
}

void TimelineTrackHeaderComponent::setOtherTrackSoloed(synth::TrackId id, bool soloed) {
    if (isSoloedNow(id) == soloed)
        return;
    if (auto* link = linkSurface(); link != nullptr && link->linkedChannelSoloed(id).has_value()) {
        link->toggleLinkedChannelSoloed(id);
        return;
    }
    performEdit([this, id, soloed] { doc_.setTrackSoloed(id, soloed); });
}

void TimelineTrackHeaderComponent::toggleMuted() {
    const auto targets = bulkTargets();
    if (targets.size() < 2) {
        toggleMutedOne();
        return;
    }
    host_->performTrackGroupEdit([this, &targets] {
        toggleMutedOne();
        const bool muted = isMutedNow(trackId_);
        for (const auto id : targets)
            if (id != trackId_)
                setOtherTrackMuted(id, muted);
    });
    refreshFromDoc(); // a linked strip's state is not a doc change: nothing else tells the rows
}

void TimelineTrackHeaderComponent::toggleSoloed() {
    const auto targets = bulkTargets();
    if (targets.size() < 2) {
        toggleSoloedOne();
        return;
    }
    host_->performTrackGroupEdit([this, &targets] {
        toggleSoloedOne();
        const bool soloed = isSoloedNow(trackId_);
        for (const auto id : targets)
            if (id != trackId_)
                setOtherTrackSoloed(id, soloed);
    });
    refreshFromDoc();
}

void TimelineTrackHeaderComponent::toggleMutedOne() {
    const auto* t = track();
    if (t == nullptr)
        return;
    // docs/mixer/mixer.md#channels-follow-audio-not-tracks (c): a LINKED track's M IS the channel's mute -- one
    // mute, not two. The surface returns false for a shared channel (or no channel at all), and note gating below is
    // then exactly what it has always been. The refresh is explicit because a strip write is not a doc change: nothing
    // notifies the header otherwise.
    if (auto* link = linkSurface(); link != nullptr && link->toggleLinkedChannelMuted(trackId_)) {
        refreshFromDoc();
        return;
    }
    const bool next = !t->muted;
    performEdit([this, next] { doc_.setTrackMuted(trackId_, next); });
}

void TimelineTrackHeaderComponent::toggleSoloedOne() {
    const auto* t = track();
    if (t == nullptr)
        return;
    if (auto* link = linkSurface(); link != nullptr && link->toggleLinkedChannelSoloed(trackId_)) {
        refreshFromDoc(); // see toggleMuted
        return;
    }
    const bool next = !t->soloed;
    performEdit([this, next] { doc_.setTrackSoloed(trackId_, next); });
}

} // namespace synth::ui
