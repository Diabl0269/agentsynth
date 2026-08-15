#pragma once

#include <cstdint>
#include <juce_core/juce_core.h>
#include <type_traits>
#include <utility>
#include <vector>

namespace synth {

namespace detail {

// Strongly-typed timeline id. Tagged so a ClipId can never be passed where a TrackId is
// expected: the whole point is that ids are opaque handles the doc hands out, not indices.
// Ids are assigned monotonically per doc, never reused within a doc's lifetime (removing a
// track does not free its id), and survive a toVar/fromVar round trip — the doc persists its
// next-id counters, so anything holding an id across a save/load still resolves.
// value == 0 is the "invalid / not found" sentinel every failing factory returns.
template <typename Tag>
struct TimelineId {
    std::int64_t value = 0;

    constexpr bool isValid() const noexcept { return value > 0; }

    constexpr bool operator==(const TimelineId& other) const noexcept { return value == other.value; }
    constexpr bool operator!=(const TimelineId& other) const noexcept { return value != other.value; }
    constexpr bool operator<(const TimelineId& other) const noexcept { return value < other.value; }
    constexpr bool operator>(const TimelineId& other) const noexcept { return other.value < value; }
    constexpr bool operator<=(const TimelineId& other) const noexcept { return !(other.value < value); }
    constexpr bool operator>=(const TimelineId& other) const noexcept { return !(value < other.value); }
};

struct TrackIdTag {};
struct ClipIdTag {};
struct LaneIdTag {};
struct NoteIdTag {};

} // namespace detail

using TrackId = detail::TimelineId<detail::TrackIdTag>;
using ClipId = detail::TimelineId<detail::ClipIdTag>;
using LaneId = detail::TimelineId<detail::LaneIdTag>;
using NoteId = detail::TimelineId<detail::NoteIdTag>;

// Serialised as an int, so these numbers are format. Values 3..15 are reserved for future
// kinds; a file carrying one is rejected by fromVar rather than coerced into a kind this
// build understands.
enum class TrackKind : int {
    Midi = 0,
    Audio = 1,      // reserved until TL6 — the model accepts it, nothing renders it yet
    Automation = 2, // reserved: lanes may live on their own track row later
};

static_assert(static_cast<int>(TrackKind::Midi) == 0, "TrackKind is serialised as an int — renumbering breaks files");
static_assert(static_cast<int>(TrackKind::Audio) == 1, "TrackKind is serialised as an int — renumbering breaks files");
static_assert(static_cast<int>(TrackKind::Automation) == 2,
              "TrackKind is serialised as an int — renumbering breaks files");

// Interpolation from a breakpoint to the next one. Serialised as an int (same contract as
// TrackKind: the numbers are format).
enum class BreakpointCurve : int {
    Hold = 0,
    Linear = 1,
    Bezier = 2, // reserved — reads/writes the `tension` field, no evaluator yet
};

static_assert(static_cast<int>(BreakpointCurve::Hold) == 0,
              "BreakpointCurve is serialised as an int — renumbering breaks files");
static_assert(static_cast<int>(BreakpointCurve::Linear) == 1,
              "BreakpointCurve is serialised as an int — renumbering breaks files");
static_assert(static_cast<int>(BreakpointCurve::Bezier) == 2,
              "BreakpointCurve is serialised as an int — renumbering breaks files");

// One note inside a clip. startBeat is CLIP-RELATIVE (offset from the clip's own startBeat),
// so moving a clip moves its notes with it and never rewrites them. `id` is a stable,
// doc-assigned handle (TL2-3) — never reused, and it survives a toVar/fromVar round trip the
// same way Track/Clip/Lane ids do.
struct MidiNote {
    NoteId id;
    double startBeat = 0.0;
    double lengthBeats = 1.0;
    int pitch = 60;
    int velocity = 100;
    int channel = 1;
};

// Notes within a clip stay sorted by (startBeat, pitch, id) — that invariant is what makes the
// audio-thread snapshot build (TL2-2) a flatten instead of a sort. The id is only a tiebreaker
// (startBeat/pitch collisions are legal — e.g. a chord). TimelineDoc's mutation API maintains
// the order on every path and fromVar re-establishes it defensively.
struct Clip {
    ClipId id;
    juce::String name;
    double startBeat = 0.0;
    double lengthBeats = 4.0;
    std::vector<MidiNote> notes;
};

// One automated parameter. Identity is the (nodeUuid, paramId) pair, doc-wide: there is at
// most one lane per bound parameter anywhere in the document.
struct AutomationLane {
    // The parameter's range as it was when the lane was created. Breakpoint values are stored
    // DENORMALISED (in the parameter's own units), so a later build that widens or narrows the
    // range can compare against this snapshot and notice, instead of silently reinterpreting
    // every point.
    struct RangeSnapshot {
        float minValue = 0.0f;
        float maxValue = 1.0f;
        float defaultValue = 0.0f;
    };

    struct Breakpoint {
        double beat = 0.0;
        double value = 0.0;
        float tension = 0.0f;                                  // Bezier shape, [-1, 1]; unused by Hold/Linear
        int curve = static_cast<int>(BreakpointCurve::Linear); // a BreakpointCurve value
    };

    LaneId id;
    juce::String nodeUuid; // the graph node's "uuid" property — NOT its integer NodeID, which
                           // merge-mode graph apply is free to renumber
    juce::String paramId;
    RangeSnapshot range;
    std::vector<Breakpoint> points; // sorted by beat; beats are unique (a second insert at the
                                    // same beat replaces the existing point)
};

// Clips within a track stay sorted by (startBeat, id). Overlapping clips are legal in the
// model — whether the UI allows drawing them is a policy decision made higher up.
struct Track {
    TrackId id;
    TrackKind kind = TrackKind::Midi;
    juce::String name;
    juce::uint32 colourArgb = 0xff808080; // UI-neutral placeholder; the editor picks real colours
    bool muted = false;
    bool soloed = false;
    bool armed = false;
    juce::String bindingUuid; // MIDI tracks: uuid of the Track In node this track feeds (TL3).
                              // May be empty — an unbound track is legal, it just plays nowhere.
    std::vector<Clip> clips;
    std::vector<AutomationLane> lanes;
};

// The timeline's message-thread document model (TL2-1): tracks, clips, notes and automation
// lanes, in beats. Mutable and serialisable, with no GUI or editor dependency.
//
// Beats are canonical — nothing here stores samples, and tempo/time signature deliberately
// live on TransportService, not in this document (a tempo map moves in here only when real
// tempo maps arrive).
//
// Single mutation choke point: every public mutator validates its arguments, then funnels the
// actual edit through the private applyMutation(), which bumps the revision counter and fires
// one change notification. Everything downstream hangs off that one seam — the audio-thread
// snapshot republish (TL2-2), undo capture (TL2-5) and dirty-marking for .agsproj save (TL2-4).
// A call that changes nothing (a rejected note, addLane finding an existing lane, a setter
// given the value already stored) returns without entering applyMutation, so it neither bumps
// the revision nor notifies.
//
// Threading: message thread only. It is not safe to read this doc from the audio thread; that
// is what the immutable snapshot in TL2-2 exists for. Listeners must treat the doc as const
// for the duration of timelineChanged().
class TimelineDoc {
public:
    TimelineDoc() = default;

    class Listener {
    public:
        virtual ~Listener() = default;
        // Fired once per effective mutation, after the edit is complete and the revision has
        // been bumped. The doc is fully consistent (all sorted invariants hold) when this runs.
        virtual void timelineChanged(const TimelineDoc& doc) = 0;
    };

    void addListener(Listener* listener);
    void removeListener(Listener* listener);

    // -- Hard caps ------------------------------------------------------------
    // The model is bounded before validateTimeline (TL8-1) ever sees untrusted input: both the
    // mutation API and fromVar REJECT anything that would exceed a cap rather than clamping or
    // truncating, so a doc in memory can never be larger than these allow. Sized well above
    // anything a human would author by hand.
    static constexpr int kMaxTracks = 256;
    static constexpr int kMaxClipsPerTrack = 4096;
    static constexpr int kMaxNotesPerClip = 16384;
    static constexpr int kMaxLanesPerTrack = 512;
    static constexpr int kMaxBreakpointsPerLane = 16384;

    // Written as the "version" field of toVar() and required (exactly) by fromVar(). Bump only
    // for a genuinely breaking change — adding a field is additive and must not bump it.
    static constexpr int kFormatVersion = 1;

    // -- Tracks ---------------------------------------------------------------
    // Appends a track. Returns an invalid TrackId if the doc is already at kMaxTracks.
    TrackId addTrack(TrackKind kind, const juce::String& name);
    // Removes the track and everything on it. The id is retired, never reissued.
    bool removeTrack(TrackId id);
    bool setTrackName(TrackId id, const juce::String& name);
    bool setTrackColour(TrackId id, juce::uint32 colourArgb);
    bool setTrackMuted(TrackId id, bool muted);
    bool setTrackSoloed(TrackId id, bool soloed);
    bool setTrackArmed(TrackId id, bool armed);
    // Binds the track to a graph node by its "uuid" property. Pass an empty string to unbind.
    bool setTrackBinding(TrackId id, const juce::String& nodeUuid);

    // nullptr once the track is gone. The pointer is invalidated by the next mutation — never
    // hold it across one.
    const Track* getTrack(TrackId id) const;
    const std::vector<Track>& getTracks() const noexcept { return tracks; }

    // -- Clips ----------------------------------------------------------------
    // startBeat must be finite and >= 0, lengthBeats finite and > 0. Inserted in sorted
    // position; returns an invalid ClipId on rejection.
    ClipId addClip(TrackId trackId, double startBeat, double lengthBeats, const juce::String& name);
    bool removeClip(ClipId id);
    // Re-seats the clip at its new sorted position; notes move with it (they are clip-relative).
    bool moveClip(ClipId id, double newStartBeat);
    bool resizeClip(ClipId id, double newLengthBeats);

    const Clip* getClip(ClipId id) const;
    const Track* getTrackForClip(ClipId id) const;

    // Splits the clip at atBeat (CLIP-RELATIVE, must land strictly inside (0, length), else
    // rejected). The original id stays on the left part (selection stability); the right part
    // gets a freshly-assigned id. Notes entirely before the split stay on the left clip
    // untouched; notes entirely at/after it move to the right clip, re-based to its new start;
    // a note straddling the boundary is itself split in two — the left half keeps the original
    // note's id and is truncated to end exactly at the boundary, the right half gets a new id,
    // starts at beat 0, and runs for the remainder, with the same pitch/velocity/channel. One
    // mutation. Returns a pair of invalid ids if the clip doesn't exist, atBeat is out of range,
    // or the track is already at kMaxClipsPerTrack.
    std::pair<ClipId, ClipId> splitClip(ClipId id, double atBeat);
    // Joins clip b into clip a: both must be on the same track, a must start strictly before b,
    // and they must not overlap (a gap between them is legal and becomes silence) — overlapping
    // clips are rejected so note re-basing stays unambiguous. On success, a is extended to span
    // [a.start, b.end), b's notes are re-based by (b.start - a.start) and merged into a's note
    // list (sorted merge), b is removed, and one mutation fires. Rejected if the merged note
    // count would exceed kMaxNotesPerClip.
    bool joinClips(ClipId a, ClipId b);
    // Appends a copy of the clip immediately after it (new start = start + length), same name
    // and length, with a fresh id for the clip and for every one of its (deep-copied) notes.
    // Rejects if the track is already at kMaxClipsPerTrack.
    ClipId duplicateClip(ClipId id);

    // -- Notes ----------------------------------------------------------------
    // Assigns and returns a stable NoteId; an invalid id means the note was rejected — a
    // non-finite or negative startBeat, a non-positive length, pitch outside 0..127, velocity
    // outside 1..127, channel outside 1..16, or a clip already at kMaxNotesPerClip.
    NoteId addNote(ClipId clipId, const MidiNote& note);
    bool clearNotes(ClipId clipId);

    // Note editing API (TL2-3). Each validates first and only then mutates: a rejection or a
    // no-op (the value asked for is what's already there) returns without bumping the revision
    // or notifying listeners.
    bool removeNote(NoteId id);
    // newStartBeat is CLIP-RELATIVE. Rejects a non-finite/negative start or a pitch outside
    // 0..127. Keeps the clip's note list sorted by re-positioning the moved note in place, not
    // by re-sorting the whole vector.
    bool moveNote(NoteId id, double newStartBeat, int newPitch);
    bool resizeNote(NoteId id, double newLengthBeats); // rejects non-positive/non-finite
    bool setNoteVelocity(NoteId id, int velocity);     // rejects outside 1..127

    // Snaps every note in the clip toward the nearest multiple of gridBeats (rejected if <= 0 or
    // non-finite): newStart = start + strength * (nearestGridMultiple(start) - start), with
    // strength clamped to [0, 1] (0 = no movement, 1 = hard snap). Lengths are untouched; a
    // result is clamped at 0. The whole clip re-sorts once at the end and the edit is one
    // mutation, however many notes move. A call that moves nothing is a no-op.
    bool quantiseNotes(ClipId clipId, double gridBeats, double strength);

    const MidiNote* getNote(NoteId id) const;
    const Clip* getClipForNote(NoteId id) const;

    // -- Automation lanes ------------------------------------------------------
    // One lane per bound parameter, doc-wide: if a lane for (nodeUuid, paramId) already exists
    // ANYWHERE in the doc, its id is returned and nothing is mutated (no revision bump, no
    // notification) — even if the existing lane sits on a different track.
    LaneId addLane(TrackId trackId, const juce::String& nodeUuid, const juce::String& paramId,
                   const AutomationLane::RangeSnapshot& range);
    bool removeLane(LaneId id);
    // Sorted insert. A point at the exact same beat REPLACES the existing one. `value` is
    // denormalised and clamped into the lane's range snapshot; `tension` is clamped to [-1, 1];
    // `curve` must be a BreakpointCurve value.
    bool addBreakpoint(LaneId laneId, double beat, double value, float tension = 0.0f,
                       int curve = static_cast<int>(BreakpointCurve::Linear));
    bool removeBreakpoint(LaneId laneId, double beat);

    const AutomationLane* getLane(LaneId id) const;
    const Track* getTrackForLane(LaneId id) const;
    // The lane bound to (nodeUuid, paramId), or nullptr — the doc-wide identity lookup addLane
    // dedupes against.
    const AutomationLane* getLaneForParam(const juce::String& nodeUuid, const juce::String& paramId) const;

    // -- Document-level --------------------------------------------------------
    // Drops every track. Id counters are NOT reset — ids stay unique for the doc's lifetime.
    // No-op (and no notification) on an already-empty doc.
    void clear();

    bool isEmpty() const noexcept { return tracks.empty(); }

    // Bumped once per effective mutation. Starts at 0 on a fresh doc; consumers cache it to
    // tell "nothing happened" from "something did" without diffing the model.
    std::int64_t getRevision() const noexcept { return revision; }

    // -- Serialisation ---------------------------------------------------------
    // The dialect PatchDocument/TL2-4 embeds under the reserved top-level "timeline" key.
    // Field names are lowerCamelCase; the next-id counters ride along so ids stay stable
    // across save/load.
    juce::var toVar() const;

    // All-or-nothing: on ANY malformed field (wrong type, out-of-range value, duplicate id,
    // unknown track kind, a cap exceeded, a wrong "version") this returns false and leaves the
    // doc completely untouched — same content, same revision, no notification. On success it
    // replaces the whole doc, bumps the revision once and fires one notification.
    //
    // Ordering is repaired rather than trusted: a hand-edited file with mis-ordered clips,
    // notes or breakpoints loads fine and comes back sorted, because no reader downstream
    // should have to defend against a broken sort invariant.
    bool fromVar(const juce::var& state);

private:
    // The one place the doc is allowed to change. Callers validate first and only enter here
    // when the edit is guaranteed to be effective, which is what keeps "one notification per
    // real change" true without any diffing.
    template <typename Fn>
    auto applyMutation(Fn&& mutate) -> std::invoke_result_t<Fn> {
        if constexpr (std::is_void_v<std::invoke_result_t<Fn>>) {
            mutate();
            finishMutation();
        } else {
            auto result = mutate();
            finishMutation();
            return result;
        }
    }

    void finishMutation();

    Track* findTrack(TrackId id);
    const Track* findTrack(TrackId id) const;
    Clip* findClip(ClipId id, Track** ownerOut = nullptr);
    MidiNote* findNote(NoteId id, Clip** ownerOut = nullptr);
    AutomationLane* findLane(LaneId id, Track** ownerOut = nullptr);
    AutomationLane* findLaneForParam(const juce::String& nodeUuid, const juce::String& paramId);

    std::vector<Track> tracks;

    std::int64_t nextTrackId = 1;
    std::int64_t nextClipId = 1;
    std::int64_t nextLaneId = 1;
    std::int64_t nextNoteId = 1;
    std::int64_t revision = 0;

    juce::ListenerList<Listener> listeners;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimelineDoc)
};

} // namespace synth
