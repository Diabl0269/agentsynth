// Concern: tracks.
#include "TimelineDoc.h"

namespace synth {

// --------------------------------------------------------------------- tracks --

TrackId TimelineDoc::addTrack(TrackKind kind, const juce::String& name) {
    if (static_cast<int>(tracks.size()) >= kMaxTracks)
        return {};
    switch (kind) {
    case TrackKind::Midi:
    case TrackKind::Audio:
    case TrackKind::Automation:
        break;
    default:
        return {};
    }

    return applyMutation([&] {
        Track track;
        track.id = TrackId{nextTrackId++};
        track.kind = kind;
        track.name = name;
        tracks.push_back(std::move(track));
        return tracks.back().id;
    });
}

bool TimelineDoc::removeTrack(TrackId id) {
    auto* track = findTrack(id);
    if (track == nullptr)
        return false;

    return applyMutation([&] {
        tracks.erase(tracks.begin() + (track - tracks.data()));
        return true;
    });
}

bool TimelineDoc::moveTrack(TrackId id, int newIndex) {
    auto* track = findTrack(id);
    if (track == nullptr)
        return false;

    const int oldIndex = (int)(track - tracks.data());
    const int clampedIndex = juce::jlimit(0, (int)tracks.size() - 1, newIndex);
    if (clampedIndex == oldIndex)
        return true; // already there: no revision bump, no notification

    return applyMutation([&] {
        // Erase-then-insert AT clampedIndex (not adjusted for the erase) is deliberate: removing
        // the track from oldIndex never changes where an index BELOW oldIndex points, and inserting
        // at clampedIndex in the now-shorter vector already lands one slot earlier than it would
        // have pre-erase, which is exactly the compensation a target index ABOVE oldIndex needs.
        Track moved = std::move(tracks[(size_t)oldIndex]);
        tracks.erase(tracks.begin() + oldIndex);
        tracks.insert(tracks.begin() + clampedIndex, std::move(moved));
        return true;
    });
}

bool TimelineDoc::setTrackName(TrackId id, const juce::String& name) {
    auto* track = findTrack(id);
    if (track == nullptr)
        return false;
    if (track->name == name)
        return true; // already there: no revision bump, no notification
    return applyMutation([&] {
        track->name = name;
        return true;
    });
}

bool TimelineDoc::setTrackColour(TrackId id, juce::uint32 colourArgb) {
    auto* track = findTrack(id);
    if (track == nullptr)
        return false;
    if (track->colourArgb == colourArgb)
        return true;
    return applyMutation([&] {
        track->colourArgb = colourArgb;
        return true;
    });
}

bool TimelineDoc::setTrackMuted(TrackId id, bool muted) {
    auto* track = findTrack(id);
    if (track == nullptr)
        return false;
    if (track->muted == muted)
        return true;
    return applyMutation([&] {
        track->muted = muted;
        return true;
    });
}

bool TimelineDoc::setTrackSoloed(TrackId id, bool soloed) {
    auto* track = findTrack(id);
    if (track == nullptr)
        return false;
    if (track->soloed == soloed)
        return true;
    return applyMutation([&] {
        track->soloed = soloed;
        return true;
    });
}

bool TimelineDoc::setTrackArmed(TrackId id, bool armed) {
    auto* track = findTrack(id);
    if (track == nullptr)
        return false;
    if (track->armed == armed)
        return true;
    return applyMutation([&] {
        track->armed = armed;
        return true;
    });
}

bool TimelineDoc::setTrackBinding(TrackId id, const juce::String& nodeUuid) {
    auto* track = findTrack(id);
    if (track == nullptr)
        return false;
    if (track->bindingUuid == nodeUuid)
        return true;
    return applyMutation([&] {
        track->bindingUuid = nodeUuid;
        // Optimistic: whatever the track's prior orphan state was, a changed binding hasn't been
        // checked against the graph yet. reconcileBindings re-derives the truth on the next pass;
        // in the meantime an empty nodeUuid is "unbound" (never orphaned) and a non-empty one is
        // presumed live rather than left flagged against a target it no longer even names.
        track->orphaned = false;
        return true;
    });
}

} // namespace synth
