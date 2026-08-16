#pragma once

#include <cstdint>
#include <functional>
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
    Audio = 1,      // reserved — the model accepts it, nothing renders it yet
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

// Per-lane automation record mode. Serialised as an int (same contract as TrackKind and
// BreakpointCurve: the numbers are format), and flattened into TimelineSnapshot::LaneInfo so the
// audio thread's applier can honour it without touching the doc.
//
// Semantics (the applier implements the audio half, synth::AutomationRecorder the capture half):
//   Off   — the lane is inert. Automation never plays back and nothing is ever recorded into it.
//   Read  — playback only. The default, and what a lane with no stored mode loads as.
//   Touch — plays back, but the user's hand wins WHILE a parameter gesture is in flight: the
//           applier skips a claimed parameter, and the gesture is captured and committed when the
//           user lets go. Let go and playback resumes immediately.
//   Latch — like Touch, except the capture span stays open after the gesture ends and only closes
//           on stop (or when global record is disabled) — the last value the user set "latches".
//   Write — while the transport is playing and global record is enabled, the lane does not play
//           back at all and its whole play-to-stop span is overwritten. With global record OFF a
//           Write lane behaves exactly like Read. Auto-drops to Touch on stop (Cubase behaviour).
enum class LaneRecordMode : int {
    Off = 0,
    Read = 1,
    Touch = 2,
    Latch = 3,
    Write = 4,
};

static_assert(static_cast<int>(LaneRecordMode::Off) == 0,
              "LaneRecordMode is serialised as an int — renumbering breaks files");
static_assert(static_cast<int>(LaneRecordMode::Read) == 1,
              "LaneRecordMode is serialised as an int — renumbering breaks files");
static_assert(static_cast<int>(LaneRecordMode::Touch) == 2,
              "LaneRecordMode is serialised as an int — renumbering breaks files");
static_assert(static_cast<int>(LaneRecordMode::Latch) == 3,
              "LaneRecordMode is serialised as an int — renumbering breaks files");
static_assert(static_cast<int>(LaneRecordMode::Write) == 4,
              "LaneRecordMode is serialised as an int — renumbering breaks files");

// One note inside a clip. startBeat is CLIP-RELATIVE (offset from the clip's own startBeat),
// so moving a clip moves its notes with it and never rewrites them. `id` is a stable,
// doc-assigned handle — never reused, and it survives a toVar/fromVar round trip the
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
// audio-thread snapshot build a flatten instead of a sort. The id is only a tiebreaker
// (startBeat/pitch collisions are legal — e.g. a chord). TimelineDoc's mutation API maintains
// the order on every path and fromVar re-establishes it defensively.
//
// One Clip type covers both MIDI and audio: a MIDI clip is one
// whose `assetRef` is empty and whose notes carry the music; an audio clip is one that names a
// rendered asset instead. Nothing forbids a clip from carrying both — the model is deliberately
// permissive, and it is the track's TrackKind that says which half a player reads. Every audio
// field is ADDITIVE (absent in a file => the default below) and the format version stays 1.
struct Clip {
    ClipId id;
    juce::String name;
    double startBeat = 0.0;
    double lengthBeats = 4.0;
    std::vector<MidiNote> notes;

    // The clip's audio asset, as a path RELATIVE TO THE BUNDLE ROOT ("Audio/take-1.wav").
    // EMPTY for a MIDI clip. The relative-only rule is a security boundary, not a convenience:
    // a bundle can be handed to someone else, and an absolute (or `..`-escaping) path baked into
    // one would read whatever happens to sit at that path on their machine — the same reasoning
    // that keeps a provider-authored patch's `"state"` on the trusted path only (see
    // ModuleBase::setExtraState and the asset policy on ProjectBundle). setClipAsset and fromVar
    // both REJECT anything that is not bundle-relative; there is no sanitising fallback.
    juce::String assetRef;

    // Playback gain applied to the asset, in dB. 0 dB = unity, and the value is deliberately
    // unbounded here (the model does not know what a sane ceiling is for a given renderer).
    double gainDb = 0.0;

    // Fades measured in beats from the clip's own start / end. 0 = no fade. Never negative.
    double fadeInBeats = 0.0;
    double fadeOutBeats = 0.0;

    // Where inside the ASSET this clip starts reading, in seconds. Seconds rather than beats
    // because it indexes a recorded file, whose samples do not move when the tempo map does.
    double sourceStartSeconds = 0.0;
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

    // The target parameter's index within its processor's getParameters(), captured ONCE at
    // lane-creation time (never re-derived afterwards). -1 means "no hint" — every lane on a
    // non-plugin module (the resolver only ever consults this for
    // a HostedPluginModule node; see Source/Timeline/AutomationBinding.h). It exists to let a lane
    // whose hosted plugin has no stable parameter ids at all (a legacy format) still re-bind after a
    // reload, WITHOUT ever letting an index match paper over a plugin update that moved a DIFFERENT,
    // still-stably-identified parameter into that slot — that case orphans instead. Additive field:
    // absent in a file loads as -1.
    int paramIndexHint = -1;

    // A LaneRecordMode value, stored as an int because it is serialised as one. Read is the
    // default and what a file that predates the field loads as. Written through
    // TimelineDoc::setLaneRecordMode, which validates the range — a raw int outside 0..4 must never
    // reach the snapshot, where the applier switches on it.
    int recordMode = static_cast<int>(LaneRecordMode::Read);

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
    bool orphaned = false;
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
    juce::String bindingUuid; // MIDI tracks: uuid of the Track In node this track feeds.
                              // May be empty — an unbound track is legal, it just plays nowhere.
    std::vector<Clip> clips;
    std::vector<AutomationLane> lanes;

    // RUNTIME-ONLY: same contract as AutomationLane::orphaned, but for bindingUuid — see
    // that field's comment for the full unbound-vs-orphaned distinction. Never written by toVar,
    // always false after fromVar, and the only writer is TimelineDoc::reconcileBindings.
    bool orphaned = false;
};

// The timeline's message-thread document model: tracks, clips, notes and automation
// lanes, in beats. Mutable and serialisable, with no GUI or editor dependency.
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
    // The model is bounded before validateTimeline ever sees untrusted input: both the
    // mutation API and fromVar REJECT anything that would exceed a cap rather than clamping or
    // truncating, so a doc in memory can never be larger than these allow. Sized well above
    // anything a human would author by hand.
    static constexpr int kMaxTracks = 256;
    static constexpr int kMaxClipsPerTrack = 4096;
    static constexpr int kMaxNotesPerClip = 16384;
    static constexpr int kMaxLanesPerTrack = 512;
    static constexpr int kMaxBreakpointsPerLane = 16384;

    // Upper bound on any id or next-id counter fromVar() accepts, so `id + 1` on the next
    // allocation can never signed-overflow. Generous relative to anything kMaxTracks etc. could
    // ever assign, but far below INT64_MAX.
    static constexpr std::int64_t kMaxIdValue = 1'000'000'000'000'000; // 1e15

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

    // -- Audio clips ------------------------------------------------------------
    // Points the clip at an audio asset. `assetRef` MUST be bundle-root-relative — an absolute
    // path, a Windows drive letter, a leading '/' or '\', or any `..` segment is REJECTED outright
    // (no mutation, returns false), because a bundle is a document that gets handed to other
    // machines. Passing an EMPTY assetRef is legal and means "this clip has no asset" (the MIDI
    // default); `sourceStartSeconds` must be finite and >= 0. Setting exactly what the clip
    // already has is a no-op, like every other setter here.
    bool setClipAsset(ClipId id, const juce::String& assetRef, double sourceStartSeconds);
    // Playback gain in dB. Rejects a non-finite value; otherwise unbounded (see Clip::gainDb).
    bool setClipGainDb(ClipId id, double gainDb);
    // Fade lengths in beats from the clip's start / end. Both must be finite and >= 0; they are
    // NOT clamped against the clip's length, so a later resize cannot silently rewrite them.
    bool setClipFades(ClipId id, double fadeInBeats, double fadeOutBeats);

    /** True if `ref` is a legal bundle-relative asset reference: empty (no asset), or a relative
     *  path with no `..` segment, no leading separator and no drive letter. The single predicate
     *  setClipAsset and fromVar both gate on — see Clip::assetRef for why it is a security rule
     *  rather than a formatting one. */
    static bool isValidAssetRef(const juce::String& ref);

    // -- Notes ----------------------------------------------------------------
    // Assigns and returns a stable NoteId; an invalid id means the note was rejected — a
    // non-finite or negative startBeat, a non-positive length, pitch outside 0..127, velocity
    // outside 1..127, channel outside 1..16, or a clip already at kMaxNotesPerClip.
    NoteId addNote(ClipId clipId, const MidiNote& note);
    bool clearNotes(ClipId clipId);

    // Note editing API. Each validates first and only then mutates: a rejection or a
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
    // `paramIndexHint` is a hosted-plugin fallback capture (-1 = none; see
    // AutomationLane::paramIndexHint). Every non-plugin caller omits it.
    LaneId addLane(TrackId trackId, const juce::String& nodeUuid, const juce::String& paramId,
                   const AutomationLane::RangeSnapshot& range, int paramIndexHint = -1);
    bool removeLane(LaneId id);
    // Sorted insert. A point at the exact same beat REPLACES the existing one. `value` is
    // denormalised and clamped into the lane's range snapshot; `tension` is clamped to [-1, 1];
    // `curve` must be a BreakpointCurve value.
    bool addBreakpoint(LaneId laneId, double beat, double value, float tension = 0.0f,
                       int curve = static_cast<int>(BreakpointCurve::Linear));
    bool removeBreakpoint(LaneId laneId, double beat);

    // The automation lane editor's ONE batched commit primitive. A single user gesture
    // (a pencil stroke, a straight line, a dragged handle, an eraser sweep) can touch several
    // points at once, and each such gesture must cost exactly one revision bump / one
    // Listener::timelineChanged call — never one per point moved, or every downstream republish
    // (audio-thread snapshot, undo capture) fires once per point instead of once per gesture.
    //
    // Removes every existing point whose beat is in `removeBeats` (a beat with no matching point
    // is silently skipped, same as removeBreakpoint), then inserts every point in `addPoints`,
    // each validated and clamped exactly like addBreakpoint (finite beat >= 0, finite value/
    // tension, a legal curve; value clamped into the lane's range, tension into [-1, 1]; a point
    // whose beat collides with an earlier one already inserted from `addPoints` replaces it, the
    // same "last one wins" rule a single addBreakpoint call has for a repeated beat). Order and
    // duplicates within `removeBeats` don't matter.
    //
    // Rejected outright — no mutation — if the lane doesn't resolve, if ANY point in `addPoints`
    // is invalid, or if the resulting point count would exceed kMaxBreakpointsPerLane. Both lists
    // empty is a no-op (no revision bump, no notification), like every other mutator here.
    bool editBreakpoints(LaneId laneId, const std::vector<double>& removeBeats,
                         const std::vector<AutomationLane::Breakpoint>& addPoints);

    // Sets the lane's record mode. `mode` must be a LaneRecordMode value (0..4) — anything
    // else is rejected outright rather than clamped, so an out-of-range int can never reach the
    // snapshot the audio thread switches on. Setting the mode the lane already has is a no-op (no
    // revision bump, no notification), like every other setter here.
    //
    // NOT a user edit in the undo sense: a mode flip records no musical intent and is deliberately
    // never wrapped in AppUndoManager::recordTimelineChange — synth::AutomationRecorder's
    // Write-drops-to-Touch-on-stop rule calls this directly, and undoing the data a take wrote must
    // not silently re-arm the lane.
    bool setLaneRecordMode(LaneId id, int mode);

    const AutomationLane* getLane(LaneId id) const;
    const Track* getTrackForLane(LaneId id) const;
    // The lane bound to (nodeUuid, paramId), or nullptr — the doc-wide identity lookup addLane
    // dedupes against.
    const AutomationLane* getLaneForParam(const juce::String& nodeUuid, const juce::String& paramId) const;

    // -- Bindings / reconciliation -----------------------------------------------
    // Recomputes EVERY track's and lane's `orphaned` flag: orphaned = binding is non-empty AND
    // uuidResolves(binding) returns false. This doc never sees an AudioProcessorGraph itself —
    // synth::TimelineReconciler is the bridge that builds `uuidResolves` from a real graph's live
    // node uuids and is the intended caller for anything graph-aware; this overload exists so the
    // doc (and its tests) stay graph-agnostic and headless.
    //
    // Routed through the single applyMutation choke point ONLY if at least one flag actually
    // changes: one revision bump and one Listener::timelineChanged call cover however many
    // bindings flipped in this pass. A reconcile that changes nothing (including one run against
    // an empty doc) is a genuine no-op — returns false, no bump, no notification.
    //
    // NOT a user edit: reconciliation derives runtime state from the current graph, it doesn't
    // record an intent a user should be able to undo. Callers must never wrap this in
    // AppUndoManager::recordTimelineChange (or recordCombinedChange) — see docs/architecture.md.
    //
    // `laneResolves` is an OPTIONAL richer predicate for lane orphaning specifically: a lane
    // whose node resolves may still need to orphan when that node is a HostedPluginModule and its
    // parameter set no longer safely matches (see Source/Timeline/AutomationBinding.h). Left unset
    // (the graph-agnostic overload every headless TimelineDoc test uses), a lane's orphan status
    // falls back to `uuidResolves(nodeUuid)` alone. Track
    // bindings are never affected: a track binds to a node, not a parameter, so `uuidResolves` alone
    // decides `Track::orphaned` either way.
    bool reconcileBindings(const std::function<bool(const juce::String& uuid)>& uuidResolves,
                           const std::function<bool(const juce::String& uuid, const juce::String& paramId,
                                                    int paramIndexHint)>& laneResolves = {});

    // The one-click "re-bind" gesture's model half: retargets an orphaned (or any) lane's
    // nodeUuid. Rejected — no mutation — if newNodeUuid is empty, if `id` doesn't resolve to a
    // lane, or if ANOTHER lane anywhere in the doc already owns (newNodeUuid, this lane's
    // paramId): the same doc-wide one-lane-per-parameter invariant addLane enforces.
    //
    // On success, clears `orphaned` to false OPTIMISTICALLY — this does not itself confirm
    // newNodeUuid resolves to a live node; the next reconcileBindings re-derives the true state
    // from the graph. A normal mutation: bumps the revision and notifies listeners exactly like
    // any other mutator (unlike reconcileBindings, this IS a user gesture and callers should wrap
    // it in recordTimelineChange).
    bool rebindLane(LaneId id, const juce::String& newNodeUuid);

    // -- Document-level --------------------------------------------------------
    // Drops every track. Id counters are NOT reset — ids stay unique for the doc's lifetime.
    // No-op (and no notification) on an already-empty doc.
    void clear();

    bool isEmpty() const noexcept { return tracks.empty(); }

    // Bumped once per effective mutation. Starts at 0 on a fresh doc; consumers cache it to
    // tell "nothing happened" from "something did" without diffing the model.
    std::int64_t getRevision() const noexcept { return revision; }

    // -- Serialisation ---------------------------------------------------------
    // The dialect PatchDocument embeds under the reserved top-level "timeline" key.
    // Field names are lowerCamelCase; the next-id counters ride along so ids stay stable
    // across save/load. Track::orphaned / AutomationLane::orphaned are NOT written — they are
    // runtime-derived, not part of the document's persistent identity.
    juce::var toVar() const;

    // All-or-nothing: on ANY malformed field (wrong type, out-of-range value, duplicate id,
    // unknown track kind, a cap exceeded, a wrong "version") this returns false and leaves the
    // doc completely untouched — same content, same revision, no notification. On success it
    // replaces the whole doc, bumps the revision once and fires one notification.
    //
    // Ordering is repaired rather than trusted: a hand-edited file with mis-ordered clips,
    // notes or breakpoints loads fine and comes back sorted, because no reader downstream
    // should have to defend against a broken sort invariant.
    //
    // Every loaded Track/AutomationLane starts with orphaned == false, whatever a hand-edited
    // file might claim (the field isn't read at all) — there is no graph at load time to check
    // bindings against, so the caller must run reconcileBindings (via TimelineReconciler)
    // afterwards to get correct flags.
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
