// Concern: listeners/notification, id lookups and whole-document lifecycle.
#include "TimelineDoc.h"

#include <algorithm>

namespace synth {

// ------------------------------------------------------------------ listeners --

void TimelineDoc::addListener(Listener* listener) {
    if (listener != nullptr)
        listeners.add(listener);
}

void TimelineDoc::removeListener(Listener* listener) {
    if (listener != nullptr)
        listeners.remove(listener);
}

void TimelineDoc::finishMutation() {
    ++revision;
    listeners.call([this](Listener& l) { l.timelineChanged(*this); });
}

// -------------------------------------------------------------------- lookups --

Track* TimelineDoc::findTrack(TrackId id) {
    if (!id.isValid())
        return nullptr;
    for (auto& track : tracks)
        if (track.id == id)
            return &track;
    return nullptr;
}

const Track* TimelineDoc::findTrack(TrackId id) const { return const_cast<TimelineDoc*>(this)->findTrack(id); }

Clip* TimelineDoc::findClip(ClipId id, Track** ownerOut) {
    if (!id.isValid())
        return nullptr;
    for (auto& track : tracks) {
        for (auto& clip : track.clips) {
            if (clip.id == id) {
                if (ownerOut != nullptr)
                    *ownerOut = &track;
                return &clip;
            }
        }
    }
    return nullptr;
}

MidiNote* TimelineDoc::findNote(NoteId id, Clip** ownerOut) {
    if (!id.isValid())
        return nullptr;
    for (auto& track : tracks) {
        for (auto& clip : track.clips) {
            for (auto& note : clip.notes) {
                if (note.id == id) {
                    if (ownerOut != nullptr)
                        *ownerOut = &clip;
                    return &note;
                }
            }
        }
    }
    return nullptr;
}

AutomationLane* TimelineDoc::findLane(LaneId id, Track** ownerOut) {
    if (!id.isValid())
        return nullptr;
    for (auto& track : tracks) {
        for (auto& lane : track.lanes) {
            if (lane.id == id) {
                if (ownerOut != nullptr)
                    *ownerOut = &track;
                return &lane;
            }
        }
    }
    return nullptr;
}

AutomationLane* TimelineDoc::findLaneForParam(const juce::String& nodeUuid, const juce::String& paramId) {
    for (auto& track : tracks)
        for (auto& lane : track.lanes)
            if (lane.nodeUuid == nodeUuid && lane.paramId == paramId)
                return &lane;
    return nullptr;
}

Marker* TimelineDoc::findMarker(MarkerId id) {
    for (auto& marker : markers)
        if (marker.id == id)
            return &marker;
    return nullptr;
}

const Track* TimelineDoc::getTrack(TrackId id) const { return findTrack(id); }

double TimelineDoc::getArrangementEndBeat() const noexcept {
    double endBeat = 0.0;
    for (const auto& track : tracks)
        for (const auto& clip : track.clips)
            endBeat = std::max(endBeat, clip.startBeat + clip.lengthBeats);
    return endBeat;
}

const Clip* TimelineDoc::getClip(ClipId id) const { return const_cast<TimelineDoc*>(this)->findClip(id); }

const Track* TimelineDoc::getTrackForClip(ClipId id) const {
    Track* owner = nullptr;
    if (const_cast<TimelineDoc*>(this)->findClip(id, &owner) == nullptr)
        return nullptr;
    return owner;
}

const MidiNote* TimelineDoc::getNote(NoteId id) const { return const_cast<TimelineDoc*>(this)->findNote(id); }

const Clip* TimelineDoc::getClipForNote(NoteId id) const {
    Clip* owner = nullptr;
    if (const_cast<TimelineDoc*>(this)->findNote(id, &owner) == nullptr)
        return nullptr;
    return owner;
}

const AutomationLane* TimelineDoc::getLane(LaneId id) const { return const_cast<TimelineDoc*>(this)->findLane(id); }

const Track* TimelineDoc::getTrackForLane(LaneId id) const {
    Track* owner = nullptr;
    if (const_cast<TimelineDoc*>(this)->findLane(id, &owner) == nullptr)
        return nullptr;
    return owner;
}

const AutomationLane* TimelineDoc::getLaneForParam(const juce::String& nodeUuid, const juce::String& paramId) const {
    return const_cast<TimelineDoc*>(this)->findLaneForParam(nodeUuid, paramId);
}

const Marker* TimelineDoc::getMarker(MarkerId id) const { return const_cast<TimelineDoc*>(this)->findMarker(id); }

// ----------------------------------------------------------------- doc-level --

void TimelineDoc::clear() {
    if (tracks.empty() && markers.empty())
        return;
    applyMutation([&] {
        tracks.clear();
        markers.clear();
    });
}

} // namespace synth
