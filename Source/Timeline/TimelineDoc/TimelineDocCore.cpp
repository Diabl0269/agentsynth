// Concern: listeners/notification, id lookups and whole-document lifecycle.
#include "TimelineDoc.h"

#include <algorithm>

// ------------------------------------------------------------ model field notes --
//
// MidiNote::muted:
// Per-note mute (the Mute tool's note-level half). The note keeps its id, its fields and its
// place in the sorted list; it is simply not flattened into TimelineSnapshot. Deliberately
// NOT modelled as velocity 0 or as a delete: a mute has to be exactly reversible, and both of
// those destroy information the user expects back when they un-mute. The flag travels with the
// note through every structural edit — a split re-bases it, a duplicate copies it, a join
// merges it — because it is part of the note, not of the clip that happens to hold it.
// Additive field: absent in a file loads as false, and kFormatVersion stays 1.
//
// Clip:
// Notes within a clip stay sorted by (startBeat, pitch, id) — that invariant is what makes the
// audio-thread snapshot build a flatten instead of a sort. The id is only a tiebreaker
// (startBeat/pitch collisions are legal — e.g. a chord). TimelineDoc's mutation API maintains
// the order on every path and fromVar re-establishes it defensively.
//
// One Clip type covers both MIDI and audio: a MIDI clip is one
// whose `assetRef` is empty and whose notes carry the music; an audio clip is one that names a
// rendered asset instead. Nothing forbids a clip from carrying both — the model is deliberately
// permissive, and it is the track's TrackKind that says which half a player reads. Every audio
// field is ADDITIVE (absent in a file => the default below) and the format version stays 1.
//
// Clip::assetRef:
// The clip's audio asset, as a path RELATIVE TO THE BUNDLE ROOT ("Audio/take-1.wav").
// EMPTY for a MIDI clip. The relative-only rule is a security boundary, not a convenience:
// a bundle can be handed to someone else, and an absolute (or `..`-escaping) path baked into
// one would read whatever happens to sit at that path on their machine — the same reasoning
// that keeps a provider-authored patch's `"state"` on the trusted path only (see
// ModuleBase::setExtraState and the asset policy on ProjectBundle). setClipAsset and fromVar
// both REJECT anything that is not bundle-relative; there is no sanitising fallback.
//
// Clip::muted:
// Per-clip mute (the Mute tool's clip-level half). A muted clip keeps its id, its position,
// its notes and every audio field, and contributes NOTHING to TimelineSnapshot — no note
// events, no AudioClipInfo entry. Doing the exclusion at flatten time is what keeps every
// downstream consumer (TimelineMidiSource, the AudioClipStreamer's assignment table) unaware
// that mute exists at all: they simply never see the clip.
//
// Independent of Track::muted — un-muting a track does not un-mute a clip the user muted, and
// the two are ORed by omission (a muted track's whole run is silenced by its consumers, a
// muted clip is missing from the run in the first place). It survives every structural edit:
// both halves of a split inherit it, a duplicate copies it, and a join keeps the SURVIVING
// clip's flag (see splitClip / duplicateClip / joinClips). Additive field: absent in a file
// loads as false, and kFormatVersion stays 1.
//
// AutomationLane::paramIndexHint:
// The target parameter's index within its processor's getParameters(), captured ONCE at
// lane-creation time (never re-derived afterwards). -1 means "no hint" — every lane on a
// non-plugin module (the resolver only ever consults this for
// a HostedPluginModule node; see Source/Timeline/AutomationBinding.h). It exists to let a lane
// whose hosted plugin has no stable parameter ids at all (a legacy format) still re-bind after a
// reload, WITHOUT ever letting an index match paper over a plugin update that moved a DIFFERENT,
// still-stably-identified parameter into that slot — that case orphans instead. Additive field:
// absent in a file loads as -1.
//
// AutomationLane::orphaned:
// RUNTIME-ONLY: true when nodeUuid is non-empty but does not resolve to any live
// graph node's "uuid" property. Never set directly by a caller — TimelineDoc::reconcileBindings
// is the only writer, driven by synth::TimelineReconciler against a real graph. NOT written by
// toVar and always reset to false by fromVar (a freshly-loaded doc has no graph to check
// against, so the very next reconcile is what re-derives the truth — this field is derived
// state, not part of the document's persistent identity).
//
// Distinct from "unbound": an EMPTY nodeUuid never was bound to anything and is merely
// unbound, not orphaned. Orphaned means it WAS bound and no longer resolves — a deleted
// module, a rejected restore — and the lane is retained regardless: an
// orphaned binding is surfaced for the user to re-bind (see rebindLane), never auto-deleted.
//
// Marker:
// One named position in the arrangement — a cue point the ruler draws a flag for. Deliberately
// NOT a track: a marker has no clips, no binding and no audible effect, it is a label on the
// timebase, so it lives on the document rather than in the track list (a "marker track" would
// otherwise have to be excluded from every place that iterates tracks to make sound).
//
// `beat` is absolute (unlike a note's clip-relative start), `text` is free-form and may be empty
// (a flag with no label is legal), and `colourArgb` is inert display data. Markers stay sorted by
// (beat, id) — the same rule clips follow, so a ruler paint is a walk rather than a sort.
//
// TimelineDoc:
// The timeline's message-thread document model: tracks, clips, notes, automation
// lanes and markers, in beats. Mutable and serialisable, with no GUI or editor dependency.
//
// Beats are canonical — nothing here stores samples, and tempo/time signature deliberately
// live on TransportService, not in this document (a tempo map moves in here only when real
// tempo maps arrive).
//
// Single mutation choke point: every public mutator validates its arguments, then funnels the
// actual edit through the private applyMutation(), which bumps the revision counter and fires
// one change notification. Everything downstream hangs off that one seam — the audio-thread
// snapshot republish, undo capture and dirty-marking for .agsproj save.
// A call that changes nothing (a rejected note, addLane finding an existing lane, a setter
// given the value already stored) returns without entering applyMutation, so it neither bumps
// the revision nor notifies.
//
// Threading: message thread only. It is not safe to read this doc from the audio thread; that
// is what the immutable snapshot exists for. Listeners must treat the doc as const
// for the duration of timelineChanged().

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
    dissolveLoneLinkGroups(); // every mutation ends with no link group of fewer than two clips
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

std::set<juce::String> TimelineDoc::automatedParamIds(const juce::String& nodeUuid) const {
    std::set<juce::String> out;
    if (nodeUuid.isEmpty())
        return out;
    for (const auto& track : tracks)
        for (const auto& lane : track.lanes)
            if (lane.nodeUuid == nodeUuid)
                out.insert(lane.paramId);
    return out;
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
