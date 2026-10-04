// Concern: tracks.
#include "TimelineDoc.h"
#include <algorithm>
#include <cmath>

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
        const TrackId id = track.id;
        tracks.push_back(std::move(track));
        tracks = automationTracksLast(std::move(tracks));
        return id;
    });
}

// The Automation track holds lanes no single track owns and is drawn as the closing "Unassigned
// automation" section, so every path that changes track order (add, move, load) keeps it last.
std::vector<Track> TimelineDoc::automationTracksLast(std::vector<Track> list) {
    std::stable_partition(list.begin(), list.end(), [](const Track& t) { return t.kind != TrackKind::Automation; });
    return list;
}

// The Automation track only exists to hold lanes no single track owns. Callers that take a lane off
// it (a move, a delete) run this inside the same undo step, so undo brings the track back with it.
bool TimelineDoc::removeEmptyAutomationTracks() {
    const auto isEmptyAutomation = [](const Track& t) {
        return t.kind == TrackKind::Automation && t.lanes.empty() && t.clips.empty();
    };
    if (std::none_of(tracks.begin(), tracks.end(), isEmptyAutomation))
        return false;
    return applyMutation([&] {
        tracks.erase(std::remove_if(tracks.begin(), tracks.end(), isEmptyAutomation), tracks.end());
        return true;
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

// Inserts a copy of `source` directly below it with fresh ids on the track, its clips, their notes and its lanes:
// same kind, clips, notes and lane points, a colour of `colourArgb`, never soloed or armed.
// `uuidRemap` (original node uuid -> its copy's uuid) rebinds the copy's track binding and lanes; a lane whose
// node is not in the map is left off, because a (node, param) pair carries one lane doc-wide. Invalid TrackId
// when `source` is missing, is the Automation track, or the doc is at kMaxTracks.
TrackId TimelineDoc::duplicateTrack(TrackId source, const juce::String& name, juce::uint32 colourArgb,
                                    const std::map<juce::String, juce::String>& uuidRemap) {
    const auto* original = findTrack(source);
    if (original == nullptr || original->kind == TrackKind::Automation || static_cast<int>(tracks.size()) >= kMaxTracks)
        return {};

    return applyMutation([&] {
        const auto remap = [&uuidRemap](const juce::String& uuid) {
            const auto it = uuidRemap.find(uuid);
            return it != uuidRemap.end() ? it->second : juce::String();
        };

        // Copied field by field, not by struct assignment: every id must be new.
        Track copy;
        copy.id = TrackId{nextTrackId++};
        copy.kind = original->kind;
        copy.name = name;
        copy.colourArgb = colourArgb;
        copy.muted = original->muted;
        copy.heightScale = original->heightScale;
        copy.bindingUuid = remap(original->bindingUuid);
        for (const auto& clip : original->clips) {
            Clip dup = clip;
            dup.id = ClipId{nextClipId++};
            for (auto& note : dup.notes)
                note.id = NoteId{nextNoteId++};
            copy.clips.push_back(std::move(dup));
        }
        for (const auto& lane : original->lanes) {
            const auto uuid = remap(lane.nodeUuid);
            if (uuid.isEmpty())
                continue;
            AutomationLane dup = lane;
            dup.id = LaneId{nextLaneId++};
            dup.nodeUuid = uuid;
            dup.orphaned = false;
            copy.lanes.push_back(std::move(dup));
        }

        const TrackId id = copy.id;
        const auto at = tracks.begin() + (original - tracks.data()) + 1;
        tracks.insert(at, std::move(copy));
        return id;
    });
}

bool TimelineDoc::moveTrack(TrackId id, int newIndex) {
    auto* track = findTrack(id);
    if (track == nullptr)
        return false;

    if (track->kind == TrackKind::Automation)
        return true; // pinned after every other track: never moves, and nothing moves below it
    const int oldIndex = (int)(track - tracks.data());
    // The other tracks stay above the Automation track(s), so the last index they may take is the
    // last non-Automation slot.
    const int movable = (int)std::count_if(tracks.begin(), tracks.end(),
                                           [](const Track& t) { return t.kind != TrackKind::Automation; });
    const int clampedIndex = juce::jlimit(0, std::max(0, movable - 1), newIndex);
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

bool TimelineDoc::setTrackHeightScale(TrackId id, double scale) {
    auto* track = findTrack(id);
    if (track == nullptr || !std::isfinite(scale))
        return false;
    const double clamped = std::clamp(scale, Track::kMinHeightScale, Track::kMaxHeightScale);
    if (track->heightScale == clamped)
        return true;
    return applyMutation([&] {
        track->heightScale = clamped;
        return true;
    });
}

bool TimelineDoc::resetTrackHeightScales() {
    const bool anyDiffers =
        std::any_of(tracks.begin(), tracks.end(), [](const Track& t) { return t.heightScale != 1.0; });
    if (!anyDiffers)
        return false;
    return applyMutation([&] {
        for (auto& track : tracks)
            track.heightScale = 1.0;
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
